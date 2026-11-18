#include "pixel/Blend.h"

#include <cstdint>
#include <mutex>

#include "core/log/Logger.h"
#include "vendor/krkrz/visual/porting/pixel_routines.h"  // 上游像素函数表（只在本文件出现）

// 定义在 vendor/krkrz/visual/gl/blend_function.cpp：把 29 种混合算子的函数指针
// 指向该文件里的便携实现。上游没有为它提供头文件声明（只在本仓库登记）。
void TVPGL_C_Init();

namespace krkr::pixel {

namespace {

constexpr tjs_int kSpanLength = 1;

// 上游把混合算子拆成若干变体（后缀含义见 vendor/krkrz/visual/tvpgl.h）：
//   plain  = 没有 opacity（opa == 255）
//   scaled = 带 opacity
// 我们只取这两支：Surface 的目标 alpha 恒为 0xFF，`_d`/`_a`（目标带 alpha）与
// plain 在 RGB 上等价，`_HDA`（保护目标 alpha）在 alpha 恒为 0xFF 时也是空操作。
struct BlendFunctions {
  void (*plain)(tjs_uint32 *, const tjs_uint32 *, tjs_int);
  void (*scaled)(tjs_uint32 *, const tjs_uint32 *, tjs_int, tjs_int);
};

// 与上游 `LayerBitmapIntf.cpp` 的 bmXxx 分派一一对应（`TVP_BLEND_4(basename)`）。
// 上游的 layer type → operation mode → bm 两步映射在这里合并成一张表；
// 若上游更换映射，`host/BlendMatrixProbe.cpp` 的语义断言会先变红。
BlendFunctions FunctionsFor(LayerOp op) {
  switch (op) {
    case ltAlpha:
      return {TVPAlphaBlend, TVPAlphaBlend_o};
    case ltAdditive:
      return {TVPAddBlend, TVPAddBlend_o};
    case ltSubtractive:
      return {TVPSubBlend, TVPSubBlend_o};
    case ltMultiplicative:
      return {TVPMulBlend, TVPMulBlend_o};
    case ltDodge:
      return {TVPColorDodgeBlend, TVPColorDodgeBlend_o};
    case ltDarken:
      return {TVPDarkenBlend, TVPDarkenBlend_o};
    case ltLighten:
      return {TVPLightenBlend, TVPLightenBlend_o};
    case ltScreen:
      return {TVPScreenBlend, TVPScreenBlend_o};
    case ltAddAlpha:
      return {TVPAdditiveAlphaBlend, TVPAdditiveAlphaBlend_o};
    case ltPsNormal:
      return {TVPPsAlphaBlend, TVPPsAlphaBlend_o};
    case ltPsAdditive:
      return {TVPPsAddBlend, TVPPsAddBlend_o};
    case ltPsSubtractive:
      return {TVPPsSubBlend, TVPPsSubBlend_o};
    case ltPsMultiplicative:
      return {TVPPsMulBlend, TVPPsMulBlend_o};
    case ltPsScreen:
      return {TVPPsScreenBlend, TVPPsScreenBlend_o};
    case ltPsOverlay:
      return {TVPPsOverlayBlend, TVPPsOverlayBlend_o};
    case ltPsHardLight:
      return {TVPPsHardLightBlend, TVPPsHardLightBlend_o};
    case ltPsSoftLight:
      return {TVPPsSoftLightBlend, TVPPsSoftLightBlend_o};
    case ltPsColorDodge:
      return {TVPPsColorDodgeBlend, TVPPsColorDodgeBlend_o};
    case ltPsColorDodge5:
      return {TVPPsColorDodge5Blend, TVPPsColorDodge5Blend_o};
    case ltPsColorBurn:
      return {TVPPsColorBurnBlend, TVPPsColorBurnBlend_o};
    case ltPsLighten:
      return {TVPPsLightenBlend, TVPPsLightenBlend_o};
    case ltPsDarken:
      return {TVPPsDarkenBlend, TVPPsDarkenBlend_o};
    case ltPsDifference:
      return {TVPPsDiffBlend, TVPPsDiffBlend_o};
    case ltPsDifference5:
      return {TVPPsDiff5Blend, TVPPsDiff5Blend_o};
    case ltPsExclusion:
      return {TVPPsExclusionBlend, TVPPsExclusionBlend_o};
    default:
      return {nullptr, nullptr};
  }
}

// `ltOpaque` 在上游走 bmCopy（`LayerBitmapIntf.cpp`）：
//   - opa == 255（且保护目标 alpha）：`TVPCopyColor`，逐字节复制；
//   - 否则：`TVPConstAlphaBlend`，只用图层 opacity 在 dst/src 之间插值，
//     **忽略源像素 alpha**（这与 `docs/render-spec.md` §3「result = src」一致）。
// 两者都不是「α 混合」：opaque 层的不透明度是常量比例，不读源 alpha。
std::uint32_t BlendOpaque(std::uint32_t dst, std::uint32_t src, std::uint32_t opacity) {
  tjs_uint32 destination = dst;
  const tjs_uint32 source = src;
  if (opacity >= 255u) {
    TVPCopyColor(&destination, &source, kSpanLength);
  } else {
    TVPConstAlphaBlend(&destination, &source, kSpanLength, static_cast<tjs_int>(opacity));
  }
  return destination;
}

std::once_flag g_initOnce;
bool g_initialized = false;

void InitializeOnce() {
  TVPInitTVPGL();  // 上游 tvpgl.c：非混合类算子与表
  TVPGL_C_Init();  // 上游 blend_function.cpp：29 种混合算子
  g_initialized = true;
  krkr::log::Debug("pixel: upstream blend routines initialised (vendor/krkrz)");
}

} // namespace

void InitializeBlendRoutines() {
  std::call_once(g_initOnce, InitializeOnce);
}

bool AreBlendRoutinesReady() { return g_initialized; }

std::uint32_t BlendPixel(std::uint32_t dst, std::uint32_t src, LayerOp op,
                         std::uint32_t opacity) {
  InitializeBlendRoutines();

  const std::uint32_t clampedOpacity = opacity > 255u ? 255u : opacity;
  if (!IsLayerOpDrawable(op)) {
    return dst;
  }
  if (op == ltOpaque) {
    return BlendOpaque(dst, src, clampedOpacity) | 0xFF000000u;
  }

  const BlendFunctions functions = FunctionsFor(op);
  if (functions.plain == nullptr || functions.scaled == nullptr) {
    // 枚举与上游映射脱节时宁可不动像素，也不猜一个算子。
    krkr::log::Warn("blend: no upstream routine for layer op ", LayerOpName(op));
    return dst;
  }

  tjs_uint32 destination = dst;
  const tjs_uint32 source = src;
  if (clampedOpacity >= 255u) {
    functions.plain(&destination, &source, kSpanLength);
  } else {
    functions.scaled(&destination, &source, kSpanLength, static_cast<tjs_int>(clampedOpacity));
  }
  // 上游的非 `_d`/`_a` 变体不维护目标 alpha（有的会把它清零）。本层合成的是**不透明的
  // 窗口表面**，所以统一规范为 0xFF；图层之间的 alpha 语义由 legacy 位图自己负责（R1-3）。
  return destination | 0xFF000000u;
}

} // namespace krkr::pixel
