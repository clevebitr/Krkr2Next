#include "shim/classes/TjsSystem.h"

#include <string>
#include <vector>

#include "core/contracts/ScriptServices.h"
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

// 参数形如 "-name" 或 "-name=value"；命中时返回其值，未命中返回空串
// （与 KiriKiri 的行为一致：未提供的启动参数视为空字符串）。
std::string LookupArgument(const std::vector<std::string> &arguments, const std::string &key) {
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string &item = arguments[i];
    if (item == key) {
      if (i + 1 < arguments.size() && !arguments[i + 1].empty() &&
          arguments[i + 1][0] != '-') {
        return arguments[i + 1];
      }
      return {};
    }
    if (item.size() > key.size() && item.compare(0, key.size(), key) == 0 &&
        item[key.size()] == '=') {
      return item.substr(key.size() + 1);
    }
  }
  return {};
}

ILaunchArguments *Arguments() { return GetContext().services.arguments; }
IPlatformInfo *Platform() { return GetContext().services.platform; }
IPathProvider *Paths() { return GetContext().services.paths; }
IWindowInfo *Window() { return GetContext().services.window; }

std::string RequiredPath(const char *name, std::string (IPathProvider::*getter)() const) {
  IPathProvider *paths = Paths();
  if (paths == nullptr) {
    ThrowScriptException(std::string("System.") + name, "host did not provide a path service");
  }
  const std::string value = (paths->*getter)();
  if (value.empty()) {
    ThrowScriptException(std::string("System.") + name,
                         "host did not configure this path");
  }
  return value;
}

class SystemClass final : public TJS::tTJSNativeClass {
  public:
    static inline tjs_uint32 ClassID = static_cast<tjs_uint32>(-1);

    SystemClass() : tTJSNativeClass(TJS_W("System")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ System)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ System)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ System)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getArgument) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string key = Utf8FromWide(ttstr(*param[0]).c_str());
        ILaunchArguments *arguments = Arguments();
        if (arguments == nullptr) {
          ThrowScriptException("System.getArgument", "host did not provide launch arguments");
        }
        const std::string value = LookupArgument(arguments->GetLaunchArguments(), key);
        krkr::log::Debug("System.getArgument(\"", key, "\") = \"", value, "\"");
        if (result != nullptr) {
          *result = WideFromUtf8(value).c_str();
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getArgument)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setArgument) {
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string key = Utf8FromWide(ttstr(*param[0]).c_str());
        const std::string value = Utf8FromWide(ttstr(*param[1]).c_str());
        ILaunchArguments *arguments = Arguments();
        if (arguments == nullptr) {
          ThrowScriptException("System.setArgument", "host did not provide launch arguments");
        }
        if (!arguments->SetLaunchArgument(key, value)) {
          ThrowScriptException("System.setArgument", "host rejected the argument list write");
        }
        krkr::log::Info("System.setArgument(\"", key, "\") = \"", value, "\"");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ setArgument)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ inform) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string text = Utf8FromWide(ttstr(*param[0]).c_str());
        if (IOutput *output = GetContext().services.output; output != nullptr) {
          output->Inform(text);
        } else {
          krkr::log::Info("script: ", text);
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ inform)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ informError) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string text = Utf8FromWide(ttstr(*param[0]).c_str());
        if (IOutput *output = GetContext().services.output; output != nullptr) {
          output->InformError(text);
        } else {
          krkr::log::Error("script: ", text);
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ informError)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exit) {
        if (IAppControl *appControl = GetContext().services.appControl;
            appControl != nullptr) {
          appControl->RequestExit();
        } else {
          krkr::log::Warn("System.exit ignored: host did not provide app control");
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exit)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTickCount) {
        IPlatformInfo *platform = Platform();
        if (platform == nullptr) {
          ThrowScriptException("System.getTickCount", "host did not provide platform info");
        }
        if (result != nullptr) {
          *result = static_cast<tjs_int64>(platform->GetTickCount());
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getTickCount)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ title)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IWindowInfo *window = Window();
          if (window == nullptr) {
            ThrowScriptException("System.title", "host did not provide window info");
          }
          *result = WideFromUtf8(window->GetTitle()).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          IWindowInfo *window = Window();
          if (window == nullptr) {
            ThrowScriptException("System.title", "host did not provide window info");
          }
          window->SetTitle(Utf8FromWide(ttstr(*param).c_str()));
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ title)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ versionInformation)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IPlatformInfo *platform = Platform();
          if (platform == nullptr) {
            ThrowScriptException("System.versionInformation",
                                 "host did not provide platform info");
          }
          *result = WideFromUtf8(platform->GetVersionString()).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ versionInformation)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ versionString)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IPlatformInfo *platform = Platform();
          if (platform == nullptr) {
            ThrowScriptException("System.versionString",
                                 "host did not provide platform info");
          }
          *result = WideFromUtf8(platform->GetVersionString()).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ versionString)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ osName)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IPlatformInfo *platform = Platform();
          if (platform == nullptr) {
            ThrowScriptException("System.osName", "host did not provide platform info");
          }
          *result = WideFromUtf8(platform->GetOSName()).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ osName)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ platformName)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IPlatformInfo *platform = Platform();
          if (platform == nullptr) {
            ThrowScriptException("System.platformName", "host did not provide platform info");
          }
          *result = WideFromUtf8(platform->GetPlatformName()).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ platformName)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ exePath)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = WideFromUtf8(RequiredPath("exePath", &IPathProvider::GetExecutableDirectory))
                        .c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ exePath)

      // 参照实现里 arcPath 与 exePath 同值（KRKRZ 系 initialize.tjs 会读它）。
      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ arcPath)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = WideFromUtf8(RequiredPath("arcPath", &IPathProvider::GetExecutableDirectory))
                        .c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ arcPath)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ dataPath)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = WideFromUtf8(RequiredPath("dataPath", &IPathProvider::GetDataDirectory))
                        .c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ dataPath)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ personalPath)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = WideFromUtf8(RequiredPath("personalPath", &IPathProvider::GetPersonalDirectory))
                        .c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ personalPath)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ savedGamesPath)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = WideFromUtf8(
                        RequiredPath("savedGamesPath", &IPathProvider::GetSavedGamesDirectory))
                        .c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ savedGamesPath)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ desktopWidth)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IWindowInfo *window = Window();
          if (window == nullptr) {
            ThrowScriptException("System.desktopWidth", "host did not provide window info");
          }
          *result = window->GetDesktopWidth();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ desktopWidth)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ desktopHeight)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          IWindowInfo *window = Window();
          if (window == nullptr) {
            ThrowScriptException("System.desktopHeight", "host did not provide window info");
          }
          *result = window->GetDesktopHeight();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_STATIC_PROP_DECL(/*prop. name*/ desktopHeight)

      TJS_END_NATIVE_MEMBERS
    }
};

} // namespace

void RegisterSystemClass(const ScriptContext &context) {
  if (!ExposeNativeClass(context.engine, TJS_W("System"), new SystemClass())) {
    return;
  }
  krkr::log::Info("script: System registered (services: arguments=",
                  context.services.arguments != nullptr ? 1 : 0,
                  " window=", context.services.window != nullptr ? 1 : 0,
                  " paths=", context.services.paths != nullptr ? 1 : 0, ")");
}

} // namespace krkr::script
