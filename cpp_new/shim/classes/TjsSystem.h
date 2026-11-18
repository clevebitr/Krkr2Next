#pragma once

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 System 类。
  // 成员通过 ScriptContext.services 取得宿主能力；能力缺失时显式失败，不用默认值。
  void RegisterSystemClass(const ScriptContext &context);
} // namespace krkr::script
