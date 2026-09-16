#include <thread>

extern "C" {
#include "libswscale/swscale.h"
}

#include <spdlog/spdlog.h>
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

int TVPMoviePlayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
    // from other thread
    if(pic.format != RENDER_FMT_YUV420P)
        return -2;
    if(pic.pts == DVD_NOPTS_VALUE)
        return 0;

    if(m_usedPicture >= MAX_BUFFER_COUNT) {
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        m_condPicture.wait(lk);
    }
    if(m_usedPicture >= MAX_BUFFER_COUNT)
        return -1;

    int width = pic.iWidth, height = pic.iHeight;
    // YUV data passthrough
    int yuvwidth[3] = { width, width / 2, width / 2 };
    int yuvheight[3] = { height, height / 2, height / 2 };
    uint8_t *yuvdata[3] = { nullptr };
    for(int i = 0; i < sizeof(yuvdata) / sizeof(yuvdata[0]); ++i) {
        int size = yuvwidth[i] * yuvheight[i];
        yuvdata[i] = (uint8_t *)TJSAlignedAlloc(size, 4);
        if(yuvwidth[i] == pic.iLineSize[i]) {
            memcpy(yuvdata[i], pic.data[i], size);
        } else {
            uint8_t *d = yuvdata[i], *s = pic.data[i];
            for(int y = 0; y < yuvheight[i]; ++y) {
                memcpy(d, s, yuvwidth[i]);
                d += yuvwidth[i];
                s += pic.iLineSize[i];
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf =
            m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
        picbuf.Clear();
        picbuf.width = width;
        picbuf.height = height;
        for(int i = 0; i < sizeof(yuvdata) / sizeof(yuvdata[0]); ++i) {
            picbuf.yuv[i] = yuvdata[i];
        }
        picbuf.pts = pic.pts / DVD_TIME_BASE;
        ++m_usedPicture;
        return MAX_BUFFER_COUNT - m_usedPicture;
    }

    // 	const static std::string sckey("present");
    // 	m_pRootNode->scheduleOnce(std::bind(&PlayerOverlay::PresentPicture,
    // this, std::placeholders::_1), 0, sckey);
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
        TVPHostClearVideoOverlayFrame();
        return;
    }

    tTVPRect dest = GetBounds();
    if(dest.get_width() <= 0 || dest.get_height() <= 0)
        dest = tTVPRect(0, 0, pic.width, pic.height);
    TVPHostSubmitVideoOverlayFrame(pic.rgba, pic.width, pic.height,
                                   pic.width * 4, dest.left, dest.top,
                                   dest.right, dest.bottom);
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
