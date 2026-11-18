#pragma once

#include "core/contracts/GLApi.h"

namespace krkr::platform {
  // 桌面 GL（Windows/WGL）上下文：隐藏窗口 + OpenGL 3.3 core。
  //
  // 只服务 8b 的验证：给 GLBackend 一个可渲染的上下文与函数指针表。
  // 8c 的 Android 侧换成 EGL，接口（上下文 + GLApi）保持不变。
  class GLContext final {
    public:
      GLContext(int width, int height);
      ~GLContext();

      GLContext(const GLContext &) = delete;
      GLContext &operator = (const GLContext &) = delete;

      bool IsValid() const;
      const char *GetLastErrorText() const { return lastError_; }

      // 载入入口点到 api；返回 false 表示驱动/上下文不完整。
      bool LoadApi(krkr::render::GLApi &api);

      bool MakeCurrent();
      bool SwapBuffers();

      int GetWidth() const { return width_; }
      int GetHeight() const { return height_; }

    private:
      class Impl;
      Impl *impl_ = nullptr;
      int width_ = 0;
      int height_ = 0;
      const char *lastError_ = "";
  };
} // namespace krkr::platform
