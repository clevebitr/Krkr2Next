#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace krkr::script {
  // 脚本层需要宿主提供的能力，按用途拆成小接口。
  //
  // 约定：能力缺失用 HostServices 中的空指针表达，桥接层必须显式失败（抛带上下文的
  // 异常），不得用默认值掩盖「未实现」。因此这里的接口方法一律纯虚。

  class ILaunchArguments {
    public:
      virtual ~ILaunchArguments() = default;
      virtual std::vector<std::string> GetLaunchArguments() const = 0;
      // 返回 false 表示宿主不接受写入。
      virtual bool SetLaunchArgument(const std::string &key, const std::string &value) = 0;
  };

  class IOutput {
    public:
      virtual ~IOutput() = default;
      virtual void Inform(const std::string &text) = 0;
      virtual void InformError(const std::string &text) = 0;
  };

  class IAppControl {
    public:
      virtual ~IAppControl() = default;
      virtual void RequestExit() = 0;
  };

  class IPlatformInfo {
    public:
      virtual ~IPlatformInfo() = default;
      virtual std::string GetOSName() const = 0;
      virtual std::string GetPlatformName() const = 0;
      virtual std::string GetVersionString() const = 0;
      // 单调递增的毫秒数，进程启动为 0（对应 System.getTickCount）。
      virtual std::int64_t GetTickCount() const = 0;
  };

  class IPathProvider {
    public:
      virtual ~IPathProvider() = default;
      // 可执行文件所在目录，以分隔符结尾（System.exePath / System.arcPath）。
      virtual std::string GetExecutableDirectory() const = 0;
      // 游戏数据目录，以 / 结尾（System.dataPath）。
      virtual std::string GetDataDirectory() const = 0;
      virtual std::string GetPersonalDirectory() const = 0;
      virtual std::string GetSavedGamesDirectory() const = 0;
  };

  class IWindowInfo {
    public:
      virtual ~IWindowInfo() = default;
      virtual int GetWidth() const = 0;
      virtual int GetHeight() const = 0;
      virtual int GetDesktopLeft() const = 0;
      virtual int GetDesktopTop() const = 0;
      virtual int GetDesktopWidth() const = 0;
      virtual int GetDesktopHeight() const = 0;
      virtual std::string GetTitle() const = 0;
      virtual void SetTitle(const std::string &title) = 0;
  };

  // 宿主能力集合。空指针 = 该能力未提供。
  struct HostServices {
    ILaunchArguments *arguments = nullptr;
    IOutput *output = nullptr;
    IAppControl *appControl = nullptr;
    IPlatformInfo *platform = nullptr;
    IPathProvider *paths = nullptr;
    IWindowInfo *window = nullptr;
  };
} // namespace krkr::script
