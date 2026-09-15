#pragma once

#include "KRMoviePlayer.h"
#include "EventIntf.h"

#include <mutex>

NS_KRMOVIE_BEGIN

class VideoPresentLayer : public TVPMoviePlayer,
                          public tTVPContinuousEventCallbackIntf {
protected:
    tTVPBaseTexture *m_BmpBits[2]{};
    int m_nCurBmpBuff = 0;

    // ── 视频缓冲的生命周期 ────────────────────────────────────────────
    // 缓冲由宿主（VideoOvlImpl 的 layer 模式 / layerExMovie）用
    // SetVideoBuffer 交付，并在 Close() 时释放；解码线程却会在任意时刻从
    // AddVideoPicture 进来写帧。裸读 m_BmpBits 与尺寸就是"解码线程往已释放
    // 的纹理里写"。状态一律加锁读，失效时 return nullptr 让上层保持上一帧，
    // 而不是拿一个空纹理去顶替图层内容。
    std::mutex m_mtxVideoBuffer;
    int m_videoBufferWidth = 0;
    int m_videoBufferHeight = 0;
    bool m_videoBufferActive = false;
    bool m_continuousHookRegistered = false;

    // 每部影片只打一次的边沿诊断（见 KRMovieLayer.cpp 的 MovieVideo 日志）。
    bool m_loggedFirstQueued = false;
    bool m_loggedFirstPresent = false;

    /** 幂等注册每帧钩子；重复注册会让 OnContinuousCallback 每帧被调两次。 */
    void EnsureContinuousHook();

public:
    ~VideoPresentLayer() override;

    tTVPBaseTexture *GetFrontBuffer() override;

    void SetVideoBuffer(tTVPBaseTexture *buff1, tTVPBaseTexture *buff2,
                        long size) override;

    /** 解绑视频缓冲并摘掉每帧钩子。缓冲即将失效时必须先调。 */
    void ClearVideoBuffer();

    void OnContinuousCallback(tjs_uint64 tick) override;

    virtual void OnPlayEvent(KRMovieEvent msg, void *p) = 0;

    int AddVideoPicture(DVDVideoPicture &pic, int index) override;
};

class MoviePlayerLayer : public VideoPresentLayer {
    typedef VideoPresentLayer inherit;
    tTJSNI_VideoOverlay *m_pCallbackWin = nullptr;

public:
    void BuildGraph(tTJSNI_VideoOverlay *callbackwin, IStream *stream,
                    const tjs_char *streamname, const tjs_char *type,
                    uint64_t size);

    void OnPlayEvent(KRMovieEvent msg, void *p) override;

    void Play() override;
};

NS_KRMOVIE_END
