#pragma once

#include "core/contracts/IRenderTarget.h"

namespace krkr::platform {
  // Android 呈现目标。
  //
  // 8c 才接 ANativeWindow + EGL：在那之前 Present 返回 false 并记录一次原因，
  // 使"画面没出来"在日志里立刻可定位，而不是静默成功。
  class AndroidTarget final : public krkr::render::IRenderTarget {
    public:
      AndroidTarget() = default;
      ~AndroidTarget() override = default;

      bool IsValid() const override { return false; }
      int GetWidth() const override { return 0; }
      int GetHeight() const override { return 0; }

      bool Present(const std::uint32_t *pixels, int width, int height, int stride) override;
      bool PumpEvents() override { return true; }
  };
} // namespace krkr::platform
