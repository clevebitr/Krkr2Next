#include "core/env/app/EngineRuntime.h"

namespace krkr::environ {
  EngineRuntime *EngineRuntime::Create() { return new EngineRuntime(); };

  void EngineRuntime::Destroy(EngineRuntime *engine) { delete engine; }

  EngineResult EngineRuntime::OpenGame(const char *gameRootUtf8) {
    if (gameRootUtf8 == nullptr || gameRootUtf8[0] == '\0') {
      lastError_ = "game root is empty";
      return EngineResult::InvalidArgument;
    }
    if (gameOpen_) {
      lastError_ = "game already open";
      return EngineResult::InvalidState;
    }
    gameRoot_ = gameRootUtf8;
    gameOpen_ = true;
    lastError_.clear();
    return EngineResult::Ok;
  }

  EngineResult EngineRuntime::Tick(double deltaSeconds) {
    if (!gameOpen_) {
      lastError_ = "Tick before OpenGame";
      return EngineResult::InvalidState;
    }
    if (deltaSeconds < 0.0) {
      lastError_ = "negative delta";
      return EngineResult::InvalidArgument;
    }
    ++frameCount_;
    return EngineResult::Ok;
  }
} // namespace krkr::environ
