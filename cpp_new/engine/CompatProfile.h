#pragma once

#include <string>
#include <utility>
#include <vector>

#include "shim/TjsBridge.h"

namespace krkr::engine {
  // 兼容层能力声明：只声明本引擎对外声称支持什么，不实现任何能力。
  //
  // 声明方式决定脚本走哪条分支（例如 `kirikiriz` 预处理器值）。把"声称支持什么"
  // 集中在一处，比让脚本逐成员探测、再由桥接层逐个补洞可控得多。
  struct Profile {
    const char *name = "";
    // 预处理器值（脚本的 @if(...)）。
    std::vector<std::pair<std::string, int>> preprocessorValues;
    // 注册哪些原生类，按顺序调用。
    std::vector<void (*)(const krkr::script::ScriptContext &)> nativeClasses;
    // 注册完成后执行的脚本前奏（兼容层提供的脚本级 stub）。
    std::string preludeScript;
  };

  // KiriKiri2 兼容声明：不声称 KiriKiriZ 扩展，脚本因此走 k2compat 分支。
  Profile MakeKiriKiri2Profile();
} // namespace krkr::engine
