#pragma once

#include <string>

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 Plugins 类（link / unlink / getList）。
  void RegisterPluginsClass(const ScriptContext &context);

  // 静态插件登记入口：插件以静态库形式接入引擎时，初始化期登记其名字，
  // 之后脚本侧 Plugins.link 才能解析到它。名字按不区分大小写的基名比较。
  void RegisterStaticPlugin(const std::string &name);
} // namespace krkr::script
