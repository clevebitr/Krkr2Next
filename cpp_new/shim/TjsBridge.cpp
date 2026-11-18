#include "shim/TjsBridge.h"

namespace krkr::script {

ScriptContext &GetContext() {
  static ScriptContext instance;
  return instance;
}

void SetContext(const ScriptContext &context) {
  GetContext() = context;
}

} // namespace krkr::script
