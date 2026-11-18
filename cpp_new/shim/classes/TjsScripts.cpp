#include "shim/classes/TjsScripts.h"

#include <string>

#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/engine/TjsEngine.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

std::string ReadAllFromStorage(const std::string &name, bool *ok) {
  *ok = false;
  Bridge &bridge = GetBridge();
  if (bridge.storage == nullptr) {
    krkr::log::Warn("Scripts: storage not attached");
    return {};
  }
  const std::unique_ptr<krkr::io::IStream> stream = bridge.storage->Open(name);
  if (stream == nullptr) {
    krkr::log::Warn("Scripts: cannot open \"", name, "\": ",
                    bridge.storage->GetLastErrorText());
    return {};
  }

  std::string bytes;
  char buffer[16 * 1024];
  std::int64_t read = 0;
  while ((read = stream->Read(buffer, static_cast<std::int64_t>(sizeof(buffer)))) > 0) {
    bytes.append(buffer, static_cast<std::size_t>(read));
  }
  if (read < 0) {
    krkr::log::Error("Scripts: read error on \"", name, "\"");
    return {};
  }
  *ok = true;
  return bytes;
}

tjs_error RunSource(const std::string &sourceUtf8, const char *name, bool evaluate,
                    TJS::tTJSVariant *result, TJS::iTJSDispatch2 *context) {
  Bridge &bridge = GetBridge();
  auto *tjs = static_cast<TJS::tTJS *>(bridge.engine->GetVMScriptEngineRaw());
  if (tjs == nullptr) {
    return TJS_E_FAIL;
  }

  const std::wstring source = WideFromUtf8(sourceUtf8);
  const std::wstring wideName = WideFromUtf8(name == nullptr ? "Scripts" : name);
  try {
    if (evaluate) {
      tTJSVariant evaluated;
      tjs->EvalExpression(source.c_str(), &evaluated, context, wideName.c_str());
      if (result != nullptr) {
        *result = evaluated;
      }
    } else {
      tjs->ExecScript(source.c_str(), result, context, wideName.c_str());
    }
  } catch (const TJS::eTJS &e) {
    krkr::log::Error("Scripts: ", name, ": ", Utf8FromWide(e.GetMessage().c_str()));
    return TJS_E_FAIL;
  } catch (...) {
    krkr::log::Error("Scripts: unknown exception while running ", name);
    return TJS_E_FAIL;
  }
  return TJS_S_OK;
}

class ScriptsClass final : public TJS::tTJSNativeClass {
  public:
    static tjs_uint32 ClassID;

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
        const ttstr sourceText = *param[0];
        const std::string source = Utf8FromWide(sourceText.c_str());
        iTJSDispatch2 *context =
            (numparams >= 2 && param[1]->Type() != tvtVoid) ? param[1]->AsObjectNoAddRef()
                                                            : nullptr;
        krkr::log::Debug("Scripts.eval(", source.size(), " chars)");
        return RunSource(source, "eval", true, result, context);
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ eval)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exec) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr sourceText = *param[0];
        const std::string source = Utf8FromWide(sourceText.c_str());
        iTJSDispatch2 *context =
            (numparams >= 2 && param[1]->Type() != tvtVoid) ? param[1]->AsObjectNoAddRef()
                                                            : nullptr;
        krkr::log::Debug("Scripts.exec(", source.size(), " chars)");
        return RunSource(source, "exec", false, result, context);
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exec)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ evalStorage) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        bool ok = false;
        const std::string bytes = ReadAllFromStorage(name, &ok);
        if (!ok) {
          return TJS_E_FAIL;
        }
        iTJSDispatch2 *context =
            (numparams >= 2 && param[1]->Type() != tvtVoid) ? param[1]->AsObjectNoAddRef()
                                                            : nullptr;
        krkr::log::Debug("Scripts.evalStorage(", name, ")");
        return RunSource(DecodeScriptSource(bytes), name.c_str(), true, result, context);
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ evalStorage)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ execStorage) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        bool ok = false;
        const std::string bytes = ReadAllFromStorage(name, &ok);
        if (!ok) {
          return TJS_E_FAIL;
        }
        iTJSDispatch2 *context =
            (numparams >= 2 && param[1]->Type() != tvtVoid) ? param[1]->AsObjectNoAddRef()
                                                            : nullptr;
        krkr::log::Debug("Scripts.execStorage(", name, ")");
        return RunSource(DecodeScriptSource(bytes), name.c_str(), false, result, context);
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

tjs_uint32 ScriptsClass::ClassID = static_cast<tjs_uint32>(-1);

void RegisterScriptsClass(Engine *engine, krkr::io::Storage *storage) {
  Bridge &bridge = GetBridge();
  bridge.engine = engine;
  bridge.storage = storage;

  if (engine == nullptr || !engine->IsReady()) {
    krkr::log::Warn("RegisterScriptsClass: engine not ready");
    return;
  }

  auto *global = static_cast<TJS::iTJSDispatch2 *>(engine->GetGlobalObjectRaw());
  if (global == nullptr) {
    krkr::log::Warn("RegisterScriptsClass: global object unavailable");
    return;
  }

  auto *nativeClass = new ScriptsClass();
  TJS::tTJSVariant value(nativeClass, nullptr);
  const tjs_error error =
      global->PropSet(TJS_MEMBERENSURE, TJS_W("Scripts"), nullptr, &value, global);
  nativeClass->Release();
  if (TJS_FAILED(error)) {
    krkr::log::Error("RegisterScriptsClass: failed to expose Scripts, code=", error);
    return;
  }
  krkr::log::Info("script: Scripts registered (storage attached=", storage != nullptr, ")");
}

} // namespace krkr::script
