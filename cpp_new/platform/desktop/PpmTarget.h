#pragma once

#include <string>

#include "core/contracts/IRenderTarget.h"

namespace krkr::platform {
  // 离屏呈现目标：把每帧写成 PPM（P6）。
  //
  // 用途：CI 与没有显示设备的环境里验证合成结果；也是"同一份 DrawList 出图"的最小闭环。
  class PpmTarget final : public krkr::render::IRenderTarget {
    public:
      explicit PpmTarget(const std::string &pathUtf8, int width, int height);
      ~PpmTarget() override = default;

      bool IsValid() const override { return true; }
      int GetWidth() const override { return width_; }
      int GetHeight() const override { return height_; }

      bool Present(const std::uint32_t *pixels, int width, int height, int stride) override;
      bool PumpEvents() override { return true; }

      const std::string &GetLastPath() const { return path_; }

    private:
      std::string path_;
      int width_ = 0;
      int height_ = 0;
  };
} // namespace krkr::platform
