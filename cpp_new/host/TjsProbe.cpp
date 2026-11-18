#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/contracts/ScriptServices.h"
#include "core/io/PathUtil.h"
#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "debugger/agent/EventRecorder.h"
#include "engine/ScriptRuntime.h"
#include "shim/classes/TjsPlugins.h"
#include "shim/engine/TjsEngine.h"

// TJS2 测试台：宿主角色的最小实现。
//
//   tjs_probe <script.tjs> <root-path-file> [launch arguments...]
//
// 装配（引擎 + 原生类 + 兼容声明）由 cpp_new/engine 的 ScriptRuntime 完成，
// 与产品入口共用同一条链路；本文件只提供宿主能力与断言约定：
// 脚本正常结束、或抛出的异常消息含有 "-ok"，视为通过。

namespace {

// 断言脚本默认使用的启动参数（t_system.tjs 依赖这两组键值）。
const char *const kDefaultArguments[] = { "-debugwin", "no", "-probe", "1" };

class ProbeHost final : public krkr::script::ILaunchArguments,
                        public krkr::script::IOutput,
                        public krkr::script::IAppControl,
                        public krkr::script::IPlatformInfo,
                        public krkr::script::IPathProvider,
                        public krkr::script::IWindowInfo {
  public:
    ProbeHost(int argc, char **argv) {
      if (argc > 3) {
        for (int i = 3; i < argc; ++i) {
          arguments_.emplace_back(argv[i]);
        }
        return;
      }
      for (const char *item : kDefaultArguments) {
        arguments_.emplace_back(item);
      }
    }

    krkr::script::HostServices Services() {
      krkr::script::HostServices services;
      services.arguments = this;
      services.output = this;
      services.appControl = this;
      services.platform = this;
      services.paths = this;
      services.window = this;
      services.debug = debugSink_;
      return services;
    }

    std::vector<std::string> GetLaunchArguments() const override { return arguments_; }

    bool SetLaunchArgument(const std::string &key, const std::string &value) override {
      arguments_.push_back(key);
      if (!value.empty()) {
        arguments_.push_back(value);
      }
      return true;
    }

    void Inform(const std::string &text) override {
      std::fprintf(stdout, "[script] %s\n", text.c_str());
      std::fflush(stdout);
    }

    void InformError(const std::string &text) override {
      std::fprintf(stderr, "[script:error] %s\n", text.c_str());
      std::fflush(stderr);
    }

    void RequestExit() override { exitRequested_ = true; }

    std::string GetOSName() const override { return "probe"; }
    std::string GetPlatformName() const override { return "probe"; }
    std::string GetVersionString() const override { return "cpp_new probe"; }
    std::int64_t GetTickCount() const override {
      const auto elapsed = std::chrono::steady_clock::now() - startTime_;
      return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    // 与 EngineRuntime 同语义：可执行文件目录以分隔符结尾，数据目录是游戏根。
    std::string GetExecutableDirectory() const override {
      const std::filesystem::path executable = krkr::io::PathFromUtf8(executablePath_);
      if (executable.empty()) {
        return {};
      }
      std::string directory = krkr::io::PathToUtf8(executable.parent_path());
      if (!directory.empty() && directory.back() != '/' && directory.back() != '\\') {
        directory.push_back('/');
      }
      return directory;
    }
    std::string GetDataDirectory() const override { return dataDirectory_; }
    std::string GetPersonalDirectory() const override { return {}; }
    std::string GetSavedGamesDirectory() const override { return {}; }

    int GetWidth() const override { return 1280; }
    int GetHeight() const override { return 720; }
    int GetDesktopLeft() const override { return 0; }
    int GetDesktopTop() const override { return 0; }
    int GetDesktopWidth() const override { return 1280; }
    int GetDesktopHeight() const override { return 720; }
    std::string GetTitle() const override { return title_; }
    void SetTitle(const std::string &title) override { title_ = title; }

    bool IsExitRequested() const { return exitRequested_; }

    void SetDebugSink(krkr::debug::IDebugSink *sink) { debugSink_ = sink; }

    void SetExecutablePath(const std::string &path) { executablePath_ = path; }
    void SetDataDirectory(const std::string &path) {
      dataDirectory_ = path;
      if (!dataDirectory_.empty() && dataDirectory_.back() != '/' &&
          dataDirectory_.back() != '\\') {
        dataDirectory_.push_back('/');
      }
    }

  private:
    std::vector<std::string> arguments_;
    std::string executablePath_;
    std::string dataDirectory_;
    krkr::debug::IDebugSink *debugSink_ = nullptr;
    std::string title_;
    bool exitRequested_ = false;
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
};

bool ReadWholeFile(const char *path, std::string &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

// 路径文件避免 Windows 命令行 ANSI 代码页破坏非 ASCII 路径（技术债 #21）。
bool ReadRootPath(const char *path, std::string &out) {
  std::string bytes;
  if (!ReadWholeFile(path, bytes)) {
    return false;
  }
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf) {
    bytes.erase(0, 3);
  }
  while (!bytes.empty() && (bytes.back() == '\r' || bytes.back() == '\n' ||
                            bytes.back() == ' ' || bytes.back() == '\t')) {
    bytes.pop_back();
  }
  out = bytes;
  return true;
}

// 断言脚本以抛出 "<name>-ok" 结束；失败路径的异常文本不含这个标记。
bool IsAssertionPass(const std::string &message) {
  return message.find("-ok") != std::string::npos;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: tjs_probe <script.tjs> <root-path-file> [args...]\n");
    return 2;
  }

  std::string script;
  if (!ReadWholeFile(argv[1], script)) {
    std::fprintf(stderr, "cannot read script: %s\n", argv[1]);
    return 2;
  }

  std::string root;
  if (!ReadRootPath(argv[2], root) || root.empty()) {
    std::fprintf(stderr, "cannot read root path file: %s\n", argv[2]);
    return 2;
  }

  krkr::io::Storage storage(root);
  if (!storage.Mounted()) {
    std::fprintf(stderr, "cannot mount %s: %s\n", root.c_str(), storage.GetLastErrorText());
    return 2;
  }

  std::printf("script   = %s\n", argv[1]);
  std::printf("root     = %s\n", root.c_str());
  std::printf("bytes    = %zu\n", script.size());

  // 事件记录由环境变量开启（KRKR_DEBUG_EVENTS=<file>），不影响正常路径。
  const std::unique_ptr<krkr::debugger::EventRecorder> recorder =
      krkr::debugger::EventRecorder::FromEnvironment();
  if (recorder != nullptr) {
    krkr::log::Logger::Instance().SetDebugSink(recorder.get());
    storage.SetDebugSink(recorder.get());
  }

  // 断言用的静态插件：覆盖 Plugins.link 的命中路径（注册表与 TJS 无关）。
  krkr::script::RegisterStaticPlugin("probe_static.dll");

  ProbeHost host(argc, argv);
  host.SetExecutablePath(argv[0]);
  host.SetDataDirectory(root);
  if (recorder != nullptr) {
    host.SetDebugSink(recorder.get());
  }
  krkr::engine::ScriptRuntime runtime;
  std::string error;
  if (!runtime.Startup(&storage, host.Services(), &error)) {
    std::fprintf(stderr, "script runtime startup failed: %s\n", error.c_str());
    return 2;
  }

  const bool ok = runtime.GetEngine()->RunScript(script, argv[1], &error);
  if (ok) {
    std::printf("exec     = ok\n");
  } else {
    std::printf("exec     = script exception: %s\n", error.c_str());
  }
  if (host.IsExitRequested()) {
    std::printf("exit     = requested\n");
  }

  krkr::log::Logger::Instance().Flush();
  if (ok) {
    return 0;
  }
  return IsAssertionPass(error) ? 0 : 1;
}
