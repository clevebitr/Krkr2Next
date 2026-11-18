#include "shim/classes/TjsWindow.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/contracts/ScriptServices.h"
#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/TjsDiagnostics.h"
#include "shim/classes/TjsWindowInternals.h"
#include "shim/engine/NativeClass.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

// 窗口登记表：本会话创建过的窗口场景。参照实现的窗口同样活到进程结束，
// 因此这里持有 shared_ptr；运行时重启时由 ResetWindowRegistry 清空。
std::vector<std::shared_ptr<krkr::visual::Scene>> &WindowScenes() {
  static std::vector<std::shared_ptr<krkr::visual::Scene>> scenes;
  return scenes;
}

krkr::script::IWindowInfo *HostWindow() { return GetContext().services.window; }

void SetSceneSize(detail::WindowNativeInstance &window, int width, int height,
                  const char *context) {
  if (window.scene == nullptr || !window.scene->SetSize(width, height)) {
    ThrowScriptException(context, "invalid window size " + std::to_string(width) + "x" +
                                      std::to_string(height));
  }
}

class WindowClass final : public TJS::tTJSNativeClass {
  public:
    static tjs_uint32 ClassID;

    WindowClass() : tTJSNativeClass(TJS_W("Window")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Window)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var. name*/ _this,
                                        /*var. type*/ detail::WindowNativeInstance,
                                        /*TJS class name*/ Window) {
        return TJS_S_OK;
      }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Window)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ add) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        TJS::iTJSDispatch2 *object = param[0]->AsObjectClosureNoAddRef().Object;
        if (object == nullptr) {
          ThrowScriptException("Window.add", "the argument is not an object");
        }
        for (const tTJSVariant &pinned : _this->pinned) {
          if (pinned.AsObjectClosureNoAddRef().Object == object) {
            return TJS_S_OK;
          }
        }
        _this->pinned.push_back(*param[0]);
        krkr::log::Debug("Window.add: object pinned (", _this->pinned.size(), " total)");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ add)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ remove) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        TJS::iTJSDispatch2 *object = param[0]->AsObjectClosureNoAddRef().Object;
        for (std::size_t i = 0; i < _this->pinned.size(); ++i) {
          if (_this->pinned[i].AsObjectClosureNoAddRef().Object == object) {
            _this->pinned.erase(_this->pinned.begin() + static_cast<std::ptrdiff_t>(i));
            krkr::log::Debug("Window.remove: object released (", _this->pinned.size(),
                             " left)");
            break;
          }
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ remove)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSize) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        SetSceneSize(*_this, static_cast<tjs_int>(*param[0]), static_cast<tjs_int>(*param[1]),
                     "Window.setSize");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSize)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setInnerSize) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        // 本引擎没有窗口装饰，客户区与外框同尺寸。
        SetSceneSize(*_this, static_cast<tjs_int>(*param[0]), static_cast<tjs_int>(*param[1]),
                     "Window.setInnerSize");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setInnerSize)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPos) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        _this->left = static_cast<tjs_int>(*param[0]);
        _this->top = static_cast<tjs_int>(*param[1]);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPos)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setLayerPos) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        // 参照实现里 layerLeft/layerTop 是图层区在窗口内的偏移。本引擎的窗口
        // 就是合成表面，没有非客户区，因此只记录不改变几何。
        krkr::log::Debug("Window.setLayerPos(", static_cast<tjs_int>(*param[0]), ", ",
                         static_cast<tjs_int>(*param[1]), ") recorded only");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setLayerPos)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ update) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        // 呈现由宿主 tick 驱动；脚本的 update 只表达"这一帧有变化"。
        krkr::log::Debug("Window.update: presentation is driven by the host tick");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ update)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ bringToFront) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        krkr::log::Warn("Window.bringToFront: multiple windows are not supported yet");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ bringToFront)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ close) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
        krkr::log::Info("Window.close: requesting engine exit");
        IAppControl *appControl = GetContext().services.appControl;
        if (appControl == nullptr) {
          ThrowScriptException("Window.close", "host did not provide app control");
        }
        appControl->RequestExit();
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ close)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ visible)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->visible ? 1 : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          _this->visible = static_cast<bool>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ visible)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ caption)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = WideFromUtf8(_this->caption).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          _this->caption = detail::Utf8Text(*param);
          if (IWindowInfo *window = HostWindow(); window != nullptr) {
            window->SetTitle(_this->caption);
          } else {
            krkr::log::Warn("Window.caption: host did not provide window info");
          }
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ caption)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ width)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->scene != nullptr ? _this->scene->Width() : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          SetSceneSize(*_this, static_cast<tjs_int>(*param),
                       _this->scene != nullptr ? _this->scene->Height() : 0, "Window.width");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ width)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ height)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->scene != nullptr ? _this->scene->Height() : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          SetSceneSize(*_this, _this->scene != nullptr ? _this->scene->Width() : 0,
                       static_cast<tjs_int>(*param), "Window.height");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ height)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ innerWidth)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->scene != nullptr ? _this->scene->Width() : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          SetSceneSize(*_this, static_cast<tjs_int>(*param),
                       _this->scene != nullptr ? _this->scene->Height() : 0,
                       "Window.innerWidth");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ innerWidth)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ innerHeight)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->scene != nullptr ? _this->scene->Height() : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          SetSceneSize(*_this, _this->scene != nullptr ? _this->scene->Width() : 0,
                       static_cast<tjs_int>(*param), "Window.innerHeight");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ innerHeight)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ left)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->left;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          _this->left = static_cast<tjs_int>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ left)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ top)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->top;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          _this->top = static_cast<tjs_int>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ top)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ zoomNumer)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->zoomNumer;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          const tjs_int value = static_cast<tjs_int>(*param);
          _this->zoomNumer = value > 0 ? value : 1;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ zoomNumer)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ zoomDenom)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          *result = _this->zoomDenom;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          const tjs_int value = static_cast<tjs_int>(*param);
          _this->zoomDenom = value > 0 ? value : 1;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ zoomDenom)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ mainWindow)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          const auto &scenes = WindowScenes();
          *result = (!scenes.empty() && scenes.front() == _this->scene) ? 1 : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ mainWindow)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ primaryLayer)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::WindowNativeInstance);
          if (_this->primaryLayer == nullptr) {
            ThrowScriptException("Window.primaryLayer",
                                 "no primary layer has been created (new Layer(window, null))");
          }
          *result = tTJSVariant(_this->primaryLayer, _this->primaryLayer);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ primaryLayer)

      TJS_END_NATIVE_MEMBERS
    }

  protected:
    // 没有这个 override，CreateNew 会把空的实例登记进去，
    // 构造时 NativeInstanceSupport 取到的指针为空（表现为 TJS_E_NATIVECLASSCRASH）。
    TJS::iTJSNativeInstance *CreateNativeInstance() override {
      return new detail::WindowNativeInstance();
    }
};

} // namespace

