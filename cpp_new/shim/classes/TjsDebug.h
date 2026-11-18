#pragma once

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 Debug 类（message / notice / startLogToFile / logAsError 等），
  // 以及通过 Debug.console / Debug.controller 暴露的 Console、Controller 类对象。
  // 输出映射到 core/log；控制台窗口在当前平台不存在，相关属性显式记录后忽略。
  void RegisterDebugClass(const ScriptContext &context);
} // namespace krkr::script
