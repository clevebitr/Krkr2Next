#pragma once

#include "core/contracts/ScriptServices.h"

namespace krkr::io {
  class Storage;
}

namespace krkr::script {
  class Engine;

  // 原生类共享的装配上下文。
  //
  // tjs2 的静态方法无法携带实例状态，且 KiriKiri 的脚本层本身为进程内单实例，
  // 因此装配出一份上下文由桥接实现统一读取。写入点只有一处：装配层
  // （cpp_new/engine/ScriptRuntime）在 Startup 时调用 SetContext，原生类只读。
  struct ScriptContext {
    Engine *engine = nullptr;
    krkr::io::Storage *storage = nullptr;
    HostServices services;
  };

  ScriptContext &GetContext();
  void SetContext(const ScriptContext &context);
} // namespace krkr::script
