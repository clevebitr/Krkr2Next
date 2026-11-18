#pragma once

#include <cstdint>
#include <vector>

namespace krkr::visual {
  // 解码后的图像：0xAARRGGBB，自上而下，与 Surface/Texture 的像素格式一致。
  // 解码器只产出这个结构，不接触 Storage、DrawList 或任何平台接口。
  struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    bool IsValid() const {
      return width > 0 && height > 0 &&
             pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
  };
} // namespace krkr::visual
