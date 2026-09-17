#include "KRMovieLayer.h"
#include "VideoCodec.h"
#include "LayerBitmapIntf.h"
#include "Application.h"
#include "VideoOvlImpl.h"

#include <chrono>

#include <spdlog/spdlog.h>

extern "C" {
#include "libswscale/swscale.h"
}

NS_KRMOVIE_BEGIN

VideoPresentLayer::~VideoPresentLayer() { TVPRemoveContinuousEventHook(this); }

tTVPBaseTexture *VideoPresentLayer::GetFrontBuffer() {
    BitmapPicture pic;
    if(!m_usedPicture) {
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        picbuf.swap(pic);
        m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
        --m_usedPicture;
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    int n = m_nCurBmpBuff;
    m_nCurBmpBuff = !m_nCurBmpBuff;
    m_BmpBits[n]->Update(pic.data[0], pic.width * 4, 0, 0, pic.width,
                         pic.height);
    return m_BmpBits[n];
}

void VideoPresentLayer::SetVideoBuffer(tTVPBaseTexture *buff1,
                                       tTVPBaseTexture *buff2, long size) {
    m_BmpBits[0] = buff1;
    m_BmpBits[1] = buff2;
    m_nCurBmpBuff = 0;
    //	TVPAddContinuousEventHook(this);
}

void VideoPresentLayer::OnContinuousCallback(tjs_uint64 tick) {
    if(!m_usedPicture)
        return;
    double m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        // check pts
        if(picbuf.pts > m_curpts) { // present in future
            // 这条链路每 tick 最多呈现一帧，"跳过"次数偏高就说明呈现被引擎
            // tick 卡住（而不是解码慢）—— 统计里必须区分开。
            TVPMovieStatsNotePresent("layer", /*ptsNotYet=*/true);
            return;
        }
    }
#if 0
        do { // skip frame
            pic.Clear();
            picbuf.swap(pic);
            m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
            --m_usedPicture;
        } while (m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
        assert(m_usedPicture >= 0);
#endif
    OnPlayEvent(KRMovieEvent::Update, nullptr);
    TVPMovieStatsNotePresent("layer", /*ptsNotYet=*/false);
}

int VideoPresentLayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
    // from other thread
    if(pic.format != RENDER_FMT_YUV420P)
        return -2;
    if(pic.pts == DVD_NOPTS_VALUE)
        return 0;

    if(m_usedPicture >= MAX_BUFFER_COUNT) {
        // 与 overlay 链路同样的有界/可中止等待：消费者是渲染线程每帧的
        // GetFrontBuffer()，无界等待会把解码线程永久钉住，进而让停播/析构路径的
        // StopThread() 挂死渲染线程（真机：播 CG 视频时无响应并被 ANR）。
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        while(m_usedPicture >= MAX_BUFFER_COUNT &&
              !m_pictureWaitAbort.load(std::memory_order_acquire)) {
            m_condPicture.wait_for(lk, std::chrono::milliseconds(50));
        }
        if(m_pictureWaitAbort.load(std::memory_order_acquire))
            return -1;
    }
    if(m_usedPicture >= MAX_BUFFER_COUNT)
        return -1;

    int width = pic.iWidth, height = pic.iHeight;

    uint8_t *data = (uint8_t *)TJSAlignedAlloc(width * height * 4, 4);
    int datasize = width * 4;

    // 经典 layer 链路：每帧一次全画面 YUV→RGBA 软件转换，外加一次
    // width*height*4 的堆分配（1080p 就是每帧 8MB 的分配/释放 churn）。
    // 单独计时上报，才能判断"帧率低"是不是转换（含分配）造成的。
    const auto convertStart = std::chrono::steady_clock::now();
    img_convert_ctx = sws_getCachedContext(
        img_convert_ctx, width, height, AV_PIX_FMT_YUV420P, width, height,
        AV_PIX_FMT_RGBA, /*sws_flags*/ SWS_FAST_BILINEAR, nullptr, nullptr,
        nullptr);
    assert(img_convert_ctx);
    int processed = sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0,
                              pic.iHeight, &data, &datasize);
    TVPMovieStatsNoteDecode(
        "layer",
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
        picbuf.data[0] = data;
        picbuf.pts = pic.pts / DVD_TIME_BASE;
        ++m_usedPicture;
    }

    return MAX_BUFFER_COUNT - m_usedPicture;
}

void MoviePlayerLayer::BuildGraph(tTJSNI_VideoOverlay *callbackwin,
                                  IStream *stream, const tjs_char *streamname,
                                  const tjs_char *type, uint64_t size) {
    m_pCallbackWin = callbackwin;
    m_pPlayer->SetCallback([this](auto &&PH1, auto &&PH2) {
        OnPlayEvent(std::forward<decltype(PH1)>(PH1),
                    std::forward<decltype(PH2)>(PH2));
    });
    m_pPlayer->OpenFromStream(stream, streamname, type, size);
    // 开片时记一次片源自身参数 + 链路标签：视频声明的帧率与尺寸是判断"卡"的基准线
    // （例如片源 60fps 而引擎只跑 30fps，那就不是解码问题而是呈现节流）；
    // 有了这一行才能在日志里区分"这条电影根本没开"与"开了但没上屏"。
    const std::string srcName =
        streamname ? ttstr(streamname).AsStdString() : std::string();
    TVPMovieLogOpened(this, "layer", srcName.c_str());
}

void MoviePlayerLayer::OnPlayEvent(KRMovieEvent msg, void *p) {
    if(msg == KRMovieEvent::Update) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_UPDATE;
        int frame;
        GetFrame(&frame);
        ev.LParam = frame;
        m_pCallbackWin->WndProc(ev); // in the same thread
    } else if(msg == KRMovieEvent::Ended) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

void MoviePlayerLayer::Play() {
    inherit::Play();
    TVPAddContinuousEventHook(this);
}

NS_KRMOVIE_END
