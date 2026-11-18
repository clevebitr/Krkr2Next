#pragma once

#include <string>

namespace TJS {
  class iTJSDispatch2;
  class tTJSVariant;
} // namespace TJS

namespace krkr::script {
  class Engine;

  enum class ScriptMode {
    Execute, // ExecScript：普通语句
    Evaluate // EvalExpression：求值表达式
  };

  // 脚本装载的唯一入口：识别 TJS2 字节码与文本编码、执行、异常带上下文格式化后重抛。
  //
  // 任何新的脚本入口（Scripts、KAGParser、插件脚本、崩溃报告）都走这里，
  // 不要在调用点自行组合「读取 + 解码 + ExecScript」，否则编码与字节码语义会分叉。
  void RunScriptBytes(Engine &engine, const std::string &bytes,
                      const std::string &scriptName, const std::string &context,
                      ScriptMode mode, TJS::tTJSVariant *result, TJS::iTJSDispatch2 *scope);

  // 从当前存储读取并执行。caller 形如 "Scripts.execStorage"，用于拼出定位上下文；
  // 打开或读取失败抛带上下文的脚本异常。
  void RunScriptFromStorage(Engine &engine, const std::string &name, ScriptMode mode,
                            const char *caller, TJS::tTJSVariant *result,
                            TJS::iTJSDispatch2 *scope);
} // namespace krkr::script
