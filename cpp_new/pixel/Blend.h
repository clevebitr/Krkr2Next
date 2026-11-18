#pragma once

#include <cstdint>

#include "pixel/LayerOps.h"

namespace krkr::pixel {
  // 单像素混合。
  //
  // 语义由 vendor/krkrz（krkrz 的 tvpgl）决定：29 种算子的公式、定点舍入、
  // opacity 的施加方式都是上游实现。本层只做「操作 → 上游函数」的映射与类型转换，
  // 不再自己写一遍混合数学（见 docs/visual-vendor.md）。
  //
  // 约定：
  //   - dst / src 均为 0xAARRGGBB 直通 alpha；
  //   - dst 的 alpha 恒为 0xFF（合成表面的约定），因此使用上游不带 `_d`/`_a` 的变体；
  //   - opacity 取 0..255，越界按 255 处理；
  //   - 不产生绘制项的操作（`ltBinder`/`ltEffect`/`ltFilter`）原样返回 dst。
  std::uint32_t BlendPixel(std::uint32_t dst, std::uint32_t src, LayerOp op,
                           std::uint32_t opacity);

  // 初始化上游像素函数表（幂等）。首次 BlendPixel 会自动调用；
  // 显式调用用于测试里把「初始化失败」与「算子算错」分开。
  void InitializeBlendRoutines();

  // 上游函数表是否已初始化。
  bool AreBlendRoutinesReady();
} // namespace krkr::pixel
