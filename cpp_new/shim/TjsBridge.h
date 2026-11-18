#pragma once

namespace krkr::io {
  class Storage;
}

namespace krkr::script {
  class Engine;
  class IScriptHost;

  // 原生类共享的上下文。
  // tjs2 的静态方法无法携带实例状态，且 KiriKiri 的脚本层本身为进程内单实例，
  // 故以单一上下文承载引擎与存储引用；新增原生类时从此处取依赖。
  struct Bridge {
    Engine *engine = nullptr;
    krkr::io::Storage *storage = nullptr;
    IScriptHost *host = nullptr;
  };

  Bridge &GetBridge();
} // namespace krkr::script
