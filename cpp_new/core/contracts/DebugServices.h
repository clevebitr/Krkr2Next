#pragma once

#include <cstdint>
#include <string>

namespace krkr::debug {
  // 调试事件：下层只负责"发生了什么"，怎么展示、怎么落盘由 debugger/ 决定。
  // 字段保持扁平，新增能力时用 channel + 新 EventKind 扩展，不改已有结构。
  enum class EventKind {
    EngineState,   // detail = 新状态名
    EngineFailure, // name = 子系统，detail = 原因
    ScriptBegin,   // name = 脚本名，size = 字节数，ok = 是否字节码
    ScriptEnd,     // name = 脚本名，size = 耗时毫秒
    ScriptFailure, // name = 脚本名，line，detail = 带定位的消息
    StorageMount,  // name = 路径，detail = 后端，size = 条目数
    StorageOpen,   // name = 名字，detail = 命中后端，size = 大小，ok = 成功
    LogLine,       // channel = 日志级别，detail = 行文本
    ExitRequested, // 脚本请求退出
  };

  inline const char *EventKindName(EventKind kind) {
    switch (kind) {
      case EventKind::EngineState: return "engine.state";
      case EventKind::EngineFailure: return "engine.failure";
      case EventKind::ScriptBegin: return "script.begin";
      case EventKind::ScriptEnd: return "script.end";
      case EventKind::ScriptFailure: return "script.fail";
      case EventKind::StorageMount: return "storage.mount";
      case EventKind::StorageOpen: return "storage.open";
      case EventKind::LogLine: return "log.line";
      case EventKind::ExitRequested: return "exit.requested";
    }
    return "unknown";
  }

  struct DebugEvent {
    EventKind kind = EventKind::LogLine;
    std::uint64_t frame = 0;
    std::string channel;
    std::string name;
    std::string detail;
    int line = 0;
    std::int64_t size = -1;
    bool ok = true;
  };

  // 观察平面：实现方（debugger/agent）决定记录、筛选、转发。
  class IDebugSink {
    public:
      virtual ~IDebugSink() = default;
      virtual void OnDebugEvent(const DebugEvent &event) = 0;
  };

  // 未挂接调试器时 sink 为空，调用点必须只做一次空指针检查。
  inline void Emit(IDebugSink *sink, DebugEvent event) {
    if (sink != nullptr) {
      sink->OnDebugEvent(event);
    }
  }
} // namespace krkr::debug
