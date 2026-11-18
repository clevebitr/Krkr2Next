#pragma once

#include <string>

#include "core/contracts/ScriptServices.h"

namespace krkr::io {
  class Storage;
}

namespace krkr::script {
  // 脚本运行时对引擎的契约。
  //
  // 方向：core 定义、装配层（cpp_new/engine）实现，使 core/api 不必引用 shim 或 tjs。
  // 引擎只负责判断启动脚本是否存在、把存储与宿主能力交出去；脚本怎么执行由装配层决定。
  class IScriptRuntime {
    public:
      virtual ~IScriptRuntime() = default;

      // 存储挂载完成后调用一次：装配引擎与原生类，执行兼容层前奏脚本。
      virtual bool Startup(krkr::io::Storage *storage, const HostServices &services,
                           std::string *errorUtf8) = 0;

      // 执行启动脚本（startup.tjs）。
      virtual bool RunStartupScript(std::string *errorUtf8) = 0;

      // 每帧推进（脚本侧事件与定时器；当前为空实现）。
      virtual void Tick(double deltaSeconds) = 0;

      // 释放脚本引擎持有的脚本侧状态。
      virtual void Shutdown() = 0;
  };
} // namespace krkr::script
