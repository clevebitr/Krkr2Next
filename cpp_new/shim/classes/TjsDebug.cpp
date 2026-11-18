#include "shim/classes/TjsDebug.h"

#include <string>

#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/TjsDiagnostics.h"
#include "shim/engine/NativeClass.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

// Debug 的脚本侧可写状态。logLocation 是 startLogToFile 的目标路径；
// 两个 OnError 开关只保存取值：日志文件由宿主设定，引擎不做错误触发的重定向。
std::string &LogLocation() {
  static std::string value;
  return value;
}

bool &LogToFileOnError() {
  static bool value = false;
  return value;
}

bool &ClearLogFileOnError() {
  static bool value = false;
  return value;
}

std::string ArgumentsToText(tjs_int numparams, TJS::tTJSVariant **param) {
  std::string text;
  for (tjs_int i = 0; i < numparams; ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += Utf8FromWide(ttstr(*param[i]).c_str());
  }
  return text;
}

// 控制台窗口不存在：属性写入记录下来后忽略，读取固定为「不可见」。
class ConsoleClass final : public TJS::tTJSNativeClass {
  public:
    static inline tjs_uint32 ClassID = static_cast<tjs_uint32>(-1);

    ConsoleClass() : tTJSNativeClass(TJS_W("Console")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Console)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ Console)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Console)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ visible)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          krkr::log::Info("Debug.console.visible = ", param->operator bool(),
                          " ignored: no console window on this platform");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ visible)

      TJS_END_NATIVE_MEMBERS
    }
};

class ControllerClass final : public TJS::tTJSNativeClass {
  public:
    static inline tjs_uint32 ClassID = static_cast<tjs_uint32>(-1);

    ControllerClass() : tTJSNativeClass(TJS_W("Controller")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Controller)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ Controller)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Controller)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ visible)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          krkr::log::Info("Debug.controller.visible = ", param->operator bool(),
                          " ignored: no controller window on this platform");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ visible)

      TJS_END_NATIVE_MEMBERS
    }
};

TJS::iTJSDispatch2 *NewConsoleClass() {
  auto *object = new ConsoleClass();
  return object;
}

TJS::iTJSDispatch2 *NewControllerClass() {
  auto *object = new ControllerClass();
  return object;
}

} // namespace

class DebugClass final : public TJS::tTJSNativeClass {
  public:
    static inline tjs_uint32 ClassID = static_cast<tjs_uint32>(-1);

    DebugClass() : tTJSNativeClass(TJS_W("Debug")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Debug)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ Debug)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Debug)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ message) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string text = ArgumentsToText(numparams, param);
        krkr::log::Info("debug: ", text);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ message)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ notice) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        krkr::log::Warn("debug: ", ArgumentsToText(numparams, param));
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ notice)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ logAsError) {
        krkr::log::Error("debug: current log marked as error by script");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ logAsError)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ startLogToFile) {
        const bool clear = numparams >= 1 && param[0]->operator bool();
        const std::string &location = LogLocation();
        if (location.empty()) {
          ThrowScriptException("Debug.startLogToFile",
                               "logLocation is not set; log file is controlled by the host");
        }
        // clear 语义是「打开前清空」，Logger 只提供追加写入，明确记录差异。
        if (clear) {
          krkr::log::Warn("Debug.startLogToFile: truncation is not supported, "
                          "existing log file is appended");
        }
        krkr::log::Logger &logger = krkr::log::Logger::Instance();
        if (!logger.SetFile(location)) {
          ThrowScriptException("Debug.startLogToFile", logger.GetFileError());
        }
        krkr::log::Info("Debug.startLogToFile: log file switched to \"", location, "\"");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ startLogToFile)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getLastLog) {
        std::size_t lines = 512;
        if (numparams >= 1) {
          const tjs_int requested = param[0]->AsInteger();
          lines = requested > 0 ? static_cast<std::size_t>(requested) : lines;
        }
        if (result != nullptr) {
          *result = WideFromUtf8(krkr::log::Logger::Instance().GetRecentLines(lines)).c_str();
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getLastLog)

      // 日志回调需要 core/log 向脚本层转发的能力，尚未提供契约；
      // 显式失败而不是静默成功，避免脚本以为回调已生效。
      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addLoggingHandler) {
        krkr::log::Warn("Debug.addLoggingHandler: log listeners are not implemented");
        return TJS_E_NOTIMPL;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addLoggingHandler)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ removeLoggingHandler) {
        krkr::log::Warn("Debug.removeLoggingHandler: log listeners are not implemented");
        return TJS_E_NOTIMPL;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ removeLoggingHandler)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ logLocation)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = WideFromUtf8(LogLocation()).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          LogLocation() = Utf8FromWide(ttstr(*param).c_str());
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ logLocation)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ logToFileOnError)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = LogToFileOnError() ? 1 : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          LogToFileOnError() = param->operator bool();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ logToFileOnError)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ clearLogFileOnError)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = ClearLogFileOnError() ? 1 : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          ClearLogFileOnError() = param->operator bool();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ clearLogFileOnError)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ console)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS::iTJSDispatch2 *object = NewConsoleClass();
          *result = TJS::tTJSVariant(object, object);
          object->Release();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ console)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ controller)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS::iTJSDispatch2 *object = NewControllerClass();
          *result = TJS::tTJSVariant(object, object);
          object->Release();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ controller)

      TJS_END_NATIVE_MEMBERS
    }
};

void RegisterDebugClass(const ScriptContext &context) {
  if (!ExposeNativeClass(context.engine, TJS_W("Debug"), new DebugClass())) {
    return;
  }
  krkr::log::Info("script: Debug registered (console window unavailable)");
}

} // namespace krkr::script
