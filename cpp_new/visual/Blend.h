#pragma once

#include <cstdint>

#include "visual/Surface.h"

namespace krkr::visual {
  // 单像素混合，软件合成与 Surface::Composite 共用同一份实现。
  // 语义见 docs/render-spec.md §3.1；定点规则集中在这里，改动必须同步断言。
  std::uint32_t BlendPixel(std::uint32_t dst, std::uint32_t src, LayerType type,
                           std::uint32_t alpha);

  // 逐像素直通 alpha 的插值（8 位，先乘后除、四舍五入）。
  std::uint32_t MulDiv255(std::uint32_t value, std::uint32_t scale);
} // namespace krkr::visual
