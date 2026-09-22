#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include "libswscale/swscale.h"
}

#include <spdlog/spdlog.h>
// 统计汇总在解锁后才输出：先在锁内 fmt::format 成字符串（见下方死锁说明）。
#include <spdlog/fmt/fmt.h>
#include "KRMoviePlayer.h"
#include "../../utils/StallWatchdog.h"
#include "VideoCodec.h"
#include "CodecUtils.h"
#include "AudioDevice.h"
#include "WaveMixer.h"
#include "WindowImpl.h"
#include "VideoOvlImpl.h"

extern std::thread::id TVPMainThreadID;

// 宿主视频 overlay 提交接口 —— 实现落在 cpp/core/environ/stubs/ui_stubs.cpp 的
// HostWindowLayer（与 TVPSetPostDrawHook 同一种做法：本地 extern 声明，不新增
// 头文件）。引擎把解码出的 RGBA 帧拷给宿主层，宿主层在本帧场景 blit 之后把它
// 当作一张纹理叠画到宿主 render target 上（overlay 模式下视频盖在画面之上）。
extern bool TVPHostSubmitVideoOverlayFrame(const void *rgba, int width,
                                           int height, int stride_bytes,
                                           int left, int top, int right,
                                           int bottom);
extern void TVPHostClearVideoOverlayFrame();

NS_KRMOVIE_BEGIN

// ── 电影链路低频统计（声明见 KRMoviePlayer.h）───────────────────────────────
// 两条链路（layer / overlay）共用一份表，按 tag 分开累计；每 5 秒各输出一行。
namespace {
    struct MovieStatsState {
        uint64_t frames = 0;       // 解码后入队的帧数
        uint64_t presents = 0;     // 真正呈现/提交的帧数
        uint64_t futureSkips = 0;  // 因 pts 还没到而未呈现的次数
        uint64_t convertUs = 0;    // YUV→RGBA 累计耗时
        uint64_t convertMaxUs = 0; // 单帧转换耗时峰值
        std::chrono::steady_clock::time_point windowStart{};
    };

    std::mutex g_movieStatsMutex;
    std::map<std::string, MovieStatsState> g_movieStats;

    // 只在持锁时调用：累计计数；窗口到点就把汇总文本写进 outMsg 并返回 true。
    //
    // ⚠️ 这里**绝对不能**直接调 spdlog。spdlog 的 StartupLogSink 内部要取
    // `g_registry_mutex`，而 `engine_tick()` 整帧持有同一把锁并会调用
    // `TVPMovieStatsNotePresent()`。若本函数在持 `g_movieStatsMutex` 时打日志，
    // 两个线程的加锁顺序正好相反（解码线程 movieStats→registry；引擎线程
    // registry→movieStats）⇒ 必然死锁。真机表现就是：传统兼容层播放视频时
    // 卡死到 ANR（首个 5 秒窗口到点的那一刻）。
    // 所以只在这里拼字符串，真正的输出交给解锁之后的调用方。
    bool MovieStatsMaybeFillReportLocked(const char *tag, MovieStatsState &s,
                                         std::string &outMsg, bool &outStarved) {
        const auto now = std::chrono::steady_clock::now();
        if(s.windowStart.time_since_epoch().count() == 0) {
            s.windowStart = now;
            return false;
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - s.windowStart)
                            .count();
        if(ms < 5000)
            return false;
        const double secs = static_cast<double>(ms) / 1000.0;
        const double avgConvertMs =
            s.frames ? static_cast<double>(s.convertUs) /
                    static_cast<double>(s.frames) / 1000.0
                     : 0.0;
        // "有声音没画面"的指纹：解码一直在进帧（音频独立解码，所以声音照常），
        // 但一帧都没上屏。把它单独标出来，免得混在普通统计里被忽略。
        outStarved = s.frames > 0 && s.presents == 0;
        outMsg = fmt::format(
            "movie[{}]: 入队 {:.1f} 帧/秒，呈现 {:.1f} 帧/秒，pts未到跳过 {}，"
            "转换 avg={:.2f}ms max={:.2f}ms（窗口 {:.1f}s）",
            tag ? tag : "?", static_cast<double>(s.frames) / secs,
            static_cast<double>(s.presents) / secs, s.futureSkips,
            avgConvertMs, static_cast<double>(s.convertMaxUs) / 1000.0, secs);
        s = MovieStatsState{};
        s.windowStart = now;
        return true;
    }