namespace detail {

std::string Utf8Text(const tTJSVariant &value) { return Utf8FromWide(ttstr(value).c_str()); }

tjs_uint32 WindowClassID() { return WindowClass::ClassID; }

WindowNativeInstance *ResolveWindow(const tTJSVariant &value, const std::string &context) {
  iTJSDispatch2 *object = value.AsObjectClosureNoAddRef().Object;
  if (object == nullptr) {
    ThrowScriptException(context, "the argument is not an object");
  }
  WindowNativeInstance *window = nullptr;
  if (TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, WindowClassID(),
                                               reinterpret_cast<iTJSNativeInstance **>(&window))) ||
      window == nullptr) {
    ThrowScriptException(context, "the argument is not a Window");
  }
  return window;
}

tjs_error TJS_INTF_METHOD WindowNativeInstance::Construct(tjs_int numparams, tTJSVariant **param,
                                                          iTJSDispatch2 *tjs_obj) {
  (void)numparams;
  (void)param;
  (void)tjs_obj;

  IWindowInfo *info = HostWindow();
  int width = info != nullptr ? info->GetWidth() : 0;
  int height = info != nullptr ? info->GetHeight() : 0;
  if (width <= 0 || height <= 0) {
    // 宿主没给表面尺寸时用参照实现的默认窗口尺寸，并明确记录降级原因。
    width = 640;
    height = 480;
    krkr::log::Warn("Window: host did not provide a surface size, falling back to ", width,
                    "x", height);
  }

  scene = std::make_shared<krkr::visual::Scene>(width, height);
  handle = std::make_shared<WindowHandle>();
  handle->instance = this;
  caption = info != nullptr ? info->GetTitle() : std::string();

  auto &scenes = WindowScenes();
  scenes.push_back(scene);
  krkr::log::Info("script: Window created (", width, "x", height, ", window #",
                  scenes.size() - 1, " of this session)");
  return TJS_S_OK;
}

void TJS_INTF_METHOD WindowNativeInstance::Invalidate() {
  if (handle != nullptr) {
    handle->instance = nullptr;
    handle.reset();
  }
  pinned.clear();
  primaryLayer = nullptr;

  auto &scenes = WindowScenes();
  const auto same = [this](const std::shared_ptr<krkr::visual::Scene> &scene) {
    return scene == this->scene;
  };
  const auto removed = std::remove_if(scenes.begin(), scenes.end(), same);
  if (removed != scenes.end()) {
    scenes.erase(removed, scenes.end());
    krkr::log::Debug("script: Window invalidated (", scenes.size(), " window(s) left)");
  }
  scene.reset();
}

} // namespace detail

tjs_uint32 WindowClass::ClassID = static_cast<tjs_uint32>(-1);

void RegisterWindowClass(const ScriptContext &context) {
  if (!ExposeNativeClass(context.engine, TJS_W("Window"), new WindowClass())) {
    return;
  }
  krkr::log::Info("script: Window registered (surface=",
                  context.services.window != nullptr ? 1 : 0, ")");
}

std::shared_ptr<krkr::visual::Scene> GetPrimaryScene() {
  const auto &scenes = WindowScenes();
  return scenes.empty() ? nullptr : scenes.front();
}

void ResetWindowRegistry() {
  if (!WindowScenes().empty()) {
    krkr::log::Info("script: window registry reset (", WindowScenes().size(),
                    " scene(s) dropped)");
    WindowScenes().clear();
  }
}

std::size_t GetWindowCount() { return WindowScenes().size(); }

} // namespace krkr::script
