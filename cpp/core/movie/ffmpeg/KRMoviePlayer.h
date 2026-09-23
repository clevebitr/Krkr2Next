#pragma once
#define NOMINMAX

#include "VideoPlayer.h"
#include "krmovie.h"
#include "ComplexRect.h"
#include "EventIntf.h"

#include <atomic>

struct SwsContext;

class iTVPSoundBuffer;

class TVPYUVSprite;

// Forward-declare a generic node pointer.
// These are opaque pointers wired up via the host rendering path.
using OverlayNode = void;

NS_KRMOVIE_BEGIN
#define MAX_BUFFER_COUNT 4

// ---------------------------------------------------------------------------
// 电影链路的低频统计（每 5 秒汇总一行）
//
// 为什么需要：真机上「视频帧率偏低」至少有四个互相独立的可能来源 ——
//   1) 解码跟不上（解码线程慢）；
//   2) YUV→RGBA 逐帧软件转换慢（每帧一次全画面）；
//   3) 呈现被引擎 tick 限制（每条链路每 tick 最多呈现一帧）；
//   4) 图层上传 / 宿主提交慢。
// 没有计数器只能靠猜。热路径上只做整数加法与两次 steady_clock::now()，
// 输出被限到 5 秒一行（与 engine 侧 frame_perf 同节奏，便于并排对照）。
// tag 用来区分链路："layer" = 画进 Layer 的经典路径（老游戏），
// "overlay" = 宿主纹理共享路径（overlay 模式）。
// ---------------------------------------------------------------------------
void TVPMovieStatsNoteDecode(const char *tag, uint64_t convertUs);
void TVPMovieStatsNotePresent(const char *tag, bool ptsNotYet);
// 开片时记一次片源与链路（三条 BuildGraph 都调）：声明见上面的统计说明。
// name 传 UTF-8，避免在本头里依赖 ttstr 的声明。
void TVPMovieLogOpened(class TVPMoviePlayer *player, const char *tag,
                       const char *name_utf8);

class TVPMoviePlayer : public iTVPVideoOverlay, public CBaseRenderer {
public:
    ~TVPMoviePlayer() override;

    void AddRef() override { RefCount++; }

    void Release() override;

    void SetVisible(bool b) override { Visible = b; }

    void Play() override {
        // 新一轮播放：清掉上一次停播/析构留下的中止标志。
        m_pictureWaitAbort.store(false, std::memory_order_release);
        m_pPlayer->Play();
    }

    void Stop() override {
        // 停播时唤醒卡在"等空槽位"的解码线程，别让它拖住 StopThread/析构。
        AbortPictureWait();
        m_pPlayer->Stop();
    }

    void Pause() override { m_pPlayer->Pause(); }

    void SetPosition(uint64_t tick) override;

    void GetPosition(uint64_t *tick) override;

    void GetStatus(tTVPVideoStatus *status) override;

    void Rewind() override;

    void SetFrame(int f) override;

    void GetFrame(int *f) override;

    void GetFPS(double *f) override;

    void GetNumberOfFrame(int *f) override;

    void GetTotalTime(int64_t *t) override;

    void GetVideoSize(long *width, long *height) override;

    void SetPlayRate(double rate) override;

    void GetPlayRate(double *rate) override;

    void SetAudioBalance(long balance) override;

    void GetAudioBalance(long *balance) override;

    void SetAudioVolume(long volume) override;

    void GetAudioVolume(long *volume) override;

    void GetNumberOfAudioStream(unsigned long *streamCount) override;

    void SelectAudioStream(unsigned long num) override;

    void GetEnableAudioStreamNum(long *num) override;

    void DisableAudioStream() override;

    void GetNumberOfVideoStream(unsigned long *streamCount) override;

    void SelectVideoStream(unsigned long num) override;

    void GetEnableVideoStreamNum(long *num) override;

    // TODO
    void SetStopFrame(int frame) override {}

    void GetStopFrame(int *frame) override {}

    void SetDefaultStopFrame() override {}

    // function for overlay mode
    void SetWindow(class tTJSNI_Window *window) override {}

    void SetMessageDrainWindow(void *window) override {}

    void SetRect(int l, int t, int r, int b) override {}

    // function for layer mode
    tTVPBaseTexture *GetFrontBuffer() override { return nullptr; }

    void SetVideoBuffer(tTVPBaseTexture *buff1, tTVPBaseTexture *buff2,
                        long size) override {}

    // function for mixer mode
    void SetMixingBitmap(class tTVPBaseTexture *dest, float alpha) override {}

    void ResetMixingBitmap() override {}

    void SetMixingMovieAlpha(float a) override {}

    void GetMixingMovieAlpha(float *a) override { *a = 1.0f; }

    void SetMixingMovieBGColor(unsigned long col) override {}

    void GetMixingMovieBGColor(unsigned long *col) override {
        *col = 0xFF000000;
    }

    void PresentVideoImage() override {}

    void GetContrastRangeMin(float *v) override {}

    void GetContrastRangeMax(float *v) override {}

    void GetContrastDefaultValue(float *v) override {}

    void GetContrastStepSize(float *v) override {}

    void GetContrast(float *v) override {}

    void SetContrast(float v) override {}

    void GetBrightnessRangeMin(float *v) override {}

    void GetBrightnessRangeMax(float *v) override {}

    void GetBrightnessDefaultValue(float *v) override {}

    void GetBrightnessStepSize(float *v) override {}

    void GetBrightness(float *v) override {}

    void SetBrightness(float v) override {}

    void GetHueRangeMin(float *v) override {}

    void GetHueRangeMax(float *v) override {}

    void GetHueDefaultValue(float *v) override {}

    void GetHueStepSize(float *v) override {}

