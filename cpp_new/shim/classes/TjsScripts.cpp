#include "shim/classes/TjsScripts.h"

#include <string>

#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/engine/NativeClass.h"
#include "shim/engine/ScriptLoader.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

std::string CallArgument(tjs_int numparams, TJS::tTJSVariant **param) {
  return numparams >= 1 ? Utf8FromWide(ttstr(*param[0]).c_str()) : std::string();
}

TJS::iTJSDispatch2 *ContextArgument(tjs_int numparams, TJS::tTJSVariant **param) {
  return (numparams >= 2 && param[1]->Type() != tvtVoid) ? param[1]->AsObjectNoAddRef()
                                                         : nullptr;
}

class ScriptsClass final : public TJS::tTJSNativeClass {
  public:
    static inline tjs_uint32 ClassID = static_cast<tjs_uint32>(-1);

    ScriptsClass() : tTJSNativeClass(TJS_W("Scripts")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Scripts)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ Scripts)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Scripts)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ eval) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string source = CallArgument(numparams, param);
        const std::string context = "Scripts.eval(\"" + std::to_string(source.size()) + " bytes\")";
        RunScriptBytes(*GetContext().engine, source, context, context, ScriptMode::Evaluate,
                       result, ContextArgument(numparams, param));
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ eval)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exec) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string source = CallArgument(numparams, param);
        const std::string context = "Scripts.exec(\"" + std::to_string(source.size()) + " bytes\")";
        RunScriptBytes(*GetContext().engine, source, context, context, ScriptMode::Execute,
                       result, ContextArgument(numparams, param));
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exec)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ evalStorage) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string name = CallArgument(numparams, param);
        RunScriptFromStorage(*GetContext().engine, name, ScriptMode::Evaluate,
                             "Scripts.evalStorage", result,
                             ContextArgument(numparams, param));
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ evalStorage)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ execStorage) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const std::string name = CallArgument(numparams, param);
        RunScriptFromStorage(*GetContext().engine, name, ScriptMode::Execute,
                             "Scripts.execStorage", result,
                             ContextArgument(numparams, param));
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ execStorage)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTraceString) {
        // 调用栈追溯依赖调试设施，当前版本不提供，返回空串以保持脚本兼容。
        if (result != nullptr) {
          *result = TJS_W("");
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getTraceString)

      TJS_END_NATIVE_MEMBERS
    }
};

} // namespace

void RegisterScriptsClass(const ScriptContext &context) {
  if (!ExposeNativeClass(context.engine, TJS_W("Scripts"), new ScriptsClass())) {
    return;
  }
  krkr::log::Info("script: Scripts registered (storage attached=",
                  context.storage != nullptr, ")");
}

} // namespace krkr::script