    MovieStatsState &MovieStatsLocked(const char *tag) {
        return g_movieStats[tag ? tag : "?"];
    }
} // namespace

void TVPMovieStatsNoteDecode(const char *tag, uint64_t convertUs) {
    std::string msg;
    bool due = false;
    bool starved = false;
    {
        std::lock_guard<std::mutex> lk(g_movieStatsMutex);
        MovieStatsState &s = MovieStatsLocked(tag);
        ++s.frames;
        s.convertUs += convertUs;
        if(convertUs > s.convertMaxUs)
            s.convertMaxUs = convertUs;
        due = MovieStatsMaybeFillReportLocked(tag, s, msg, starved);
    }
    if(due) {
        if(starved)
            spdlog::warn("movie[{}]: 解码在进帧但一帧都没上屏（有声音没画面的指纹）—— {}",
                         tag ? tag : "?", msg);
        else
            spdlog::info("{}", msg); // 已解锁，见上面的死锁说明
    }
}

void TVPMovieStatsNotePresent(const char *tag, bool ptsNotYet) {
    std::string msg;
    bool due = false;
    bool starved = false;
    {
        std::lock_guard<std::mutex> lk(g_movieStatsMutex);
        MovieStatsState &s = MovieStatsLocked(tag);
        if(ptsNotYet)
            ++s.futureSkips;
        else
            ++s.presents;
        due = MovieStatsMaybeFillReportLocked(tag, s, msg, starved);
    }
    if(due) {
        if(starved)
            spdlog::warn("movie[{}]: 解码在进帧但一帧都没上屏（有声音没画面的指纹）—— {}",
                         tag ? tag : "?", msg);
        else
            spdlog::info("{}", msg); // 已解锁，见上面的死锁说明
    }
}

// 开片时记一次片源与链路（三条 BuildGraph 都调它）：出问题时第一件要确认的事
// 就是"这条电影到底开没开、走的是 layer 还是 overlay、片源声明的帧率/尺寸是多少"
// —— 声明的 60fps 落在只跑 30fps 的引擎上，与"解码慢"是两种完全不同的结论。
void TVPMovieLogOpened(TVPMoviePlayer *player, const char *tag,
                       const char *name_utf8) {
    if(!player)
        return;
    double fps = 0.0;
    long w = 0, h = 0;
    int frames = 0;
    player->GetFPS(&fps);
    player->GetVideoSize(&w, &h);
    player->GetNumberOfFrame(&frames);
    spdlog::info("movie[{}]: 打开片源 {} fps={:.3f} 尺寸={}x{} 总帧数={}",
                 tag ? tag : "?", name_utf8 ? name_utf8 : "?", fps, w, h,
                 frames);
}



TVPMoviePlayer::TVPMoviePlayer() { m_pPlayer = new BasePlayer(this); }

TVPMoviePlayer::~TVPMoviePlayer() {
    // 关键顺序：先唤醒可能卡在"等空槽位"的解码线程，再删播放器。否则
    // delete m_pPlayer → CloseStream/StopThread 会去 join 那条线程，而它正因为
    // 队列满（游戏已停止消费）永远等不到空槽位 —— 渲染线程就此无限期挂住。
    // （为什么**不**把它丢给后台线程：BasePlayer 的析构会经 m_pRenderer 回调它的
    //  所有者，而所有者此刻正在析构；延迟释放会让 BasePlayer 活得比所有者更久 →
    //  悬垂。当前阻塞点在临时文件删除上，见 VideoOvlImpl.cpp 的说明。）
    AbortPictureWait();
    krkr::stall::MarkStage("movie: ~TVPMoviePlayer→删除播放器(join player+解码线程)");
    // 析构里的 join（~BasePlayer→CloseInputStream→StopThread）没有上界，先请求停播。
    // Release() 与 ~MoviePlayerOverlay 已请求过，这里是其余删除路径的兜底；幂等。
    if(m_pPlayer)
        m_pPlayer->RequestStop();
    delete m_pPlayer;
    m_pPlayer = nullptr;
    krkr::stall::MarkStage("movie: ~TVPMoviePlayer→播放器已销毁");
    if(img_convert_ctx)
        sws_freeContext(img_convert_ctx), img_convert_ctx = nullptr;
}

