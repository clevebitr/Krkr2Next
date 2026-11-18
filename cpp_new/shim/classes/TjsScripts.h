#pragma once

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 Scripts 类（eval / exec / evalStorage / execStorage / getTraceString）。
  void RegisterScriptsClass(const ScriptContext &context);
} // namespace krkr::script
