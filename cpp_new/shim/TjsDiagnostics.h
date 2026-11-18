#pragma once

#include <string>

#include "tjs/tjsError.h"

namespace krkr::script {
  // 垫层内部的诊断设施，只有 shim/ 下的实现包含它。

  // TJS2 的内置消息在 vendored 源里全部声明为 NULL（tjsErrorInc.h 由上游生成，
  // 默认文本为空），异常到达宿主时消息恒为空串，无法定位。这里通过上游提供的
  // 运行期接口 TJSAssignMessage 填充消息表，避免修改 tjs/。
  void InstallTjsMessageMap();

  // 把脚本异常格式化为可定位的一行：调用上下文、消息、脚本名、行号与 trace。
  // context 形如 Scripts.execStorage("system/Initialize.tjs")。
  std::string DescribeScriptException(const TJS::eTJS &error, const std::string &context);

  // 以带上下文的 TJS 异常报告桥接层失败。异常由 TJS 虚拟机转成脚本异常，
  // 因此脚本侧看到的不是空消息。
  [[noreturn]] void ThrowScriptException(const std::string &context, const std::string &detail);
} // namespace krkr::script