void TVPMoviePlayer::Release() {
    if(RefCount == 1) {
        // **先请求停播，再有界确认影片线程退出，最后才销毁**。
        //
        // 为什么不能只"等"：`BasePlayer::Process()` 的循环条件只有 `m_bAbortRequest`，
        // 而它原先只在 `~BasePlayer`→`CloseInputStream()` 里置位 —— 也就是"等线程
        // 退出"等的是一个**没人叫停**的线程，必然等满整个窗口（真机：切 CG 视频
        // update_max=4382/4553/4731ms、fps 掉到 3–18，每次都打"未退出，放弃销毁"）。
        // `RequestStop()` 会置中断标志并中断 demuxer 读取，正常情况下几十毫秒内线程
        // 就退出；4s 兜底保留 —— 它是"整机卡死只能杀进程"的安全网。
        //
        // 退不出去时**宁可泄漏整个影片对象**（什么都不释放，线程还在用它），也绝不
        // 让渲染线程卡住 —— 泄漏一个影片对象，换来的是游戏还能继续玩。
        constexpr unsigned kTeardownWaitMs = 4000;
        if(m_pPlayer) {
            // **先唤醒可能卡在"等空 picture 槽位"的解码线程**。
            //
            // `AddVideoPicture()` 里那个 50ms 分片等待只认 `m_pictureWaitAbort`，
            // 不认播放线程的中断标志（`m_bAbortRequest` / `m_bAbortOutput`）；而它的
            // 消费者是渲染线程每帧的 `GetFrontBuffer()` —— 渲染线程此刻正卡在本函数里，
            // 所以队列一定会填满、解码线程一定会停在那儿。不置位的话
            // `OnExit → CloseStream(video) → StopThread()` 的 join 永远回不来，
            // `WaitForExit()` 必然等满 4s（真机 2026-09-23 00:07 日志实证：
            // `停播请求后影片线程 4000ms 仍未退出`，而影片线程阶段停在
            // `解码线程→处理消息/解码`）。
            //
            // 为什么以前只放在 `Stop()`/析构里不够：`VideoOvlImpl::Close()` 走的是
            // `Pause() → Release()`，从不经过 `Stop()`。
            AbortPictureWait();
            m_pPlayer->RequestStop();
            const auto t0 = std::chrono::steady_clock::now();
            const bool exited = m_pPlayer->WaitForExit(kTeardownWaitMs);
            const auto waitedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
            if(!exited) {
                spdlog::error(
                    "movie: 停播请求后影片线程 {}ms 仍未退出，放弃销毁并泄漏该影片"
                    "对象（渲染线程绝不 join 它；否则整机卡死，只能杀进程）",
                    kTeardownWaitMs);
                return; // 故意不 delete：对象与线程都继续存活
            }
            // 正常应在百毫秒内。每关一片一行，用来验证"切视频卡顿已消失"。
            spdlog::info("movie: 停播请求→影片线程退出耗时 {}ms", waitedMs);
        }
        delete this;
    } else {
        RefCount--;
    }
}

void TVPMoviePlayer::SetPosition(uint64_t tick) { m_pPlayer->SeekTime(tick); }

void TVPMoviePlayer::GetPosition(uint64_t *tick) {
    if(tick)
        *tick = m_pPlayer->GetTime();
}

void TVPMoviePlayer::GetStatus(tTVPVideoStatus *status) {
    if(m_pPlayer->IsStop())
        *status = vsStopped;
    else if(m_pPlayer->GetSpeed() == 0)
        *status = vsPaused;
    else
        *status = vsPlaying;
    //	else *status = vsProcessing;
}

void TVPMoviePlayer::Rewind() { SetPosition(0); }

void TVPMoviePlayer::SetFrame(int f) {
    // TODO seek accurately
    m_pPlayer->SeekTime(f / m_pPlayer->GetFPS() * DVD_PLAYSPEED_NORMAL);
}

void TVPMoviePlayer::GetFrame(int *f) { *f = m_pPlayer->GetCurrentFrame(); }

void TVPMoviePlayer::GetFPS(double *f) { *f = m_pPlayer->GetFPS(); }

void TVPMoviePlayer::GetNumberOfFrame(int *f) {
    *f = m_pPlayer->GetTotalTime() * m_pPlayer->GetFPS() / DVD_PLAYSPEED_NORMAL;
}

