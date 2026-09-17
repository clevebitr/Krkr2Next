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
                                         std::string &outMsg) {
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
    {
        std::lock_guard<std::mutex> lk(g_movieStatsMutex);
        MovieStatsState &s = MovieStatsLocked(tag);
        ++s.frames;
        s.convertUs += convertUs;
        if(convertUs > s.convertMaxUs)
            s.convertMaxUs = convertUs;
        due = MovieStatsMaybeFillReportLocked(tag, s, msg);
    }
    if(due)
        spdlog::info("{}", msg); // 已解锁，见上面的死锁说明
}

void TVPMovieStatsNotePresent(const char *tag, bool ptsNotYet) {
    std::string msg;
    bool due = false;
    {
        std::lock_guard<std::mutex> lk(g_movieStatsMutex);
        MovieStatsState &s = MovieStatsLocked(tag);
        if(ptsNotYet)
            ++s.futureSkips;
        else
            ++s.presents;
        due = MovieStatsMaybeFillReportLocked(tag, s, msg);
    }
    if(due)
        spdlog::info("{}", msg); // 已解锁，见上面的死锁说明
}



TVPMoviePlayer::TVPMoviePlayer() { m_pPlayer = new BasePlayer(this); }

TVPMoviePlayer::~TVPMoviePlayer() {
    delete m_pPlayer;
    if(img_convert_ctx)
        sws_freeContext(img_convert_ctx), img_convert_ctx = nullptr;
}

void TVPMoviePlayer::Release() {
    if(RefCount == 1)
        delete this;
    else
        RefCount--;
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
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        m_condPicture.wait(lk);
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
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        if(m_usedPicture <= 0)
            return;
        do {
            m_picture[m_curPicture].MoveFrom(pic);
            --m_usedPicture;
            if(++m_curPicture >= MAX_BUFFER_COUNT)
                m_curPicture = 0;
        } while(m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    if(!pic.rgba)
        return;
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
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        if(m_picture[m_curPicture].pts > curpts)
            return;
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
