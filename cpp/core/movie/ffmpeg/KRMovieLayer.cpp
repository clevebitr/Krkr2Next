#include "KRMovieLayer.h"
#include "VideoCodec.h"
#include "LayerBitmapIntf.h"
#include "Application.h"
#include "VideoOvlImpl.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <spdlog/spdlog.h>

extern "C" {
#include "libswscale/swscale.h"
}

NS_KRMOVIE_BEGIN

//---------------------------------------------------------------------------
// 边沿诊断
//
// 真机症状是"影片有声音、画面停在 KAG 的占位图上"。这条链路上有三个可能断掉
// 的环节，而它们在外观上完全一样：
//   A. 解码帧没进缓冲   -> AddVideoPicture 从未成功入队
//   B. 有帧但没人来取   -> GetFrontBuffer 从未被调用（EC_UPDATE 没发生）
//   C. 取了但图层没更新 -> WndProc 的 EC_UPDATE 分支被前置条件挡掉
// 每部影片各打一条"第一次"日志就能区分，且只在状态翻转时写，不会刷屏。
// 直接用 spdlog 的格式化参数，不引入 fmt::format 依赖。
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// sws_scale 兜底
//
// 移植自 AetherKiri（同工作区、实测能正常播放本作的运行时）：它在同一处加了
// 这个手写 BT.601 转换，因为 sws_scale 在某些 Android ABI 上返回 <= 0 却不报
// 错，于是缓冲里躺着一片未初始化内存，画面上什么都看不出来。只在 sws_scale
// 明确失败时走这里，正常路径仍是 swscale。
//---------------------------------------------------------------------------
static inline uint8_t ClampByte(int value) {
    if(value < 0)
        return 0;
    if(value > 255)
        return 255;
    return static_cast<uint8_t>(value);
}