void TVPMoviePlayer::GetTotalTime(int64_t *t) {
    *t = m_pPlayer->GetTotalTime();
}

void TVPMoviePlayer::GetVideoSize(long *width, long *height) {
    m_pPlayer->GetVideoSize(width, height);
}

void TVPMoviePlayer::SetPlayRate(double rate) { m_pPlayer->SetSpeed(rate); }

void TVPMoviePlayer::GetPlayRate(double *rate) {
    *rate = m_pPlayer->GetSpeed();
}

iTVPSoundBuffer *TVPMoviePlayer::GetSoundDevice() {
    IDVDStreamPlayerAudio *audioplayer = m_pPlayer->GetAudioPlayer();
    if(!audioplayer)
        return nullptr;
    IAEStream *audiostream = audioplayer->GetOutputDevice()->m_pAudioStream;
    if(!audiostream)
        return nullptr;
    return audiostream->GetNativeImpl();
}

void TVPMoviePlayer::GetAudioBalance(long *balance) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound) {
        *balance = alsound->GetPan() * 100000;
    }
}

void TVPMoviePlayer::SetAudioBalance(long balance) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound) {
        alsound->SetPan(balance / 100000.0f);
    }
}

void TVPMoviePlayer::SetAudioVolume(long volume) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound)
        alsound->SetVolume(volume / 100000.f);
}

void TVPMoviePlayer::GetAudioVolume(long *volume) {
    iTVPSoundBuffer *alsound = GetSoundDevice();
    if(alsound)
        *volume = alsound->GetVolume() * 100000;
}

void TVPMoviePlayer::GetNumberOfAudioStream(unsigned long *streamCount) {
    *streamCount = m_pPlayer->GetAudioStreamCount();
}

void TVPMoviePlayer::SelectAudioStream(unsigned long iStream) {
    m_pPlayer->GetMessageQueue().Put(new CDVDMsgPlayerSetAudioStream(iStream));
    m_pPlayer->SynchronizeDemuxer();
}

void TVPMoviePlayer::GetEnableAudioStreamNum(long *num) {
    *num = m_pPlayer->GetAudioStream();
}

void TVPMoviePlayer::DisableAudioStream() {
    // TODO
}

void TVPMoviePlayer::GetNumberOfVideoStream(unsigned long *streamCount) {
    *streamCount = m_pPlayer->GetVideoStreamCount();
}

void TVPMoviePlayer::SelectVideoStream(unsigned long iStream) {
    m_pPlayer->GetMessageQueue().Put(new CDVDMsgPlayerSetVideoStream(iStream));
    m_pPlayer->SynchronizeDemuxer();
}

void TVPMoviePlayer::GetEnableVideoStreamNum(long *num) {
    *num = m_pPlayer->GetVideoStream();
}

int TVPMoviePlayer::WaitForBuffer(volatile std::atomic_bool &bStop,
                                  int timeout) {
    int remainBuf = MAX_BUFFER_COUNT - m_usedPicture;
    if(remainBuf > 0)
        return remainBuf;
    std::unique_lock<std::mutex> lk(m_mtxPicture);
    while(!bStop && MAX_BUFFER_COUNT <= m_usedPicture && timeout > 0) {
        timeout -= 10;
        m_condPicture.wait_for(lk, std::chrono::milliseconds(10));
    }
    return MAX_BUFFER_COUNT - m_usedPicture - 1;
}

void TVPMoviePlayer::Flush() {
    std::unique_lock<std::mutex> lk(m_mtxPicture);
    // Flush 会把未呈现的帧直接丢掉（m_usedPicture 归零）——"解码在跑却永远没有
    // submitted"的一种来路。谁在放片中途调它就一目了然，故记录丢弃数量。
    static std::atomic<int> s_flushLogs{ 0 };
    if(s_flushLogs.fetch_add(1) < 3)
        spdlog::info("MoviePlayer Flush: 丢弃 {} 帧待呈现缓冲（curPicture={}）",
                     m_usedPicture, m_curPicture);
    for(int i = 0; i < MAX_BUFFER_COUNT; ++i) {
        m_picture[i].Clear();
    }
    m_curpts = 0.0;
    m_usedPicture = 0;
}

void TVPMoviePlayer::FrameMove() { m_pPlayer->FrameMove(); }

void TVPMoviePlayer::SetLoopSegement(int beginFrame, int endFrame) {
    m_pPlayer->SetLoopSegement(beginFrame, endFrame);
}

