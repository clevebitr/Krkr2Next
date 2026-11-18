#pragma once

#include "core/contracts/IGLContext.h"

namespace krkr::platform {
  // 桌面 GL（Windows/WGL）上下文：隐藏窗口 + OpenGL 3.3 core。
  //
  // 服务 8b 的验证：给 GLBackend 一个可渲染的上下文与函数指针表。
  // Android 侧是 EGLContext，两者都实现 krkr::render::IGLContext。
  class GLContext final : public krkr::render::IGLContext {
    public:
      GLContext(int width, int height);
      ~GLContext() override;

      GLContext(const GLContext &) = delete;
      GLContext &operator = (const GLContext &) = delete;

      bool IsValid() const override;
      int GetWidth() const override { return width_; }
      int GetHeight() const override { return height_; }

      bool LoadApi(krkr::render::GLApi &api) override;
      bool MakeCurrent() override;
      bool SwapBuffers() override;

    private:
      class Impl;
      Impl *impl_ = nullptr;
      int width_ = 0;
      int height_ = 0;
      const char *lastError_ = "";
  };
} // namespace krkr::platform