    void GetHue(float *v) override {}

    void SetHue(float v) override {}

    void GetSaturationRangeMin(float *v) override {}

    void GetSaturationRangeMax(float *v) override {}

    void GetSaturationDefaultValue(float *v) override {}

    void GetSaturationStepSize(float *v) override {}

    void GetSaturation(float *v) override {}

    void SetSaturation(float v) override {}

    void SetLoopSegement(int beginFrame, int endFrame) override;

    int AddVideoPicture(DVDVideoPicture &pic, int index) override;

    int WaitForBuffer(volatile std::atomic_bool &bStop,
                      int timeout = 0) override;

    /**
     * 有界地拿 picture 锁。拿不到（或期间收到停播/中止）返回 false，调用方按
     * "没有可用缓冲"处理。
     *
     * 为什么不能用无界的 `unique_lock lk(m_mtxPicture)`：解码线程一旦卡在这把锁上，
     * 停播路径的 `StopThread()`（join 它）就永远回不来，级联到渲染线程就是整帧卡死
     * （真机 2026-09-23 10:55 的 `.stall`：video 停在 `video→等 render 缓冲`、
     * player 停在 `video CloseStream→StopThread(join 视频线程)`）。
     */
    bool LockPictureBounded(std::unique_lock<std::mutex> &lk, int timeoutMs);

    void Flush() override;

    bool IsPlaying() const { return m_pPlayer->IsPlaying(); }

    void FrameMove();

protected:
    TVPMoviePlayer();

    iTVPSoundBuffer *GetSoundDevice();

    uint32_t RefCount = 1;
    bool Visible = false;

    BasePlayer *m_pPlayer = nullptr;

    struct BitmapPicture {
        ERenderFormat fmt;
        union {
            uint8_t *data[4];
            uint8_t *rgba;
            uint8_t *yuv[3];
        };
        int width = 0; // pitch = width * 4
        int height = 0;
        double pts;

        BitmapPicture() {
            fmt = RENDER_FMT_NONE;
            for(int i = 0; i < sizeof(data) / sizeof(data[0]); ++i)
                data[i] = nullptr;
        }

        BitmapPicture(const BitmapPicture &) = delete;
        BitmapPicture &operator=(const BitmapPicture &) = delete;

        ~BitmapPicture() { Clear(); }

        void MoveFrom(BitmapPicture &source);

        void swap(BitmapPicture &r);

        void Clear();
    };

    BitmapPicture m_picture[MAX_BUFFER_COUNT];
    int m_curPicture = 0, m_usedPicture = 0;
    std::mutex m_mtxPicture;
    std::condition_variable m_condPicture;
    struct SwsContext *img_convert_ctx = nullptr;
    double m_curpts = 0;

    /**
     * 唤醒并放弃"等一个空 picture 槽位"的等待（解码线程）。
     *
     * 为什么必须有：解码线程在 `AddVideoPicture` 里等空槽位，消费者是**渲染线程**
     * 每帧调用的 `GetFrontBuffer()`。一旦游戏停止消费（片段播完/切换中），队列填满
     * 后解码线程就永久卡在条件变量里；而停播/销毁路径要走
     * `CVideoPlayerVideo::CloseStream` + `StopThread()` 去 join 这条线程 ——
     * 于是渲染线程被无限期挂住（真机实测：播 CG 视频时整机无响应，13 秒后被系统
     * ANR 杀掉）。这里给等待加上"可被中止"的出口，停播/析构时唤醒它。
     */
    std::atomic<bool> m_pictureWaitAbort{ false };

    /** 置中止标志并唤醒等待中的解码线程（幂等）。 */
    void AbortPictureWait() {
        m_pictureWaitAbort.store(true, std::memory_order_release);
        m_condPicture.notify_all();
    }
};

// 视频呈现 overlay（overlay 模式电影）：连续事件钩子驱动呈现，帧经宿主纹理
// 共享叠画在场景之上。
class VideoPresentOverlay : public TVPMoviePlayer,
                            public tTVPContinuousEventCallbackIntf {
protected:
    OverlayNode *m_pRootNode = nullptr;
    TVPYUVSprite *m_pSprite = nullptr;

    ~VideoPresentOverlay() override;

    void ClearNode();

public:
    void PresentPicture(float dt);

    void OnContinuousCallback(tjs_uint64 tick) override;

    void Stop() override;

    void Play() override;

protected:
    virtual const tTVPRect &GetBounds() = 0;
};

class MoviePlayerOverlay : public VideoPresentOverlay {
    tTJSNI_VideoOverlay *m_pCallbackWin = nullptr;
    tTJSNI_Window *m_pOwnerWindow = nullptr;

    void OnPlayEvent(KRMovieEvent msg, void *p);

public:
    ~MoviePlayerOverlay() override;

    void SetWindow(class tTJSNI_Window *window) override;

    void BuildGraph(tTJSNI_VideoOverlay *callbackwin, IStream *stream,
                    const tjs_char *streamname, const tjs_char *type,
                    uint64_t size);

    const tTVPRect &GetBounds() override;

    void SetVisible(bool b) override;
};

class VideoPresentOverlay2 : public VideoPresentOverlay {
    std::function<const tTVPRect &()> m_funcGetBounds;

public:
    const tTVPRect &GetBounds() override { return m_funcGetBounds(); }

    void SetFuncGetBounds(const std::function<const tTVPRect &()> &func) {
        m_funcGetBounds = func;
    }

    BasePlayer *GetPlayer() { return m_pPlayer; }

    void SetRootNode(OverlayNode *node);

    OverlayNode *GetRootNode() { return m_pRootNode; }

    static VideoPresentOverlay2 *create();
};

NS_KRMOVIE_END