static inline uint8_t ClampByte(int value) {
    if(value < 0)
        return 0;
    if(value > 255)
        return 255;
    return static_cast<uint8_t>(value);
}

// sws_scale 兜底：上下文建不起来（裁剪异常等）时手写 YUV420P→RGBA，
// 与 AetherKiri 一致。BT.601 limited range，够视频看。
static void ConvertYuv420ToRgba(const DVDVideoPicture &pic, uint8_t *dst,
                                int dstWidth, int dstHeight, int dstStride) {
    const int copyWidth = std::min<int>(dstWidth, pic.iWidth);
    const int copyHeight = std::min<int>(dstHeight, pic.iHeight);
    for(int y = 0; y < copyHeight; ++y) {
        const uint8_t *yRow = pic.data[0] + y * pic.iLineSize[0];
        const uint8_t *uRow = pic.data[1] + (y / 2) * pic.iLineSize[1];
        const uint8_t *vRow = pic.data[2] + (y / 2) * pic.iLineSize[2];
        uint8_t *out = dst + static_cast<size_t>(y) * dstStride;
        for(int x = 0; x < copyWidth; ++x) {
            int c = static_cast<int>(yRow[x]) - 16;
            int d = static_cast<int>(uRow[x / 2]) - 128;
            int e = static_cast<int>(vRow[x / 2]) - 128;
            if(c < 0)
                c = 0;
            out[x * 4 + 0] = ClampByte((298 * c + 409 * e + 128) >> 8);
            out[x * 4 + 1] =
                ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
            out[x * 4 + 2] = ClampByte((298 * c + 516 * d + 128) >> 8);
            out[x * 4 + 3] = 0xff;
        }
    }
}

int TVPMoviePlayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
    // from other thread
    //
    // 队列里存 **RGBA**（sws_scale 转换），overlay 呈现（宿主纹理共享）与 layer
    // 路径都按 RGBA 消费。直接存 YUV 是 cocos2d-x 时代的做法：那时由
    // TVPYUVSprite 在着色器里做 YUV→RGB；场景树移除后没有消费方，
    // overlay 提交会把 Y 平面当 RGBA 读（越界 + 花屏）。
    if(pic.format != RENDER_FMT_YUV420P) {
        static std::atomic<int> s_formatSkips{ 0 };
        if(s_formatSkips.fetch_add(1) < 3)
            spdlog::warn("MoviePlayer AddVideoPicture: 丢弃非 YUV420P 帧 "
                         "format={} pts={}",
                         static_cast<int>(pic.format), pic.pts);
        return -2;
    }
    if(pic.pts == DVD_NOPTS_VALUE) {
        static std::atomic<int> s_noptsSkips{ 0 };
        if(s_noptsSkips.fetch_add(1) < 3)
            spdlog::warn("MoviePlayer AddVideoPicture: 丢弃无 pts 帧 {}x{}",
                         pic.iWidth, pic.iHeight);
        return 0;
    }

    if(m_usedPicture >= MAX_BUFFER_COUNT) {
        // 有界 + 可中止地等空槽位：消费者是渲染线程每帧的 GetFrontBuffer()，
        // 一旦游戏不再消费（片段播完/切换中），无界等待会把解码线程永久钉在这里，
        // 进而让停播路径的 StopThread()（join 这条线程）无限期挂住渲染线程 ——
        // 真机表现就是播 CG 视频时整机无响应、十几秒后被 ANR。停播/析构会调用
        // AbortPictureWait() 置位并唤醒，这里每 50ms 复查一次。
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        while(m_usedPicture >= MAX_BUFFER_COUNT &&
              !m_pictureWaitAbort.load(std::memory_order_acquire)) {
            krkr::stall::MarkMovieStage("movie: 解码线程→等空 picture 槽位(overlay)");
            m_condPicture.wait_for(lk, std::chrono::milliseconds(50));
        }
        if(m_pictureWaitAbort.load(std::memory_order_acquire))
            return -1;
    }
    if(m_usedPicture >= MAX_BUFFER_COUNT)
        return -1;

    const int srcWidth = pic.iWidth;
    const int srcHeight = pic.iHeight;
    const int width = pic.iDisplayWidth > 0 ? pic.iDisplayWidth : pic.iWidth;
    const int height =
        pic.iDisplayHeight > 0 ? pic.iDisplayHeight : pic.iHeight;
    if(srcWidth <= 0 || srcHeight <= 0 || width <= 0 || height <= 0 ||
       !pic.data[0] || !pic.data[1] || !pic.data[2])
        return -1;
    uint8_t *data =
        (uint8_t *)TJSAlignedAlloc(static_cast<size_t>(width) * height * 4, 4);
    if(!data)
        return -1;
    uint8_t *dstData[4] = { data, nullptr, nullptr, nullptr };
    int dstLineSize[4] = { width * 4, 0, 0, 0 };

    img_convert_ctx = sws_getCachedContext(
        img_convert_ctx, srcWidth, srcHeight, AV_PIX_FMT_YUV420P, width, height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    // 逐帧软件转换是 overlay 链路里最重的 CPU 工作（每帧一次全画面），单独计时
    // 上报，便于判断"帧率低"是不是它造成的。
    const auto convertStart = std::chrono::steady_clock::now();
    int processed = 0;
    if(img_convert_ctx) {
        processed = sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0,
                              srcHeight, dstData, dstLineSize);
    }
    if(processed <= 0) {
        std::memset(data, 0, static_cast<size_t>(width) * height * 4);
        ConvertYuv420ToRgba(pic, data, width, height, dstLineSize[0]);
    }
    TVPMovieStatsNoteDecode(
        "overlay",
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - convertStart)
                .count()));

    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf =
            m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
        picbuf.Clear();
        picbuf.width = width;
        picbuf.height = height;
        picbuf.rgba = data;
        picbuf.pts = pic.pts / DVD_TIME_BASE;
        ++m_usedPicture;
        static std::atomic<int> s_queueLogs{ 0 };
        if(s_queueLogs.fetch_add(1) < 3)
            spdlog::info("MoviePlayer AddVideoPicture: queued {}x{} pts={} "
                         "used={} visible={}",
                         width, height, picbuf.pts, m_usedPicture,
                         Visible ? "yes" : "no");
        return MAX_BUFFER_COUNT - m_usedPicture;
    }
}

