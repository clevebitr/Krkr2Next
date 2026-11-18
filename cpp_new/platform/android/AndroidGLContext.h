#pragma once

#include "core/contracts/IGLContext.h"

namespace krkr::platform {
  // Android GL 上下文：EGL + ANativeWindow。
  //
  // 窗口来自 Java `Surface`（JNI 层转成 ANativeWindow*），本类只认裸指针，
  // 因此不依赖 JNI 头；生命周期由 AndroidSurface 统一管理。
  class AndroidGLContext final : public krkr::render::IGLContext {
    public:
      AndroidGLContext(void *nativeWindow, int width, int height);
      ~AndroidGLContext() override;

      AndroidGLContext(const AndroidGLContext &) = delete;
      AndroidGLContext &operator = (const AndroidGLContext &) = delete;

      bool IsValid() const override;
      int GetWidth() const override { return width_; }
      int GetHeight() const override { return height_; }

      bool LoadApi(krkr::render::GLApi &api) override;
      bool MakeCurrent() override;
      bool SwapBuffers() override;

      // 尺寸变化（旋转/分屏）时重建 EGLSurface；返回 false 表示需要上层重来。
      bool Resize(int width, int height);

    private:
      class Impl;
      Impl *impl_ = nullptr;
      int width_ = 0;
      int height_ = 0;
  };
} // namespace krkr::platform
