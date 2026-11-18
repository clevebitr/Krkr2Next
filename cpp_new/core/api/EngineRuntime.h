#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/io/Storage.h"
#include "core/contracts/IScriptHost.h"

namespace krkr::env {
  enum class EngineResult : std::int32_t {
    Ok              =  0,
    InvalidArgument = -1,
    InvalidState    = -2,
    IoError         = -4,
    InternalError   = -5,
    StartupPending  = -7,
    // 脚本请求退出（System.exit）。数值与 ABI 的 ENGINE_RESULT_GAME_TERMINATED 一致。
    GameTerminated  = -6,
  };

  enum class GameKind : std::int32_t {
    None            =  0,
    Directory       =  1,
    Archive         =  2,
  };

  enum class EngineStartupState : std::uint32_t {
    Idle            =  0,
    Running         =  1,
    Succeeded       =  2,
    Failed          =  3,
  };

  class EngineRuntime final : public krkr::script::IScriptHost {
    public:
      static EngineRuntime *Create();
      static void Destroy(EngineRuntime *engine);

      EngineRuntime(const EngineRuntime &) = delete;
      EngineRuntime &operator = (const EngineRuntime &) = delete;

      EngineResult OpenGame(const char *gameRootUtf8);
      EngineResult Tick(double deltaSeconds);

      std::uint64_t GetFrameCount() const { return frameCount_; }
      bool IsGameOpen() const { return gameOpen_; }

      GameKind GetGameKind() const { return gameKind_; }
      const std::string &GetGameRoot() const { return gameRoot_; }

      EngineStartupState GetStartupState() const {
        return static_cast<EngineStartupState>(startupState_.load());
      }

      const char *GetLastErrorText() const { return lastError_.c_str(); }

      void SetLaunchArguments(std::vector<std::string> arguments);

      // ScriptHost 实现：脚本层的 System.inform / System.exit / System.title 等落到此处。
      std::vector<std::string> GetLaunchArguments() const override;
      void Inform(const std::string &text) override;
      void InformError(const std::string &text) override;
      void RequestExit() override;
      void SetTitle(const std::string &title) override;
      std::string GetTitle() const override;
      int GetSurfaceWidth() const override;
      int GetSurfaceHeight() const override;
      std::string GetVersionInformation() const override;

      void SetSurfaceSize(int width, int height);
      bool IsExitRequested() const { return exitRequested_.load(); }

    private:
      EngineRuntime()  = default;
      ~EngineRuntime() = default;

      void RunStartupWork();

      std::string   gameRoot_;
      std::string   lastError_;
      std::unique_ptr<krkr::io::Storage> storage_;
      std::uint64_t frameCount_ = 0;
      GameKind      gameKind_   = GameKind::None;
      bool          gameOpen_   = false;
      std::atomic<std::uint32_t> startupState_{
          static_cast<std::uint32_t>(EngineStartupState::Idle)};
      std::atomic<bool> exitRequested_{false};
      std::vector<std::string> launchArguments_;
      std::string title_;
      int surfaceWidth_ = 1280;
      int surfaceHeight_ = 720;
  };
} // namespace krkr::env
