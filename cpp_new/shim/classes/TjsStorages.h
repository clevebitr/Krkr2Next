#pragma once

namespace krkr::io {
  class Storage;
}

namespace krkr::script {
  class Engine;

  // TJS 侧 Storages 类的注册入口。
  // 该方法由 Engine 在挂载存储后调用，重复调用以最后一次为准。
  void RegisterStoragesClass(Engine *engine, krkr::io::Storage *storage);
} // namespace krkr::script
