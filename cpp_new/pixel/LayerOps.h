#pragma once

#include <cstdint>

namespace krkr::pixel {
  // 图层混合操作（KiriKiri 的图层类型常量）。
  //
  // 数值与上游一致：脚本里 `Layer.type = 3` 这样的数字直接落在这里，所以这是
  // "操作 → 行为" 的唯一词汇表。名字保留 KiriKiri 文档的写法（`ltOpaque` …），
  // 便于与 `docs/render-spec.md` §3 对照；本层不引入任何上游前缀或未定义术语。
  enum LayerOp : std::uint32_t {
    ltBinder = 0,  // 只画子层，自己不出绘制项
    ltOpaque = 1,  // 旧名 ltCoverRect
    ltAlpha = 2,   // 旧名 ltTransparent
    ltAdditive = 3,
    ltSubtractive = 4,
    ltMultiplicative = 5,
    ltEffect = 6,  // 子层合成后再整体作用，需要离屏（未实现）
    ltFilter = 7,  // 同上，可带滤镜参数（未实现）
    ltDodge = 8,
    ltDarken = 9,
    ltLighten = 10,
    ltScreen = 11,
    ltAddAlpha = 12,
    ltPsNormal = 13,
    ltPsAdditive = 14,
    ltPsSubtractive = 15,
    ltPsMultiplicative = 16,
    ltPsScreen = 17,
    ltPsOverlay = 18,
    ltPsHardLight = 19,
    ltPsSoftLight = 20,
    ltPsColorDodge = 21,
    ltPsColorDodge5 = 22,
    ltPsColorBurn = 23,
    ltPsLighten = 24,
    ltPsDarken = 25,
    ltPsDifference = 26,
    ltPsDifference5 = 27,
    ltPsExclusion = 28,
  };

  constexpr std::uint32_t kLayerOpCount = 29;

  // 稳定短名（`"Opaque"`、`"PsOverlay"` …），用于日志、断言输出与 golden 表的键。
  // 未知值返回 `"Unknown"`（不做临时缓冲，便于直接比较）。
  const char *LayerOpName(std::uint32_t op);

  // 本层自己是否需要一条绘制路径。`ltBinder` 只画子层；`ltEffect`/`ltFilter` 需要离屏，
  // 在 8d 之前不产生绘制项（跳过并计数，而不是画错）。
  bool IsLayerOpDrawable(std::uint32_t op);

  // 混合公式是否读取源像素的 alpha。集合与上游 `TVPIsTypeUsingAlpha` 一致：
  // `ltAddAlpha` 走预乘路径不在其中；`ltOpaque`/`ltAdditive`/`ltSubtractive`/
  // `ltMultiplicative`/`ltDodge`/`ltDarken`/`ltLighten`/`ltScreen` 忽略源 alpha。
  bool IsLayerOpAlphaBlended(std::uint32_t op);

  // 中性色：本层未覆盖区域参与运算时使用的颜色（0xAARRGGBB）。
  // 加法类为黑、乘法/变暗类为白、Overlay/HardLight/SoftLight 为 50% 灰。
  std::uint32_t LayerOpNeutralColor(std::uint32_t op);
} // namespace krkr::pixel
