#include "engine/ScriptRuntime.h"

#include <string>

#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/engine/TjsEngine.h"

namespace krkr::engine {

class ScriptRuntime::Impl {
  public:
    explicit Impl(Profile profileIn) : profile(std::move(profileIn)) {}

    Profile profile;
    std::unique_ptr<krkr::script::Engine> engine;
    bool started = false;
};

ScriptRuntime::ScriptRuntime(Profile profile)
    : impl_(std::make_unique<Impl>(std::move(profile))) {
  impl_->engine = std::make_unique<krkr::script::Engine>();
}

ScriptRuntime::~ScriptRuntime() { Shutdown(); }

bool ScriptRuntime::Startup(krkr::io::Storage *storage,
                            const krkr::script::HostServices &services,
                            std::string *errorUtf8) {
  if (impl_->started) {
    if (errorUtf8 != nullptr) {
      *errorUtf8 = "script runtime is already started";
    }
    return false;
  }
  if (!impl_->engine->IsReady()) {
    if (errorUtf8 != nullptr) {
      *errorUtf8 = "script engine is not ready";
    }
    return false;
  }

  krkr::script::ScriptContext context;
  context.engine = impl_->engine.get();
  context.storage = storage;
  context.services = services;
  krkr::script::SetContext(context);

  for (const auto &[name, value] : impl_->profile.preprocessorValues) {
    impl_->engine->SetPreprocessorValue(name, value);
  }
  krkr::log::Info("script: profile \"", impl_->profile.name, "\" applied (",
                  impl_->profile.preprocessorValues.size(), " preprocessor value(s), ",
                  impl_->profile.nativeClasses.size(), " native class(es))");

  for (void (*install)(const krkr::script::ScriptContext &) : impl_->profile.nativeClasses) {
    install(krkr::script::GetContext());
  }

  if (!impl_->profile.preludeScript.empty()) {
    if (!impl_->engine->RunScript(impl_->profile.preludeScript, "compat:prelude", errorUtf8)) {
      return false;
    }
  }

  impl_->started = true;
  return true;
}

bool ScriptRuntime::RunStartupScript(std::string *errorUtf8) {
  if (!impl_->started) {
    if (errorUtf8 != nullptr) {
      *errorUtf8 = "script runtime is not started";
    }
    return false;
  }
  return impl_->engine->RunStorageScript("startup.tjs", errorUtf8);
}

void ScriptRuntime::Tick(double deltaSeconds) {
  // 脚本侧事件与定时器尚未接入；留出接口以免引擎侧再改一次契约。
  (void)deltaSeconds;
}

void ScriptRuntime::Shutdown() {
  if (impl_ == nullptr || !impl_->started) {
    return;
  }
  krkr::script::SetContext(krkr::script::ScriptContext{});
  impl_->started = false;
}

krkr::script::Engine *ScriptRuntime::GetEngine() { return impl_->engine.get(); }

} // namespace krkr::engine
