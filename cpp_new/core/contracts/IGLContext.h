#pragma once

#include "core/contracts/GLApi.h"

namespace krkr::render {
  // GL 上下文契约：桌面是 WGL，Android 是 EGL + ANativeWindow。
  //
  // 方向：core 定义 → platform 实现 → host/engine 使用。visual 只拿到 GLApi 表，
  // 因此不知道上下文来自哪个平台。
  class IGLContext {
    public:
      virtual ~IGLContext() = default;

      virtual bool IsValid() const = 0;
      virtual int GetWidth() const = 0;
      virtual int GetHeight() const = 0;

      // 把入口点装进 api；返回 false 表示驱动或上下文不完整。
      virtual bool LoadApi(GLApi &api) = 0;

      virtual bool MakeCurrent() = 0;
      virtual bool SwapBuffers() = 0;

      const char *GetLastErrorText() const { return lastError_; }

    protected:
      const char *lastError_ = "";
  };
} // namespace krkr::render
