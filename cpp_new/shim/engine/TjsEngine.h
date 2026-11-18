#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace krkr::script {
  // TJS2 脚本引擎门面。
  //
  // 设计约束（重要）：
  //   1. **上层不接触 tjs2 的任何类型**。这个头文件只暴露 std::string / std::string_view，
  //      不 include 任何 core/tjs2/* 的头 —— 换实现（换 TJS 版本、换语言）时只改 .cpp。
  //   2. **不修改权威实现**。core/tjs2/ 是 krkrz 原样拷入（逐字节一致），
  //      所有平台适配都放在本文件的 .cpp 里（垫层），通过宏/包含顺序/转换函数解决。
  //   3. 文本约定：对外一律 UTF-8（`std::string`），进入 tjs2 才转成它内部的宽字符。
  class Engine final {
    public:
      Engine();
      ~Engine();

      Engine(const Engine &) = delete;
      Engine &operator = (const Engine &) = delete;

      bool IsReady() const;

      // 执行一段脚本。sourceUtf8 允许带 UTF-16LE BOM（KiriKiri 的 .tjs 就是），
      // 带了会自动识别。失败返回 false，原因写进 errorOutUtf8。
      bool RunScript(std::string_view sourceUtf8, const char *scriptName,
                     std::string *errorOutUtf8);

      // 从文件读脚本并执行（测试台用；引擎内部走存储层）。
      bool RunScriptFile(const char *pathUtf8, std::string *errorOutUtf8);

      // 暴露 TJS 全局对象，供垫层注册原生类使用；上层代码不应直接使用。
      void *GetGlobalObjectRaw();

      // 返回 tTJS 实例。仅限 core/script 内部的桥接实现使用。
      void *GetVMScriptEngineRaw();

      // 脚本里 System.inform / print 等输出会走到这里（已含换行符）。
      void SetPrinter(void (*printer)(const char *lineUtf8, void *user), void *user);

    private:
      class Impl;
      std::unique_ptr<Impl> impl_;
  };
} // namespace krkr::script