VideoPresentOverlay::~VideoPresentOverlay() {
    TVPRemoveContinuousEventHook(this);
    TVPHostClearVideoOverlayFrame();
    ClearNode();
}

void VideoPresentOverlay::ClearNode() {
    // Overlay lifecycle is managed by the host shell.
    m_pRootNode = nullptr;
    m_pSprite = nullptr;
}

void VideoPresentOverlay::PresentPicture(float dt) {
    BitmapPicture pic;
    m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        // 入口探针（前 3 次）：门控一旦放行就会走到这里；配合上面的门控快照，
        // 就能判定"从没被放行"还是"放行了但在内部提前返回"。
        static std::atomic<int> s_entries{ 0 };
        if(s_entries.fetch_add(1) < 3)
            spdlog::info("movie[overlay]: PresentPicture 进入（curpts={:.6f} "
                         "used={} visible={}）",
                         m_curpts, m_usedPicture, Visible ? 1 : 0);
    }
    {
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        if(m_usedPicture <= 0) {
            // 被调用但没帧可拿（生产者还没入队，或 Flush 把未消费帧丢了）。
            static std::atomic<int> s_emptyLogs{ 0 };
            if(s_emptyLogs.fetch_add(1) < 3)
                spdlog::info("movie[overlay]: PresentPicture 进入但 m_usedPicture<=0，无帧可呈现");
            return;
        }
        do {
            // MoveFrom 的方向是 `this ← source`。写反成
            // `队列槽.MoveFrom(pic)` 会把**空的本地 pic** 拷进队列槽、顺手把槽里刚解出
            // 的那帧像素释放掉，而 pic 依旧是空的（0x0 / rgba=null）—— 于是每一帧都
            // 在 `!pic.rgba` 处被丢掉，overlay 模式电影就成了"有声音没画面"。
            // 真机日志（2026-09-18 13:21:15.534）：
            //   PresentPicture 进入（used=1 visible=1）
            //   PresentPicture 取到空像素帧（rgba=null，0x0 pts=0）
            pic.MoveFrom(m_picture[m_curPicture]);
            --m_usedPicture;
            if(++m_curPicture >= MAX_BUFFER_COUNT)
                m_curPicture = 0;
        } while(m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    if(!pic.rgba) {
        // 队列里有帧却没有像素：MoveFrom 拿到空指针（谁把 data[0] 清了？）。
        static std::atomic<int> s_nullLogs{ 0 };
        if(s_nullLogs.fetch_add(1) < 3)
            spdlog::warn("movie[overlay]: PresentPicture 取到空像素帧（rgba=null，"
                         "{}x{} pts={}），不呈现",
                         pic.width, pic.height, pic.pts);
        return;
    }
    if(!Visible) {
        static std::atomic<int> s_invisibleLogs{ 0 };
        if(s_invisibleLogs.fetch_add(1) == 0)
            spdlog::warn("VideoPresentOverlay: 帧已解出但 Visible=false，"
                         "不呈现（overlay 未置可见？）");
        TVPHostClearVideoOverlayFrame();
        return;
    }

    tTVPRect dest = GetBounds();
    if(dest.get_width() <= 0 || dest.get_height() <= 0)
        dest = tTVPRect(0, 0, pic.width, pic.height);
    const bool submitted = TVPHostSubmitVideoOverlayFrame(
        pic.rgba, pic.width, pic.height, pic.width * 4, dest.left, dest.top,
        dest.right, dest.bottom);
    TVPMovieStatsNotePresent("overlay", /*ptsNotYet=*/false);
    static std::atomic<int> s_submitLogs{ 0 };
    if(s_submitLogs.fetch_add(1) < 3)
        spdlog::info("VideoPresentOverlay: submitted {}x{} dest=({},{})({},{}"
                     ") ok={}",
                     pic.width, pic.height, dest.left, dest.top, dest.right,
                     dest.bottom, submitted ? 1 : 0);
}

// 呈现由两条路驱动：解码回调（OnPlayEvent Update，主线程时立即呈现）与连续
// 事件钩子（每 tick 兜底，并负责把落后于时钟的帧补上）。钩子只做判空与 pts
// 门控，实际上传频率 = 视频帧率。
void VideoPresentOverlay::OnContinuousCallback(tjs_uint64 tick) {
    if(!m_usedPicture)
        return;
    const double curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    static std::atomic<uint64_t> s_gateTicks{ 0 };
    const uint64_t gateTick = s_gateTicks.fetch_add(1) + 1;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        if(m_picture[m_curPicture].pts > curpts) {
            // 呈现门控探针：真机实测"开场视频有声音没画面"时，解码帧进得来
            // （queued ... visible=yes）却一次 submitted 都没有。这条用来区分
            // "时钟没走（curpts 一直落后于帧 pts）"与"门控通过但 PresentPicture
            // 内部提前返回"。
            //   前 30 次每次都记；之后每 120 帧（约 2 秒）记一条状态快照 ——
            //   只有周期性快照才能看出"时钟是否真的越过了帧 pts"。
            const bool early = gateTick <= 30;
            const bool periodic = (gateTick % 120) == 0;
            if(early || periodic)
                spdlog::info("movie[overlay]: 呈现门控未通过（帧 pts={:.6f} > "
                             "时钟 curpts={:.6f}，used={}，第 {} 帧）",
                             m_picture[m_curPicture].pts, curpts,
                             m_usedPicture, gateTick);
            return;
        }
    }
    PresentPicture(0.0f);
}

