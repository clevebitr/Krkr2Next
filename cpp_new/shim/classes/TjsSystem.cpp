#include "shim/classes/TjsSystem.h"

#include <string>

#include "core/log/Logger.h"
#include "core/contracts/IScriptHost.h"
#include "shim/TjsBridge.h"
#include "shim/engine/TjsEngine.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

krkr::script::IScriptHost *Host() { return GetBridge().host; }

// 参数形如 "-name" 或 "-name=value"；命中时返回其值，未命中返回空串
// （与 KiriKiri 的行为一致：未提供的启动参数视为空字符串）。
std::string LookupArgument(const std::string &key) {
  if (Host() == nullptr) {
    return {};
  }
  const std::vector<std::string> arguments = Host()->GetLaunchArguments();
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

class SystemClass final : public TJS::tTJSNativeClass {
  public:
    static tjs_uint32 ClassID;

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
        const ttstr keyText = *param[0];
        const std::string key = Utf8FromWide(keyText.c_str());
        const std::string value = LookupArgument(key);
        krkr::log::Debug("System.getArgument(\"", key, "\") = \"", value, "\"");
        if (result != nullptr) {
          *result = WideFromUtf8(value).c_str();
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getArgument)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ inform) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr text = *param[0];
        if (Host() != nullptr) {
          Host()->Inform(Utf8FromWide(text.c_str()));
        } else {
          krkr::log::Info("script: ", Utf8FromWide(text.c_str()));
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ inform)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ informError) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr text = *param[0];
        if (Host() != nullptr) {
          Host()->InformError(Utf8FromWide(text.c_str()));
        } else {
          krkr::log::Error("script: ", Utf8FromWide(text.c_str()));
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ informError)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exit) {
        if (Host() != nullptr) {
          Host()->RequestExit();
        } else {
          krkr::log::Warn("System.exit called without attached host");
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exit)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ title)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          const std::string title = Host() != nullptr ? Host()->GetTitle() : std::string();
          *result = WideFromUtf8(title).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          const ttstr newTitle = *param;
          if (Host() != nullptr) {
            Host()->SetTitle(Utf8FromWide(newTitle.c_str()));
          }
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ title)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ versionInformation)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          const std::string info =
              Host() != nullptr ? Host()->GetVersionInformation() : std::string("cpp_new");
          *result = WideFromUtf8(info).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ versionInformation)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ desktopWidth)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = Host() != nullptr ? Host()->GetSurfaceWidth() : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ desktopWidth)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ desktopHeight)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          *result = Host() != nullptr ? Host()->GetSurfaceHeight() : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ desktopHeight)

      TJS_END_NATIVE_MEMBERS
    }
};

} // namespace

tjs_uint32 SystemClass::ClassID = static_cast<tjs_uint32>(-1);

void RegisterSystemClass(Engine *engine, IScriptHost *host) {
  Bridge &bridge = GetBridge();
  bridge.engine = engine;
  bridge.host = host;

  if (engine == nullptr || !engine->IsReady()) {
    krkr::log::Warn("RegisterSystemClass: engine not ready");
    return;
  }

  auto *global = static_cast<TJS::iTJSDispatch2 *>(engine->GetGlobalObjectRaw());
  if (global == nullptr) {
    krkr::log::Warn("RegisterSystemClass: global object unavailable");
    return;
  }

  auto *nativeClass = new SystemClass();
  TJS::tTJSVariant value(nativeClass, nullptr);
  const tjs_error error =
      global->PropSet(TJS_MEMBERENSURE, TJS_W("System"), nullptr, &value, global);
  nativeClass->Release();
  if (TJS_FAILED(error)) {
    krkr::log::Error("RegisterSystemClass: failed to expose System, code=", error);
    return;
  }
  krkr::log::Info("script: System registered (host attached=", host != nullptr, ")");
}

} // namespace krkr::script
