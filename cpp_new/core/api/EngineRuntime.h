#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/contracts/DebugServices.h"
#include "core/contracts/IScriptRuntime.h"
#include "core/contracts/ScriptServices.h"
#include "core/io/Storage.h"

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

  // 引擎运行时。
  //
  // 自身实现脚本层需要的宿主能力（启动参数、输出、退出、平台信息、路径、窗口），
  // 脚本运行时（cpp_new/engine）由宿主注入，本类只通过 IScriptRuntime 认识它。
  class EngineRuntime final : public krkr::script::ILaunchArguments,
                              public krkr::script::IOutput,
                              public krkr::script::IAppControl,
                              public krkr::script::IPlatformInfo,
                              public krkr::script::IPathProvider,
                              public krkr::script::IWindowInfo {
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

      // 脚本运行时由宿主注入：引擎不引用 shim/tjs，也决定不了装配内容。
      // 必须在 OpenGame 之前挂接；所有权仍在宿主，Destroy 前需保证其存活。
      bool AttachScriptRuntime(krkr::script::IScriptRuntime *runtime);
      void DetachScriptRuntime();

      // 调试观察平面：挂接后引擎状态、失败、挂载表、退出请求都会产生事件。
      // 不挂接时各发送点只做一次空指针检查。
      void AttachDebugSink(krkr::debug::IDebugSink *sink);
      krkr::debug::IDebugSink *GetDebugSink() const { return debugSink_; }

      // 宿主环境（路径、参数）由嵌入方提供；未设置的路径在脚本读取时显式失败。
      void SetExecutablePath(const std::string &pathUtf8);
      void SetPersonalDirectory(const std::string &pathUtf8);
      void SetSavedGamesDirectory(const std::string &pathUtf8);
      void SetLaunchArguments(std::vector<std::string> arguments);

      krkr::script::HostServices Services();

      // krkr::script::ILaunchArguments
      std::vector<std::string> GetLaunchArguments() const override;
      bool SetLaunchArgument(const std::string &key, const std::string &value) override;

      // krkr::script::IOutput
      void Inform(const std::string &text) override;
      void InformError(const std::string &text) override;

      // krkr::script::IAppControl
      void RequestExit() override;

      // krkr::script::IPlatformInfo
      std::string GetOSName() const override;
      std::string GetPlatformName() const override;
      std::string GetVersionString() const override;
      std::int64_t GetTickCount() const override;

      // krkr::script::IPathProvider
      std::string GetExecutableDirectory() const override;
      std::string GetDataDirectory() const override;
      std::string GetPersonalDirectory() const override;
      std::string GetSavedGamesDirectory() const override;

      // krkr::script::IWindowInfo
      int GetWidth() const override;
      int GetHeight() const override;
      int GetDesktopLeft() const override;
      int GetDesktopTop() const override;
      int GetDesktopWidth() const override;
      int GetDesktopHeight() const override;
      std::string GetTitle() const override;
      void SetTitle(const std::string &title) override;

      void SetSurfaceSize(int width, int height);
      bool IsExitRequested() const { return exitRequested_.load(); }

    private:
      EngineRuntime();
      ~EngineRuntime() = default;

      void RunStartupWork();
      void FailStartup(const std::string &reason);
      void SetStartupState(EngineStartupState state);

      std::string   gameRoot_;
      std::string   lastError_;
      std::string   executablePath_;
      std::string   personalDirectory_;
      std::string   savedGamesDirectory_;
      std::unique_ptr<krkr::io::Storage> storage_;
      krkr::debug::IDebugSink *debugSink_ = nullptr;
      krkr::script::IScriptRuntime *scriptRuntime_ = nullptr;
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
      std::chrono::steady_clock::time_point startTime_;
  };
} // namespace krkr::env
