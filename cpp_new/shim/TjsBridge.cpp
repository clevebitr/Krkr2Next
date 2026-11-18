#include "shim/TjsBridge.h"

namespace krkr::script {

Bridge &GetBridge() {
  static Bridge instance;
  return instance;
}

} // namespace krkr::script