void KRMovie::VideoPresentOverlay::Play() {
    TVPMoviePlayer::Play();
    TVPAddContinuousEventHook(this);
    spdlog::info("VideoPresentOverlay::Play: 连续事件钩子已挂（visible={}）",
                 Visible ? "yes" : "no");
}

void KRMovie::VideoPresentOverlay::Stop() {
    TVPRemoveContinuousEventHook(this);
    TVPHostClearVideoOverlayFrame();
    TVPMoviePlayer::Stop();
}

MoviePlayerOverlay::~MoviePlayerOverlay() {
    assert(std::this_thread::get_id() == TVPMainThreadID);
    krkr::stall::MarkStage("movie: ~MoviePlayerOverlay→删除播放器");
    // 同步销毁：BasePlayer 的析构会经 m_pRenderer 回到所有者，延迟释放会造成悬垂
    // （详见 TVPMoviePlayer::~TVPMoviePlayer 的说明）。真正的阻塞点在临时文件删除，
    // 那里已改成后台执行。
    //
    // 析构侧的 join 同样没有上界（CloseInputStream→StopThread），所以这里也要先
    // 请求停播：正常路径上 Release() 已经请求过（幂等），但从别的路径直接析构时
    // 就靠这一句，避免再次出现"没人叫停却去 join"。
    if(m_pPlayer)
        m_pPlayer->RequestStop();
    delete m_pPlayer;
    m_pPlayer = nullptr;
}

