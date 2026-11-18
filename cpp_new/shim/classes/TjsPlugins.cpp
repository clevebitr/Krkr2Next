#include "shim/classes/TjsPlugins.h"

#include <cctype>
#include <string>
#include <vector>

#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/TjsDiagnostics.h"
#include "shim/engine/NativeClass.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsArray.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

// 静态插件名单。宿主按名字查表：动态库加载（.dll/.so）尚未实现，
// 因此列表之外的名字一律显式失败，不用占位成功掩盖。
std::vector<std::string> &StaticPlugins() {
  static std::vector<std::string> plugins;
  return plugins;
}

std::string NormalizePluginName(const std::string &name) {
  std::size_t start = name.find_last_of("/\\");
  start = start == std::string::npos ? 0 : start + 1;
  std::string base = name.substr(start);
  for (char &ch : base) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return base;
}

bool IsStaticPluginRegistered(const std::string &key) {
  for (const std::string &item : StaticPlugins()) {
    if (item == key) {
      return true;
    }
  }
  return false;
}

std::string CallContext(const char *method, const std::string &name) {
  return "Plugins." + std::string(method) + "(\"" + name + "\")";
}

class PluginsClass final : public TJS::tTJSNativeClass {
  public:
    static inline tjs_uint32 ClassID = static_cast<tjs_uint32>(-1);

    PluginsClass() : tTJSNativeClass(TJS_W("Plugins")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Plugins)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ Plugins)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Plugins)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ link) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string name = Utf8FromWide(ttstr(*param[0]).c_str());
        const std::string key = NormalizePluginName(name);
        const std::string context = CallContext("link", name);
        if (!IsStaticPluginRegistered(key)) {
          ThrowScriptException(context,
                               "plugin is not in the static registry "
                               "(native module loading is not implemented)");
        }
        krkr::log::Info(context, ": static plugin attached");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ link)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ unlink) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string name = Utf8FromWide(ttstr(*param[0]).c_str());
        const std::string context = CallContext("unlink", name);
        // 静态链接的插件不能被卸载；返回 false 并记录原因。
        krkr::log::Warn(context, ": static plugins cannot be unloaded");
        if (result != nullptr) {
          *result = 0;
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ unlink)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getList) {
        TJS::iTJSDispatch2 *array = TJS::TJSCreateArrayObject();
        if (array == nullptr) {
          ThrowScriptException("Plugins.getList", "cannot create array object");
        }
        tjs_int index = 0;
        for (const std::string &item : StaticPlugins()) {
          TJS::tTJSVariant value(WideFromUtf8(item).c_str());
          array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
        }
        if (result != nullptr) {
          *result = TJS::tTJSVariant(array, array);
        }
        array->Release();
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getList)

      TJS_END_NATIVE_MEMBERS
    }
};

} // namespace

void RegisterStaticPlugin(const std::string &name) {
  const std::string key = NormalizePluginName(name);
  if (key.empty() || IsStaticPluginRegistered(key)) {
    return;
  }
  StaticPlugins().push_back(key);
  krkr::log::Info("plugin: registered static plugin \"", key, "\"");
}

void RegisterPluginsClass(const ScriptContext &context) {
  if (!ExposeNativeClass(context.engine, TJS_W("Plugins"), new PluginsClass())) {
    return;
  }
  krkr::log::Info("script: Plugins registered (static plugins=", StaticPlugins().size(),
                  ", native loading unavailable)");
}

} // namespace krkr::script
