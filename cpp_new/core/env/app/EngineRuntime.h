#pragma once

#include <cstdio>
#include <string>

namespace krkr::environ {
  enum class EngineResult : std::int32_t {
    Ok              =  0,
    InvalidArgument = -1,
    InvalidState    = -2,
    IoError         = -4,
    InternalError   = -5,
  };

  enum class GameKind : std::int32_t {
    None            =  0,
    Directory       =  1,
    Archive         =  2,
  };

  class EngineRuntime final {
    public:
      static EngineRuntime *Create();
      static void Destroy(EngineRuntime *engine);

      EngineRuntime(const EngineRuntime &) = delete;
      EngineRuntime &operator = (const EngineRuntime &) = delete;

      EngineResult OpenGame(const char *gameRootUtf8);
      EngineResult Tick(double deltaSeconds);

      std::uint64_t GetFrameCount() const { return frameCount_; };
      bool IsGameOpen() const { return gameOpen_; };

      GameKind GetGameKind() const { return gameKind_; };
      const std::string &GetGameRoot() const { return gameRoot_; };

      const char *GetLastError() const { return lastError_.c_str(); };

    private:
      EngineRuntime()  = default;
      ~EngineRuntime() = default;

      std::string   gameRoot_;
      std::string   lastError_;
      std::uint64_t frameCount_ = 0;
      GameKind      gameKind_   = GameKind::None;
      bool          gameOpen_   = false;
  };
} // namespace krkr::environ
