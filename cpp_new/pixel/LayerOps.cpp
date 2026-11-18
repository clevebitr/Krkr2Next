#include "pixel/LayerOps.h"

#include <array>
#include <cstddef>

namespace krkr::pixel {

namespace {

struct LayerOpInfo {
  const char *name;
  bool alphaBlended;      // 混合公式读取源 alpha（上游 TVPIsTypeUsingAlpha）
  std::uint32_t neutral;  // 中性色 0xAARRGGBB
};

// 顺序与 LayerType 的数值一一对应；新增/改值必须同步两侧。
// 数据来自上游 `tTJSNI_BaseLayer::SetType` 的 NeutralColor 分支
// 与 `drawable.h` 的 TVPIsTypeUsingAlpha。
constexpr std::array<LayerOpInfo, kLayerOpCount> kLayerOps{{
    {"Binder", false, 0x00FFFFFFu},
    {"Opaque", false, 0x00FFFFFFu},
    {"Alpha", true, 0x00FFFFFFu},
    {"Additive", false, 0x00000000u},
    {"Subtractive", false, 0x00FFFFFFu},
    {"Multiplicative", false, 0x00FFFFFFu},
    {"Effect", false, 0x00FFFFFFu},
    {"Filter", false, 0x00FFFFFFu},
    {"Dodge", false, 0x00000000u},
    {"Darken", false, 0x00FFFFFFu},
    {"Lighten", false, 0x00000000u},
    {"Screen", false, 0x00000000u},
    {"AddAlpha", false, 0x00000000u},
    {"PsNormal", true, 0x00000000u},
    {"PsAdditive", true, 0x00000000u},
    {"PsSubtractive", true, 0x00FFFFFFu},
    {"PsMultiplicative", true, 0x00FFFFFFu},
    {"PsScreen", true, 0x00000000u},
    {"PsOverlay", true, 0x00808080u},
    {"PsHardLight", true, 0x00808080u},
    {"PsSoftLight", true, 0x00808080u},
    {"PsColorDodge", true, 0x00000000u},
    {"PsColorDodge5", true, 0x00000000u},
    {"PsColorBurn", true, 0x00FFFFFFu},
    {"PsLighten", true, 0x00000000u},
    {"PsDarken", true, 0x00FFFFFFu},
    {"PsDifference", true, 0x00000000u},
    {"PsDifference5", true, 0x00000000u},
    {"PsExclusion", true, 0x00000000u},
}};

const LayerOpInfo &InfoFor(std::uint32_t op) {
  static const LayerOpInfo unknown{"Unknown", false, 0x00000000u};
  return op < kLayerOps.size() ? kLayerOps[op] : unknown;
}

} // namespace

const char *LayerOpName(std::uint32_t op) { return InfoFor(op).name; }

bool IsLayerOpDrawable(std::uint32_t op) {
  if (op == ltBinder || op == ltEffect || op == ltFilter) {
    return false;
  }
  return op < kLayerOpCount;
}

bool IsLayerOpAlphaBlended(std::uint32_t op) {
  return op < kLayerOpCount && InfoFor(op).alphaBlended;
}

std::uint32_t LayerOpNeutralColor(std::uint32_t op) {
  return op < kLayerOpCount ? InfoFor(op).neutral : 0x00000000u;
}

} // namespace krkr::pixel
