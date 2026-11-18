#include "core/api/EngineRuntime.h"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/io/PathUtil.h"
#include "core/log/Logger.h"

namespace krkr::env {

  // 编码规则全引擎只该有一份实现，这里只是把名字引进来，不重复定义。
  using krkr::io::PathFromUtf8;
  using krkr::io::PathToUtf8;

  // Game path handling
  namespace {
    bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) {
      if (text.size() < suffix.size()) {
        return false;
      }

      text.remove_prefix(text.size() - suffix.size());
      for (std::size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char lhs = static_cast<unsigned char>(text[i]);
        const unsigned char rhs = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
          return false;
        }
      }

      return true;
    }

    // 目录形式统一成以 '/' 结尾的 UTF-8 字符串；空路径原样返回。
    std::string AsDirectory(const std::filesystem::path &path) {
      if (path.empty()) {
        return {};
      }
      std::string text = PathToUtf8(path);
      if (!text.empty() && text.back() != '/' && text.back() != '\\') {
        text.push_back('/');
      }
      return text;
    }

    bool LooksLikeArchivePath(std::string_view path) {
      constexpr std::string_view kArchiveExtensions[] = { ".xp3", ".zip", ".7z", ".tar" };
      for (const std::string_view extension : kArchiveExtensions) {
        if (EndsWithIgnoreCase(path, extension)) {
          return true;
        }
      }
      return false;
    }
  } // namespace 

  EngineRuntime::EngineRuntime() : startTime_(std::chrono::steady_clock::now()) {}

  EngineRuntime *EngineRuntime::Create() { return new EngineRuntime(); }

  void EngineRuntime::Destroy(EngineRuntime *engine) {
    delete engine;
    krkr::log::Logger::Instance().Flush();
  }

  EngineResult EngineRuntime::OpenGame(const char *gameRootUtf8) {
    if (gameRootUtf8 == nullptr || gameRootUtf8[0] == '\0') {
      lastError_ = "game root is empty";
      krkr::log::Error(lastError_);
      return EngineResult::InvalidArgument;
    }
    if (gameOpen_) {
      lastError_ = "game already open";
      krkr::log::Error(lastError_);
      return EngineResult::InvalidState;
    }

    const std::string requested = gameRootUtf8;
    const std::filesystem::path requestedPath = PathFromUtf8(requested);
    if (requestedPath.empty()) {
      lastError_ = "game root is not valid UTF-8";
      krkr::log::Error(lastError_);
      return EngineResult::InvalidArgument;
    }
    krkr::log::Info("OpenGame requested=", requested);

    std::error_code ec;

    const bool exists = std::filesystem::exists(requestedPath, ec);
    
    if (ec) {
      lastError_ = "cannot stat \"" + requested + "\": " + ec.message();
      krkr::log::Error(lastError_);
      return EngineResult::IoError;
    }

    if (!exists) {
      lastError_ = "\"" + requested + "\" does not exist";
      krkr::log::Error(lastError_);
      return EngineResult::IoError;
    }

    std::filesystem::path resolved = std::filesystem::weakly_canonical(requestedPath, ec);
    if (ec) {
      ec.clear();
      resolved = std::filesystem::absolute(requestedPath, ec);
      if (ec) {
        ec.clear();
        resolved = requestedPath;
      }
    }

    GameKind kind = GameKind::None;
    const bool isDirectory = std::filesystem::is_directory(resolved, ec);
    if (ec) {
      lastError_ = "cannot inspect \"" + requested + "\": " + ec.message();
      krkr::log::Error(lastError_);
      return EngineResult::IoError;
    }

    if (isDirectory) {
      kind = GameKind::Directory;
    } else if (LooksLikeArchivePath(PathToUtf8(resolved))) {
      kind = GameKind::Archive;
    }
    if (kind == GameKind::None) {
      lastError_ = "\"" + requested + "\" is neither a directory nor a known archive (.xp3/.zip/.7z/.tar)";
      krkr::log::Error(lastError_);
      return EngineResult::IoError;
    }

    std::string normalized = PathToUtf8(resolved);
    if (kind == GameKind::Directory && (normalized.empty() || normalized.back() != '/')) {
      normalized.push_back('/');
    }

    gameRoot_ = std::move(normalized);
    gameKind_ = kind;
    gameOpen_ = true;
    lastError_.clear();
    krkr::log::Info("OpenGame ok: root=", gameRoot_, " kind=", static_cast<int>(kind));

    RunStartupWork();
    krkr::log::Logger::Instance().Flush();
    if (GetStartupState() == EngineStartupState::Failed) {
      return EngineResult::IoError; // lastError_ 已由 RunStartupWork 写好
    }
    return EngineResult::Ok;
  }

  void EngineRuntime::RunStartupWork() {
    startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Running));
    krkr::log::Info("startup: running");

    storage_ = std::make_unique<krkr::io::Storage>(gameRoot_);
    if (!storage_->Mounted()) {
      lastError_ = storage_->GetLastErrorText();
      startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Failed));
      krkr::log::Error(lastError_);
      return;
    }
    krkr::log::Info("startup: storage mounted (archive=",
                    storage_->IsArchive() ? 1 : 0, ")");

    if (scriptRuntime_ != nullptr) {
      std::string error;
      if (!scriptRuntime_->Startup(storage_.get(), Services(), &error)) {
        FailStartup(std::string("script startup failed: ") + error);
        return;
      }
      if (!scriptRuntime_->RunStartupScript(&error)) {
        FailStartup(error);
        return;
      }
      startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Succeeded));
      krkr::log::Info("startup: script executed");
      return;
    }

    // 未挂接脚本运行时（早期测试台与存储层校验）只确认脚本可读。
    std::unique_ptr<krkr::io::IStream> stream = storage_->Open("startup.tjs");
    if (stream == nullptr) {
      lastError_ = storage_->GetLastErrorText();
      startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Failed));
      krkr::log::Error(lastError_);
      return;
    }

    const std::int64_t size = stream->GetSize();
    char head[64];
    const std::int64_t got = stream->Read(head, static_cast<std::int64_t>(sizeof(head)));
    if (got < 0) {
      lastError_ = "cannot read startup script";
      startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Failed));
      krkr::log::Error(lastError_);
      return;
    }

    std::string headText(head, static_cast<std::size_t>(got));
    for (char &ch : headText) {
      const unsigned char uc = static_cast<unsigned char>(ch);
      if (uc < 0x20 || uc > 0x7e) {
        ch = '.';
      }
    }

    startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Succeeded));
    krkr::log::Info("startup: script size=", size, " head=", headText);
  }


  bool EngineRuntime::AttachScriptRuntime(krkr::script::IScriptRuntime *runtime) {
    if (gameOpen_) {
      lastError_ = "script runtime must be attached before OpenGame";
      krkr::log::Error(lastError_);
      return false;
    }
    scriptRuntime_ = runtime;
    krkr::log::Info("host: script runtime attached=", runtime != nullptr);
    return true;
  }

  void EngineRuntime::DetachScriptRuntime() {
    if (scriptRuntime_ != nullptr) {
      scriptRuntime_->Shutdown();
      scriptRuntime_ = nullptr;
    }
  }

  void EngineRuntime::SetExecutablePath(const std::string &pathUtf8) {
    executablePath_ = pathUtf8;
  }

  void EngineRuntime::SetPersonalDirectory(const std::string &pathUtf8) {
    personalDirectory_ = pathUtf8;
  }

  void EngineRuntime::SetSavedGamesDirectory(const std::string &pathUtf8) {
    savedGamesDirectory_ = pathUtf8;
  }

  krkr::script::HostServices EngineRuntime::Services() {
    krkr::script::HostServices services;
    services.arguments = this;
    services.output = this;
    services.appControl = this;
    services.platform = this;
    services.paths = this;
    services.window = this;
    return services;
  }

  void EngineRuntime::SetLaunchArguments(std::vector<std::string> arguments) {
    launchArguments_ = std::move(arguments);
    krkr::log::Info("host: launch arguments count=", launchArguments_.size());
  }

  std::vector<std::string> EngineRuntime::GetLaunchArguments() const {
    return launchArguments_;
  }

  bool EngineRuntime::SetLaunchArgument(const std::string &key, const std::string &value) {
    std::string name = key;
    if (name.empty()) {
      return false;
    }
    if (name[0] != '-') {
      name.insert(name.begin(), '-');
    }

    // 一个参数占两位（"-name" "value"）或一位（"-name=value" / 无值开关），
    // 覆盖写入前把同名项及其值一起摘掉，避免 getArgument 读到旧值。
    std::vector<std::string> next;
    for (std::size_t i = 0; i < launchArguments_.size(); ++i) {
      const std::string &item = launchArguments_[i];
      if (item == name) {
        if (i + 1 < launchArguments_.size() && !launchArguments_[i + 1].empty() &&
            launchArguments_[i + 1][0] != '-') {
          ++i;
        }
        continue;
      }
      if (item.size() > name.size() && item.compare(0, name.size(), name) == 0 &&
          item[name.size()] == '=') {
        continue;
      }
      next.push_back(item);
    }
    next.push_back(name);
    if (!value.empty()) {
      next.push_back(value);
    }
    launchArguments_ = std::move(next);
    return true;
  }

  void EngineRuntime::Inform(const std::string &text) {
    krkr::log::Info("script: ", text);
  }

  void EngineRuntime::InformError(const std::string &text) {
    krkr::log::Error("script: ", text);
  }

  void EngineRuntime::RequestExit() {
    if (!exitRequested_.exchange(true)) {
      krkr::log::Info("script: exit requested");
    }
  }

  std::string EngineRuntime::GetOSName() const {
#if defined(_WIN32)
    return "Windows";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
  }

  std::string EngineRuntime::GetPlatformName() const {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "Unknown";
#endif
  }

  std::string EngineRuntime::GetVersionString() const {
    return "Krkr2Next (KiriKiri2 compatible runtime)";
  }

  std::int64_t EngineRuntime::GetTickCount() const {
    const auto elapsed = std::chrono::steady_clock::now() - startTime_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  }

  std::string EngineRuntime::GetExecutableDirectory() const {
    if (executablePath_.empty()) {
      return {};
    }
    const std::filesystem::path executable = PathFromUtf8(executablePath_);
    if (executable.empty()) {
      return {};
    }
    return AsDirectory(executable.parent_path());
  }

  std::string EngineRuntime::GetDataDirectory() const {
    if (gameRoot_.empty()) {
      return {};
    }
    const std::filesystem::path root = PathFromUtf8(gameRoot_);
    if (root.empty()) {
      return {};
    }
    // 包模式的数据目录是包所在的目录；目录模式就是游戏目录本身。
    return gameKind_ == GameKind::Archive ? AsDirectory(root.parent_path()) : AsDirectory(root);
  }

  std::string EngineRuntime::GetPersonalDirectory() const { return personalDirectory_; }

  std::string EngineRuntime::GetSavedGamesDirectory() const { return savedGamesDirectory_; }

  int EngineRuntime::GetWidth() const { return surfaceWidth_; }

  int EngineRuntime::GetHeight() const { return surfaceHeight_; }

  // 全屏运行：桌面原点为 0，桌面尺寸即表面尺寸。
  int EngineRuntime::GetDesktopLeft() const { return 0; }

  int EngineRuntime::GetDesktopTop() const { return 0; }

  int EngineRuntime::GetDesktopWidth() const { return surfaceWidth_; }

  int EngineRuntime::GetDesktopHeight() const { return surfaceHeight_; }

  void EngineRuntime::SetTitle(const std::string &title) {
    title_ = title;
    krkr::log::Info("host: title=\"", title_, "\"");
  }

  std::string EngineRuntime::GetTitle() const { return title_; }

  void EngineRuntime::SetSurfaceSize(int width, int height) {
    if (width > 0 && height > 0) {
      surfaceWidth_ = width;
      surfaceHeight_ = height;
    }
  }

  void EngineRuntime::FailStartup(const std::string &reason) {
    lastError_ = reason;
    startupState_.store(static_cast<std::uint32_t>(EngineStartupState::Failed));
    krkr::log::Error(lastError_);
  }

  EngineResult EngineRuntime::Tick(double deltaSeconds) {
    if (!gameOpen_) {
      lastError_ = "Tick before OpenGame";
      krkr::log::Error(lastError_);
      return EngineResult::InvalidState;
    }
    if (deltaSeconds < 0.0) {
      lastError_ = "negative delta";
      krkr::log::Error(lastError_);
      return EngineResult::InvalidArgument;
    }

    if (GetStartupState() != EngineStartupState::Succeeded) {
      lastError_ = "engine startup is still running";
      return EngineResult::StartupPending;
    }

    if (exitRequested_.load()) {
      lastError_ = "game requested exit";
      return EngineResult::GameTerminated;
    }

    ++frameCount_;
    krkr::log::Logger::Instance().SetFrame(frameCount_);
    if (scriptRuntime_ != nullptr) {
      scriptRuntime_->Tick(deltaSeconds);
    }
    // TODO(第 8 步): 接上渲染后去掉这条每帧日志，只保留状态边沿。
    krkr::log::Debug("tick dt=", deltaSeconds);
    krkr::log::Logger::Instance().Flush();
    return EngineResult::Ok;
  }
} // namespace krkr::env
