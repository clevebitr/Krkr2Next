#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tjs/tjs.h"
#include "tjs/tjsNative.h"
#include "visual/Scene.h"

namespace krkr::script {
  namespace detail {

    // Window 类的 ClassID。Layer 构造时用它把脚本对象解析成窗口原生实例。
    tjs_uint32 WindowClassID();
    // Layer 类的 ClassID。Window 侧解析父层与 Window.add 的实参时使用。
    tjs_uint32 LayerClassID();

    // 窗口存活标记。窗口失效（GC）时先置 instance 为空，图层侧因此不会
    // 通过悬空指针回写窗口实例。
    struct WindowHandle {
      struct WindowNativeInstance *instance = nullptr;
    };

    class WindowNativeInstance final : public tTJSNativeInstance {
      public:
        tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                            iTJSDispatch2 *tjs_obj) override;
        void TJS_INTF_METHOD Invalidate() override;

        std::shared_ptr<visual::Scene> scene;
        std::shared_ptr<WindowHandle> handle;
        // 主图层对象：由 Layer 构造/失效维护。与参照实现的 Owner 同语义，不持有引用。
        iTJSDispatch2 *primaryLayer = nullptr;
        // window.add(obj) 只钉住对象、不让它被回收（参照实现的 ObjectVector 语义）。
        std::vector<tTJSVariant> pinned;
        bool visible = true;
        int left = 0;
        int top = 0;
        int zoomNumer = 1;
        int zoomDenom = 1;
        std::string caption;
    };

    class LayerNativeInstance final : public tTJSNativeInstance {
      public:
        tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                            iTJSDispatch2 *tjs_obj) override;
        void TJS_INTF_METHOD Invalidate() override;

        std::shared_ptr<visual::SceneNode> node;
        std::weak_ptr<WindowHandle> window;
        iTJSDispatch2 *ownerObject = nullptr;  // 自身 TJS 对象，不持有引用
        tTJSVariant windowObject;             // layer.window 的返回值（持有引用）
        tTJSVariant parentObject;             // layer.parent 的返回值（持有引用）
    };

    // 把脚本值解析成窗口原生实例；不是窗口对象时抛带上下文的异常。
    WindowNativeInstance *ResolveWindow(const tTJSVariant &value, const std::string &context);
    // 同上，解析图层；未激活的图层视为错误。
    LayerNativeInstance *ResolveLayer(const tTJSVariant &value, const std::string &context);

    // 脚本字符串 → UTF-8。
    std::string Utf8Text(const tTJSVariant &value);

  } // namespace detail
} // namespace krkr::script
