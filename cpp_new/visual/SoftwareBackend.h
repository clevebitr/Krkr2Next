#pragma once

#include <cstddef>
#include <cstdint>

#include "visual/DrawList.h"
#include "visual/Surface.h"

namespace krkr::visual {
  struct RenderStats {
    std::size_t itemsDrawn = 0;
    std::size_t pixelsWritten = 0;
    std::size_t itemsSkipped = 0;  // 尺寸为 0、被裁剪掉，或 8a 尚不支持的形状
  };

  // 软件合成后端：像素权威，用来给 GL 后端做参照，也用来在没有 GPU 的环境里出图。
  //
  // 8a 的后端只保证两件事：
  //   1. 轴对齐的四边形（纯色或纹理，可缩放）按 §3.1 的算子合成；
  //   2. 越界与 clip 一律裁剪，绝不越界写。
  // 有旋转/斜切（顶点非轴对齐）的项会被跳过并计入 itemsSkipped，而不是画错。
  bool Render(const DrawList &list, Surface &target, RenderStats *stats = nullptr);
} // namespace krkr::visual