static void ConvertYuv420ToRgba(const DVDVideoPicture &pic, uint8_t *dst,
                                int dstWidth, int dstHeight, int dstStride) {
    const int copyWidth = std::min<int>(dstWidth, pic.iWidth);
    const int copyHeight = std::min<int>(dstHeight, pic.iHeight);
    for(int y = 0; y < copyHeight; ++y) {
        const uint8_t *yRow = pic.data[0] + (size_t)y * pic.iLineSize[0];
        const uint8_t *uRow = pic.data[1] + (size_t)(y / 2) * pic.iLineSize[1];
        const uint8_t *vRow = pic.data[2] + (size_t)(y / 2) * pic.iLineSize[2];
        uint8_t *out = dst + (size_t)y * dstStride;
        for(int x = 0; x < copyWidth; ++x) {
            int c = (int)yRow[x] - 16;
            int d = (int)uRow[x / 2] - 128;
            int e = (int)vRow[x / 2] - 128;
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

VideoPresentLayer::~VideoPresentLayer() { ClearVideoBuffer(); }

void VideoPresentLayer::EnsureContinuousHook() {
    if(m_continuousHookRegistered)
        return;
    TVPAddContinuousEventHook(this);
    m_continuousHookRegistered = true;
}

tTVPBaseTexture *VideoPresentLayer::GetFrontBuffer() {
    BitmapPicture pic;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        if(!m_usedPicture) {
            return nullptr;
        }
        BitmapPicture &picbuf = m_picture[m_curPicture];
        picbuf.swap(pic);
        m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
        --m_usedPicture;
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    if(!pic.data[0] || pic.width <= 0 || pic.height <= 0) {
        pic.Clear();
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> videoLock(m_mtxVideoBuffer);
        if(!m_videoBufferActive || !m_BmpBits[0] || !m_BmpBits[1]) {
            pic.Clear();
            return nullptr;
        }
        if(!m_loggedFirstPresent) {
            m_loggedFirstPresent = true;
            spdlog::info(
                "MovieVideo: first frame handed to layer {}x{} pts={:.3f}",
                pic.width, pic.height, pic.pts);
        }
        int n = m_nCurBmpBuff;
        m_nCurBmpBuff = !m_nCurBmpBuff;
        m_BmpBits[n]->Update(pic.data[0], pic.width * 4, 0, 0, pic.width,
                             pic.height);
        pic.Clear();
        return m_BmpBits[n];
    }
}

void VideoPresentLayer::SetVideoBuffer(tTVPBaseTexture *buff1,
                                       tTVPBaseTexture *buff2, long size) {
    int width = 0;
    int height = 0;
    if(buff1) {
        width = (int)buff1->GetWidth();
        height = (int)buff1->GetHeight();
    }
    {
        std::lock_guard<std::mutex> videoLock(m_mtxVideoBuffer);
        m_BmpBits[0] = buff1;
        m_BmpBits[1] = buff2;
        m_nCurBmpBuff = 0;
        m_videoBufferWidth = width;
        m_videoBufferHeight = height;
        m_videoBufferActive = buff1 && buff2 && width > 0 && height > 0;
        m_loggedFirstQueued = false;
        m_loggedFirstPresent = false;
    }
    // 钩子挂在这里，而不是只挂在 Play()：脚本完全可能先 Play 再（或干脆不）
    // 走到这里，而"没有每帧钩子"的表现正好就是画面永远停在占位图上。
    if(m_videoBufferActive)
        EnsureContinuousHook();
}

void VideoPresentLayer::ClearVideoBuffer() {
    if(m_continuousHookRegistered) {
        TVPRemoveContinuousEventHook(this);
        m_continuousHookRegistered = false;
    }
    std::lock_guard<std::mutex> videoLock(m_mtxVideoBuffer);
    m_videoBufferActive = false;
    m_BmpBits[0] = nullptr;
    m_BmpBits[1] = nullptr;
    m_nCurBmpBuff = 0;
    m_videoBufferWidth = 0;
    m_videoBufferHeight = 0;
    m_condPicture.notify_all();
}

void VideoPresentLayer::OnContinuousCallback(tjs_uint64 tick) {
    if(!m_pPlayer)
        return;
    double curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        if(!m_usedPicture)
            return;
        BitmapPicture &picbuf = m_picture[m_curPicture];
        // check pts
        if(picbuf.pts > curpts) { // present in future
            return;
        }
    }
    OnPlayEvent(KRMovieEvent::Update, nullptr);
}

int VideoPresentLayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
    // from other thread
    int width = 0;
    int height = 0;
    {
        std::lock_guard<std::mutex> videoLock(m_mtxVideoBuffer);
        if(!m_videoBufferActive || !m_BmpBits[0] || !m_BmpBits[1])
            return -1;
        width = m_videoBufferWidth;
        height = m_videoBufferHeight;
    }
    if(width <= 0 || height <= 0)
        return -1;

    if(pic.format != RENDER_FMT_YUV420P)
        return -2;
    if(pic.pts == DVD_NOPTS_VALUE)
        return 0;

    int srcWidth = pic.iWidth;
    int srcHeight = pic.iHeight;
    if(srcWidth <= 0 || srcHeight <= 0 || !pic.data[0] || !pic.data[1] ||
       !pic.data[2])
        return -1;

    {
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        if(m_usedPicture >= MAX_BUFFER_COUNT) {
            // 有界等待：原来是无谓词、无超时的 wait()，一旦没人来取帧
            // （GetFrontBuffer 不被调用），解码线程就在这里永久挂住，连结束
            // 事件都发不出来。宁可丢帧也不能锁死。
            m_condPicture.wait_for(lk, std::chrono::milliseconds(10), [this]() {
                return m_usedPicture < MAX_BUFFER_COUNT;
            });
        }
        if(m_usedPicture >= MAX_BUFFER_COUNT)
            return -1;
    }

    uint8_t *data = (uint8_t *)TJSAlignedAlloc(width * height * 4, 4);
    if(!data)
        return -1;
    uint8_t *dstData[4] = { data, nullptr, nullptr, nullptr };
    int dstLineSize[4] = { width * 4, 0, 0, 0 };

    img_convert_ctx = sws_getCachedContext(
        img_convert_ctx, srcWidth, srcHeight, AV_PIX_FMT_YUV420P, width, height,
        AV_PIX_FMT_RGBA, /*sws_flags*/ SWS_FAST_BILINEAR, nullptr, nullptr,
        nullptr);
    int processed = 0;
    if(img_convert_ctx) {
        processed = sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0,
                              srcHeight, dstData, dstLineSize);
    }
    if(processed <= 0) {
        // swscale 失败不能留未初始化内存进缓冲，那会让图层显示成噪声。
        std::memset(data, 0, (size_t)width * height * 4);
        ConvertYuv420ToRgba(pic, data, width, height, dstLineSize[0]);
    }

    {
        std::lock_guard<std::mutex> videoLock(m_mtxVideoBuffer);
        if(!m_videoBufferActive || !m_BmpBits[0] || !m_BmpBits[1]) {
            TJSAlignedDealloc(data);
            return -1;
        }
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        if(m_usedPicture >= MAX_BUFFER_COUNT) {
            TJSAlignedDealloc(data);
            return -1;
        }
        BitmapPicture &picbuf =
            m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
        picbuf.Clear();
        picbuf.width = width;
        picbuf.height = height;
        picbuf.data[0] = data;
        picbuf.pts = pic.pts / DVD_TIME_BASE;
        ++m_usedPicture;
        if(!m_loggedFirstQueued) {
            m_loggedFirstQueued = true;
            spdlog::info(
                "MovieVideo: first decoded frame queued {}x{} pts={:.3f}",
                width, height, picbuf.pts);
        }
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
}

void MoviePlayerLayer::OnPlayEvent(KRMovieEvent msg, void *p) {
    if(msg == KRMovieEvent::Update) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_UPDATE;
        int frame;
        GetFrame(&frame);
        ev.LParam = frame;
        // 与原实现（同 krkr2）的差别：从同步直调 WndProc 改成投递。
        // 直调时 EC_UPDATE 的处理（AssignMainImage / 图层 Update /
        // FireFrameUpdateEvent 的 TJS 事件）全部跑在
        // _TVPDeliverContinuousEvent 的内层，任何异常都会被
        // TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION("continuous event") 吃掉并中断本
        // 帧剩下的连续回调——症状就是"影片在播、画面不动、日志里没有明显错误"。
        // 取 AetherKiri（实测可正常播放本作）的同名实现，投递到消息队列后在
        // 主循环里处理。
        m_pCallbackWin->PostEvent(ev);
    } else if(msg == KRMovieEvent::Ended) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

void MoviePlayerLayer::Play() {
    inherit::Play();
    EnsureContinuousHook();
}

NS_KRMOVIE_END
