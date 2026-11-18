#pragma once

#include <memory>

#include "core/contracts/IRenderTarget.h"

namespace krkr::platform {
  // Win32 窗口呈现目标：GDI 直通（StretchDIBits）。
  //
  // 只负责"开窗 + 提交像素 + 泵消息"，不做 GL；8c 换成 EGL/WGL 时本类被替换，
  // 渲染核心与 DrawList 不受影响。
  class Win32WindowTarget final : public krkr::render::IRenderTarget {
    public:
      Win32WindowTarget(const char *titleUtf8, int width, int height);
      ~Win32WindowTarget() override;

      Win32WindowTarget(const Win32WindowTarget &) = delete;
      Win32WindowTarget &operator = (const Win32WindowTarget &) = delete;

      bool IsValid() const override;
      int GetWidth() const override;
      int GetHeight() const override;

      bool Present(const std::uint32_t *pixels, int width, int height, int stride) override;
      bool PumpEvents() override;

    private:
      class Impl;
      std::unique_ptr<Impl> impl_;
  };
} // namespace krkr::platform
