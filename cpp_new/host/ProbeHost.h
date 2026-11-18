#pragma once

// 测试台共用的宿主实现。
//
// tjs_probe / window_probe 等断言程序都以同一份宿主服务启动引擎：
// 这里实现 core/contracts/ScriptServices.h 的全部小接口，加上读文件与
// "抛出 <name>-ok 表示通过" 的断言约定。产品入口（core/api/EngineRuntime）
// 有自己的一份实现，两者不共享代码，共享的是同一份契约。

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

namespace krkr::hostprobe {

// 断言脚本默认使用的启动参数（t_system.tjs 依赖这两组键值）。
inline const char *const kDefaultArguments[] = {"-debugwin", "no", "-probe", "1"};

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

    int GetWidth() const override { return width_; }
    int GetHeight() const override { return height_; }
    int GetDesktopLeft() const override { return 0; }
    int GetDesktopTop() const override { return 0; }
    int GetDesktopWidth() const override { return width_; }
    int GetDesktopHeight() const override { return height_; }
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
    void SetSurfaceSize(int width, int height) {
      width_ = width;
      height_ = height;
    }

  private:
    std::vector<std::string> arguments_;
    std::string executablePath_;
    std::string dataDirectory_;
    krkr::debug::IDebugSink *debugSink_ = nullptr;
    std::string title_;
    int width_ = 1280;
    int height_ = 720;
    bool exitRequested_ = false;
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
};

inline bool ReadWholeFile(const char *path, std::string &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

// 路径文件避免 Windows 命令行 ANSI 代码页破坏非 ASCII 路径（技术债 #21）。
inline bool ReadRootPath(const char *path, std::string &out) {
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
inline bool IsAssertionPass(const std::string &message) {
  return message.find("-ok") != std::string::npos;
}

} // namespace krkr::hostprobe
