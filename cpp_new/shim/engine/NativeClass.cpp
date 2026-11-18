#include "shim/engine/NativeClass.h"

#include "core/log/Logger.h"
#include "shim/engine/TjsEngine.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"

namespace krkr::script {

bool ExposeNativeClass(Engine *engine, const tjs_char *name, TJS::tTJSNativeClass *nativeClass) {
  if (engine == nullptr || !engine->IsReady() || nativeClass == nullptr) {
    krkr::log::Warn("ExposeNativeClass: engine or class is not ready");
    return false;
  }

  auto *global = static_cast<TJS::iTJSDispatch2 *>(engine->GetGlobalObjectRaw());
  if (global == nullptr) {
    krkr::log::Warn("ExposeNativeClass: global object unavailable");
    return false;
  }

  TJS::tTJSVariant value(nativeClass, nullptr);
  const tjs_error error = global->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, global);
  nativeClass->Release();
  if (TJS_FAILED(error)) {
    krkr::log::Error("ExposeNativeClass: failed to expose ", Utf8FromWide(name),
                     ", code=", error);
    return false;
  }
  return true;
}

} // namespace krkr::script
