#pragma once

// 过渡期别名：混合操作的词汇表由 pixel/ 拥有（`krkr::pixel::LayerOp`），
// 合成层（visual）只是它的消费者。保留本头是为了让合成与断言代码继续用
// `krkr::visual::ltOpaque` 这类写法，不产生第二份定义。
//
// 新增/改值只改 pixel/LayerOps.h；本文件不复制任何常量。

#include "pixel/LayerOps.h"

namespace krkr::visual {
  // 词汇表（`ltBinder` … `ltPsExclusion`）、计数与查询函数在视觉层可见。
  using namespace krkr::pixel;

  // 旧名：合成层的代码里用 `LayerType` 表示"这一层怎么混合"。
  using LayerType = krkr::pixel::LayerOp;
} // namespace krkr::visual
