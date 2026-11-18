#pragma once
// 上游像素函数（vendor/krkrz/visual/tvpgl.h 的导出表）。
//
// 唯一的入口头：上游头在非 Windows 下缺一组声明宏（只在 `#ifdef _WIN32` 里定义），
// 这里先补齐再包含，使上游文件保持零修改。我们的代码不要直接 include "tvpgl.h"。
//
// 导出的是**函数指针**，必须调用 `krkr::visual::InitializeBlendMath()` 之后才可用。

// 上游 tvpgl.h 假定调用方先包含 tjsTypes.h（它自己只用 tjs_* 类型），保持同一顺序。
#include "tjsTypes.h"

#include "vendor/krkrz/visual/porting/toolchain_compat.h"

#include "tvpgl.h"
