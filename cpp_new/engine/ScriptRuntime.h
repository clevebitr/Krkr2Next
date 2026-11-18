#pragma once

#include <memory>
#include <string>

#include "core/contracts/IScriptRuntime.h"
#include "engine/CompatProfile.h"

namespace krkr::io {
  class Storage;
}

namespace krkr::script {
  class Engine;
}

namespace krkr::engine {
  // 脚本装配根。
  //
  // 依赖方向：本类同时引用 core（契约、存储、日志）与 shim（引擎、原生类），
  // 是唯一允许这样做的层；core/api 只通过 IScriptRuntime 认识它。
  class ScriptRuntime final : public krkr::script::IScriptRuntime {
    public:
      explicit ScriptRuntime(Profile profile = MakeKiriKiri2Profile());
      ~ScriptRuntime() override;

      ScriptRuntime(const ScriptRuntime &) = delete;
      ScriptRuntime &operator = (const ScriptRuntime &) = delete;

      // IScriptRuntime
      bool Startup(krkr::io::Storage *storage, const krkr::script::HostServices &services,
                   std::string *errorUtf8) override;
      bool RunStartupScript(std::string *errorUtf8) override;
      void Tick(double deltaSeconds) override;
      void Shutdown() override;

      // 测试台与后续模块（KAGParser、插件脚本）共用的入口。
      krkr::script::Engine *GetEngine();

    private:
      class Impl;
      std::unique_ptr<Impl> impl_;
  };
} // namespace krkr::engine