void MoviePlayerOverlay::SetWindow(tTJSNI_Window *window) {
    ClearNode();
    m_pOwnerWindow = window;
    // 这行是 overlay 链路的关键指纹（每次开片一次）：有它说明 KAG 已经把
    // overlay 挂到窗口上；配合 AddVideoPicture / submitted 两条日志能一眼区分
    // "没解码"、"解了但不可见"、"提交了但宿主没画"。
    spdlog::info("MoviePlayerOverlay::SetWindow: owner={} visible={}",
                 static_cast<const void *>(window), Visible ? "yes" : "no");
}

void MoviePlayerOverlay::BuildGraph(tTJSNI_VideoOverlay *callbackwin,
                                    IStream *stream, const tjs_char *streamname,
                                    const tjs_char *type, uint64_t size) {
    m_pCallbackWin = callbackwin;
    m_pPlayer->SetCallback([this](auto &&PH1, auto &&PH2) {
        OnPlayEvent(std::forward<decltype(PH1)>(PH1),
                    std::forward<decltype(PH2)>(PH2));
    });
    m_pPlayer->OpenFromStream(stream, streamname, type, size);
    // 与 layer 链路同一行格式：日志里能直接对比"哪条链路开了这个片源"。
    const std::string srcName =
        streamname ? ttstr(streamname).AsStdString() : std::string();
    TVPMovieLogOpened(this, "overlay", srcName.c_str());
}

const tTVPRect &MoviePlayerOverlay::GetBounds() {
    return m_pCallbackWin->GetBounds();
}

void KRMovie::MoviePlayerOverlay::SetVisible(bool b) {
    VideoPresentOverlay::SetVisible(b);
    if(!b)
        TVPHostClearVideoOverlayFrame();
}

void MoviePlayerOverlay::OnPlayEvent(KRMovieEvent msg, void *p) {
    if(msg == KRMovieEvent::Update) {
        // 呈现必须落在引擎主线程（宿主 GL 调用与场景 blit 同线程）。回调不在
        // 主线程时跳过，连续事件钩子会在下个 tick 补上。
        if(std::this_thread::get_id() == TVPMainThreadID)
            PresentPicture(0.0f);
        if(m_pCallbackWin) {
            int frame;
            GetFrame(&frame);
            NativeEvent ev(WM_GRAPHNOTIFY);
            ev.WParam = EC_UPDATE;
            ev.LParam = frame;
            m_pCallbackWin->PostEvent(ev);
        }
    } else if(msg == KRMovieEvent::Ended) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

// 层路径（KRMovieLayer）在用：交换两块缓冲（fmt/pts 不参与交换，保持原行为）。
void VideoPresentOverlay::BitmapPicture::swap(BitmapPicture &r) {
    std::swap(data, r.data);
    std::swap(width, r.width);
    std::swap(height, r.height);
}

void TVPMoviePlayer::BitmapPicture::MoveFrom(BitmapPicture &source) {
    if(this == &source)
        return;
    Clear();
    fmt = source.fmt;
    width = source.width;
    height = source.height;
    pts = source.pts;
    for(int i = 0; i < sizeof(data) / sizeof(data[0]); ++i) {
        data[i] = source.data[i];
        source.data[i] = nullptr;
    }
    source.fmt = RENDER_FMT_NONE;
    source.width = 0;
    source.height = 0;
    source.pts = 0.0;
}

void TVPMoviePlayer::BitmapPicture::Clear() {
    for(int i = 0; i < sizeof(data) / sizeof(data[0]); ++i)
        if(data[i])
            TJSAlignedDealloc(data[i]), data[i] = nullptr;
    fmt = RENDER_FMT_NONE;
    width = 0;
    height = 0;
    pts = 0.0;
}

void VideoPresentOverlay2::SetRootNode(OverlayNode *node) {
    ClearNode();
    m_pRootNode = node;
}

VideoPresentOverlay2 *VideoPresentOverlay2::create() {
    return new VideoPresentOverlay2;
}

NS_KRMOVIE_END
