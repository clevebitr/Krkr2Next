#pragma once

#include "tjs/tjsNative.h"

namespace krkr::script {
  class Engine;

  // 把原生类对象挂到 TJS 全局对象上。
  //
  // 原生类注册的样板（构造 → tTJSVariant(cls, nullptr) → PropSet → Release）
  // 集中在这里：objthis 必须为空，否则脚本保存方法后经别名调用会失去上下文。
  bool ExposeNativeClass(Engine *engine, const tjs_char *name, TJS::tTJSNativeClass *nativeClass);
} // namespace krkr::script
