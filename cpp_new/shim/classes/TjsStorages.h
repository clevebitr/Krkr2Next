#pragma once

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 Storages 类（isExistentStorage / extractStorage* / addAutoPath 等）。
  void RegisterStoragesClass(const ScriptContext &context);
} // namespace krkr::script
