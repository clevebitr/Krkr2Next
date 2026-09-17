/**
 * @file alphamovie.cpp
 * @brief Minimal AlphaMovie stub plugin for KiriKiri2.
 *
 * Provides enough of the AlphaMovie interface so that games which
 * reference AlphaMovie.dll do not crash.  Actual video playback is
 * not implemented — calls are silently ignored.
 *
 * ⚠️ 这也解释了ゆずソフト 系"动画 CG 有声音没画面 / 只闪一两帧"：
 * 游戏每帧靠 `showNextImage(layer)` 把 αMovie 的解码帧画进图层，而本 stub 什么都不
 * 画 —— 图层内容停在游戏先画的那一帧上，随后被清屏/换层吃掉，表现就是
 * "CG 只显示一两帧就消失、只剩 UI"。AetherKiri 有完整实现
 * （`cpp/plugins/alphamovie.cpp` + `cpp/core/visual/LoadAMV.cpp`），需要时按它移植。
 *
 * 下面这些探针只做计数与限频日志：αMovie 是一条**独立交付链路**，
 * motionplayer / krmovie 的探针完全看不到它。没有这几行，就无法把
 * "游戏根本没用 αMovie" 与 "用了但 stub 不画" 区分开。
 */

#include "ncbind.hpp"

#include <atomic>
#include <string>
#include <spdlog/spdlog.h>

#define NCB_MODULE_NAME TJS_W("AlphaMovie.dll")

namespace {

// 计数 + 限频：返回 0 = 本次不打日志，否则返回本次的序号（从 1 开始）。
// 逐帧调用点传 every=300（前 3 次 + 每 300 次一条心跳），
// 每次开片一次的入口传 every=0（只记前 3 次）。
uint64_t AmvStubTake(std::atomic<uint64_t> &counter, uint64_t every) {
    const uint64_t n = counter.fetch_add(1) + 1;
    if(n <= 3 || (every != 0 && (n % every) == 0))
        return n;
    return 0;
}

std::string AmvArgText(const tTJSVariant &v) {
    switch(v.Type()) {
        case tvtString:
            return ttstr(v).AsStdString();
        case tvtInteger:
            return ttstr((tjs_int)v).AsStdString();
        case tvtObject:
            return "<object>";
        case tvtVoid:
            return "<void>";
        default:
            return "<other>";
    }
}

} // namespace

class AlphaMovie {
public:
    AlphaMovie() {}
    virtual ~AlphaMovie() {}

    void open(tTJSVariant v) {
        static std::atomic<uint64_t> s_calls{ 0 };
        if(const uint64_t n = AmvStubTake(s_calls, 0))
            spdlog::info("alphamovie[stub]: open({}) 第 {} 次 —— 不打开、不解码",
                         AmvArgText(v), n);
    }
    void play() {
        static std::atomic<uint64_t> s_calls{ 0 };
        if(const uint64_t n = AmvStubTake(s_calls, 0))
            spdlog::info("alphamovie[stub]: play 第 {} 次（不播放）", n);
    }
    void stop() {}
    void pause() {}
    void close() {}
    void rewind() {}

    bool get_loop() const { return m_loop; }
    void set_loop(bool v) { m_loop = v; }

    bool get_visible() const { return m_visible; }
    void set_visible(bool v) { m_visible = v; }

    int get_frame() const { return 0; }
    void set_frame(int) {}

    double get_fps() const { return 30.0; }
    void set_fps(double) {}

    int get_position() const { return 0; }
    void set_position(int) {}

    int get_width() const { return 0; }
    int get_height() const { return 0; }

    bool get_opened() const {
        static std::atomic<uint64_t> s_calls{ 0 };
        if(const uint64_t n = AmvStubTake(s_calls, 0))
            spdlog::info("alphamovie[stub]: opened 被查询第 {} 次 -> false"
                         "（游戏据此判断 αMovie 可用性）",
                         n);
        return false;
    }
    bool get_isPlaying() const { return false; }

    int get_totalTime() const { return 0; }
    int get_numberOfFrame() const { return 0; }
    int get_numOfFrame() const { return 0; }

    int get_FPSRate() const { return 30; }
    int get_FPSScale() const { return 1; }

    int get_screenWidth() const { return 1280; }
    int get_screenHeight() const { return 720; }

    int showNextImage(iTJSDispatch2 *) {
        // 游戏**每帧**调它推进 αMovie 画面；stub 不绘制。这条心跳同时回答
        // "动画 CG 是否走 αMovie" 与 "这条链路还在被驱动吗"。
        static std::atomic<uint64_t> s_calls{ 0 };
        if(const uint64_t n = AmvStubTake(s_calls, 300))
            spdlog::info("alphamovie[stub]: showNextImage 第 {} 次（不绘制任何帧）",
                         n);
        return 0;
    }

private:
    bool m_loop = false;
    bool m_visible = false;
};

NCB_REGISTER_CLASS(AlphaMovie) {
    Constructor();

    NCB_METHOD(open);
    NCB_METHOD(play);
    NCB_METHOD(stop);
    NCB_METHOD(pause);
    NCB_METHOD(close);
    NCB_METHOD(rewind);

    NCB_PROPERTY(loop, get_loop, set_loop);
    NCB_PROPERTY(visible, get_visible, set_visible);
    NCB_PROPERTY(frame, get_frame, set_frame);
    NCB_PROPERTY(fps, get_fps, set_fps);
    NCB_PROPERTY(position, get_position, set_position);
    NCB_PROPERTY_RO(width, get_width);
    NCB_PROPERTY_RO(height, get_height);
    NCB_PROPERTY_RO(opened, get_opened);
    NCB_PROPERTY_RO(isPlaying, get_isPlaying);
    NCB_PROPERTY_RO(totalTime, get_totalTime);
    NCB_PROPERTY_RO(numberOfFrame, get_numberOfFrame);
    NCB_PROPERTY_RO(numOfFrame, get_numOfFrame);
    NCB_PROPERTY_RO(FPSRate, get_FPSRate);
    NCB_PROPERTY_RO(FPSScale, get_FPSScale);
    NCB_PROPERTY_RO(screenWidth, get_screenWidth);
    NCB_PROPERTY_RO(screenHeight, get_screenHeight);
    NCB_METHOD(showNextImage);
};
