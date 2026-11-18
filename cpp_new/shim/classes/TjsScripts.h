#pragma once

namespace krkr::io {
  class Storage;
}

namespace krkr::script {
  class Engine;

  // 注册 TJS 侧 Scripts 类（eval / exec / evalStorage / execStorage / getTraceString）。
  // 需在 Engine 就绪后调用；storage 可为空，此时 *Storage 方法返回失败。
  void RegisterScriptsClass(Engine *engine, krkr::io::Storage *storage);
} // namespace krkr::script
