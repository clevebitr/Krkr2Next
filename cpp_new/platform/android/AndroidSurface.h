#pragma once

#include "core/contracts/IGLContext.h"

// JNI 层（bridge/engine_api）与引擎之间的最小接口。
//
// Java 侧的 Surface 生命周期：surfaceCreated → attach；surfaceChanged → resize；
// surfaceDestroyed → detach。引擎只持有裸指针，不把 ANativeWindow 泄漏到上层。
extern "C" {
  // 返回 false 表示 EGL 初始化失败（原因写日志）。
  bool krkr_android_surface_attach(void *nativeWindow, int width, int height);
  bool krkr_android_surface_resize(int width, int height);
  void krkr_android_surface_detach(void);
  bool krkr_android_surface_valid(void);
}

namespace krkr::platform {
  // 当前 GL 上下文（未 attach 时为 nullptr）。渲染线程用它 MakeCurrent / SwapBuffers。
  krkr::render::IGLContext *GetAndroidGLContext();
} // namespace krkr::platform
