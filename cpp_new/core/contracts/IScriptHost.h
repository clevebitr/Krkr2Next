#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace krkr::script {
  // 脚本层对宿主环境的抽象。
  //
  // 方向：TJS 原生类只依赖本接口；引擎入口（core/api）负责实现，
  // 因而脚本层不感知窗口、配置与生命周期细节。
  class IScriptHost {
    public:
      virtual ~IScriptHost() = default;

      virtual std::vector<std::string> GetLaunchArguments() const { return {}; }

      virtual void Inform(const std::string &text) {}
      virtual void InformError(const std::string &text) {}

      virtual void RequestExit() {}

      virtual void SetTitle(const std::string &title) {}
      virtual std::string GetTitle() const { return {}; }

      virtual int GetSurfaceWidth() const { return 0; }
      virtual int GetSurfaceHeight() const { return 0; }

      // 用于 System.versionInformation 一类只读信息。
      virtual std::string GetVersionInformation() const { return "cpp_new"; }
  };
} // namespace krkr::script
