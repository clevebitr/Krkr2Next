#pragma once

#include <cstdio>
#include <memory>
#include <string>

#include "core/contracts/DebugServices.h"

namespace krkr::debugger {
  // 最小的观察平面实现：把事件按 JSON Lines 落盘，可选同时回显到 stderr。
  //
  // 刻意不含网络、线程与暂停语义：事件写得出来、事后能筛，就足以定位当前阶段的缺口。
  // 控制平面（断点/单步）以后作为新的 IDebugSink 兄弟接口接入，不改这里。
  class EventRecorder final : public krkr::debug::IDebugSink {
    public:
      EventRecorder() = default;
      ~EventRecorder() override;

      EventRecorder(const EventRecorder &) = delete;
      EventRecorder &operator = (const EventRecorder &) = delete;

      // 失败返回 false，错误文本进 GetLastErrorText()；调用方决定是否继续跑。
      bool Open(const std::string &pathUtf8);
      void Close();
      void SetEcho(bool echo) { echo_ = echo; }

      // 环境变量 KRKR_DEBUG_EVENTS 指向事件文件时打开它，否则返回空。
      // 两个宿主（产品入口 / 测试台）共用这段约定，避免各自解析参数。
      static std::unique_ptr<EventRecorder> FromEnvironment();

      void OnDebugEvent(const krkr::debug::DebugEvent &event) override;

      std::uint64_t GetCount() const { return count_; }
      const char *GetLastErrorText() const { return lastError_.c_str(); }

    private:
      std::FILE *file_ = nullptr;
      bool echo_ = false;
      std::uint64_t count_ = 0;
      std::string lastError_;
  };
} // namespace krkr::debugger
