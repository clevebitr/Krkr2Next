#pragma once

namespace krkr::script {
  class Engine;
  class IScriptHost;

  // 注册 TJS 侧 System 类：getArgument / inform / informError / exit，
  // 以及 title / versionInformation / desktopWidth / desktopHeight 属性。
  void RegisterSystemClass(Engine *engine, IScriptHost *host);
} // namespace krkr::script
