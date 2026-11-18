// 混合算子矩阵测试台（R1-1 验收）。
//
//   blend_matrix_probe
//
// 三组断言：
//   1. 不变量：`opacity == 0` 不改目标；忽略源 alpha 的操作对 alpha 不敏感；
//      源完全不透明时结果等于源颜色。
//   2. 官方公式：`opacity == 255` 下按 docs/render-spec.md §3 的公式做浮点参照，
//      逐通道比较（8 位定点 vs 浮点，容差 kMaxChannelDelta）。
//   3. 覆盖面：29 种操作全部走一遍，映射写错（例如把 `ltAdditive` 当 alpha 混合）
//      会立刻表现为大误差。
//
// 参照实现是**从文档公式独立写的浮点模型**，与 vendor 的定点实现没有共享代码，
// 因此能发现语义错误，而不是两边一起错。真正的逐像素 oracle（Windows krkrz 落盘）
// 在 R1-2 之后接入，见 docs/visual-vendor.md §5。

#include <cstdint>
#include <cstdio>
#include <string>

#include "pixel/Blend.h"
#include "pixel/LayerOps.h"

namespace {

using krkr::pixel::AreBlendRoutinesReady;
using krkr::pixel::BlendPixel;
using krkr::pixel::InitializeBlendRoutines;
using krkr::pixel::IsLayerOpAlphaBlended;
using krkr::pixel::IsLayerOpDrawable;
using krkr::pixel::kLayerOpCount;
using krkr::pixel::LayerOp;
using krkr::pixel::LayerOpName;

// 8 位定点实现与浮点模型之间允许的单通道差值（tvpgl 用 >>8 近似 /255）。
constexpr int kMaxChannelDelta = 3;

constexpr std::uint32_t kDestination = 0xFF4080C0u;  // A=255 R=64 G=128 B=192
constexpr std::uint32_t kSource = 0xFFC86432u;       // A=255 R=200 G=100 B=50

constexpr std::uint32_t kSourceAlphas[] = {0, 64, 128, 192, 255};

int failures = 0;
int checks = 0;

void Check(bool condition, const std::string &what) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("FAIL  %s\n", what.c_str());
  }
}

int Channel(std::uint32_t pixel, int shift) {
  return static_cast<int>((pixel >> shift) & 0xffu);
}

int MaxChannelDelta(std::uint32_t left, std::uint32_t right) {
  int delta = 0;
  for (int shift : {16, 8, 0}) {
    int d = Channel(left, shift) - Channel(right, shift);
    if (d < 0) {
      d = -d;
    }
    if (d > delta) {
      delta = d;
    }
  }
  return delta;
}

// 文档公式（docs/render-spec.md §3）：通道取 0..1。
double Blend(double dst, double src, double alpha) { return dst * (1.0 - alpha) + src * alpha; }

double Clamp01(double value) { return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value); }

double Overlay(double dst, double src) {
  return dst < 0.5 ? 2.0 * dst * src : 1.0 - 2.0 * (1.0 - dst) * (1.0 - src);
}

double HardLight(double dst, double src) {
  return src < 0.5 ? 2.0 * dst * src : 1.0 - 2.0 * (1.0 - dst) * (1.0 - src);
}

// 返回 false 表示没有可用的文档公式（只做不变量断言）。
bool DocumentedResult(LayerOp op, double dst, double src, double alpha, double &out) {
  switch (op) {
    case krkr::pixel::ltOpaque:
      out = src;
      return true;
    case krkr::pixel::ltAlpha:
    case krkr::pixel::ltPsNormal:
      out = Blend(dst, src, alpha);
      return true;
    case krkr::pixel::ltAddAlpha:
      out = dst * (1.0 - alpha) + src;
      return true;
    case krkr::pixel::ltAdditive:
      out = Clamp01(dst + src);
      return true;
    case krkr::pixel::ltSubtractive:
      out = Clamp01(dst + src - 1.0);
      return true;
    case krkr::pixel::ltMultiplicative:
      out = dst * src;
      return true;
    case krkr::pixel::ltDodge:
      out = src >= 1.0 ? 1.0 : Clamp01(dst / (1.0 - src));
      return true;
    case krkr::pixel::ltLighten:
      out = dst > src ? dst : src;
      return true;
    case krkr::pixel::ltDarken:
      out = dst < src ? dst : src;
      return true;
    case krkr::pixel::ltScreen:
      out = 1.0 - (1.0 - dst) * (1.0 - src);
      return true;
    case krkr::pixel::ltPsAdditive:
      out = Blend(dst, Clamp01(dst + src), alpha);
      return true;
    case krkr::pixel::ltPsSubtractive:
      out = Blend(dst, Clamp01(dst + src - 1.0), alpha);
      return true;
    case krkr::pixel::ltPsMultiplicative:
      out = Blend(dst, dst * src, alpha);
      return true;
    case krkr::pixel::ltPsScreen:
      out = Blend(dst, 1.0 - (1.0 - dst) * (1.0 - src), alpha);
      return true;
    case krkr::pixel::ltPsOverlay:
      out = Blend(dst, Overlay(dst, src), alpha);
      return true;
    case krkr::pixel::ltPsHardLight:
      out = Blend(dst, HardLight(dst, src), alpha);
      return true;
    case krkr::pixel::ltPsColorDodge:
      out = Blend(dst, src >= 1.0 ? 1.0 : Clamp01(dst / (1.0 - src)), alpha);
      return true;
    case krkr::pixel::ltPsColorDodge5:
      out = src * alpha >= 1.0 ? 1.0 : dst / (1.0 - src * alpha);
      return true;
    case krkr::pixel::ltPsColorBurn:
      out = Blend(dst, src <= 0.0 ? 0.0 : Clamp01(1.0 - (1.0 - dst) / src), alpha);
      return true;
    case krkr::pixel::ltPsLighten:
      out = Blend(dst, dst > src ? dst : src, alpha);
      return true;
    case krkr::pixel::ltPsDarken:
      out = Blend(dst, dst < src ? dst : src, alpha);
      return true;
    case krkr::pixel::ltPsDifference:
      out = Blend(dst, dst > src ? dst - src : src - dst, alpha);
      return true;
    case krkr::pixel::ltPsDifference5:
      out = dst > src * alpha ? dst - src * alpha : src * alpha - dst;
      return true;
    case krkr::pixel::ltPsExclusion:
      out = Blend(dst, dst + src - 2.0 * src * dst, alpha);
      return true;
    default:
      // ltBinder/ltEffect/ltFilter 不产生绘制；ltPsSoftLight 的文档公式存疑，只做不变量。
      return false;
  }
}

std::uint32_t WithAlpha(std::uint32_t pixel, std::uint32_t alpha) {
  return (pixel & 0x00ffffffu) | (alpha << 24);
}

// `blend_matrix_probe --detail <操作名>`：逐通道打印某个操作的全部样点，
// 用来定位"公式与实现对不上"的具体通道（默认不留痕，断言失败时才需要）。
int Detail(const char *name) {
  InitializeBlendRoutines();
  const int dstChannels[3] = {Channel(kDestination, 16), Channel(kDestination, 8), Channel(kDestination, 0)};
  const int srcChannels[3] = {Channel(kSource, 16), Channel(kSource, 8), Channel(kSource, 0)};
  for (std::uint32_t value = 0; value < kLayerOpCount; ++value) {
    const auto op = static_cast<LayerOp>(value);
    if (std::string(LayerOpName(op)) != name) {
      continue;
    }
    std::printf("%s\n%-6s %-12s %-12s %s\n", name, "alpha", "actual", "expected", "per-channel delta r/g/b");
    for (std::uint32_t alpha : kSourceAlphas) {
      const double a = static_cast<double>(alpha) / 255.0;
      double expected[3] = {0.0, 0.0, 0.0};
      const bool documented = DocumentedResult(op, dstChannels[0] / 255.0, srcChannels[0] / 255.0, a, expected[0]) &&
                              DocumentedResult(op, dstChannels[1] / 255.0, srcChannels[1] / 255.0, a, expected[1]) &&
                              DocumentedResult(op, dstChannels[2] / 255.0, srcChannels[2] / 255.0, a, expected[2]);
      const std::uint32_t actual = BlendPixel(kDestination, WithAlpha(kSource, alpha), op, 255);
      int reference[3] = {0, 0, 0};
      int delta[3] = {0, 0, 0};
      for (int i = 0; i < 3; ++i) {
        const double clamped = expected[i] < 0.0 ? 0.0 : (expected[i] > 1.0 ? 1.0 : expected[i]);
        reference[i] = static_cast<int>(clamped * 255.0 + 0.5);
        delta[i] = Channel(actual, i == 0 ? 16 : (i == 1 ? 8 : 0)) - reference[i];
      }
      std::printf("%-6u 0x%08X   0x%02X%02X%02X        %d/%d/%d\n", alpha, actual, reference[0],
                  reference[1], reference[2], delta[0], delta[1], delta[2]);
      if (!documented) {
        std::printf("      (no documented formula)\n");
      }
    }
    return 0;
  }
  std::printf("unknown operation: %s\n", name);
  return 2;
}

} // namespace

int main(int argc, char **argv) {
  if (argc >= 3 && std::string(argv[1]) == "--detail") {
    return Detail(argv[2]);
  }
  InitializeBlendRoutines();
  if (!AreBlendRoutinesReady()) {
    std::printf("FAIL  upstream pixel routines did not initialise\n");
    return 1;
  }
  std::printf("blend_matrix: dst=0x%08X src=0x%08X\n\n", kDestination, kSource);

  const int dstChannels[3] = {Channel(kDestination, 16), Channel(kDestination, 8),
                              Channel(kDestination, 0)};
  const int srcChannels[3] = {Channel(kSource, 16), Channel(kSource, 8), Channel(kSource, 0)};

  int documentedChecks = 0;
  int documentedSkipped = 0;
  int worstDelta = 0;
  std::string worstCase;

  for (std::uint32_t value = 0; value < kLayerOpCount; ++value) {
    const auto op = static_cast<LayerOp>(value);
    const std::string name = LayerOpName(op);

    if (!IsLayerOpDrawable(op)) {
      Check(BlendPixel(kDestination, kSource, op, 128) == kDestination,
            name + ": non-drawable operation leaves the target untouched");
      continue;
    }

    // 不变量 1：opacity == 0 时目标基本不变。
    // 上游 `_o` 变体的做法是"把源色淡向该算子的中性色"，定点 >>8 会留下 ±1 的残差
    // （例如乘法层淡到白色仍会乘一次 255/256）。因此这里按容差比较，而不是严格相等。
    for (std::uint32_t alpha : kSourceAlphas) {
      Check(MaxChannelDelta(BlendPixel(kDestination, WithAlpha(kSource, alpha), op, 0), kDestination) <=
                kMaxChannelDelta,
            name + ": opacity 0 keeps the target (alpha " + std::to_string(alpha) + ")");
    }

    // 不变量 2：忽略源 alpha 的操作对 alpha 不敏感（ltAddAlpha 走预乘路径，排除）。
    if (!IsLayerOpAlphaBlended(op) && op != krkr::pixel::ltAddAlpha) {
      const std::uint32_t reference = BlendPixel(kDestination, kSource, op, 255);
      for (std::uint32_t alpha : kSourceAlphas) {
        Check(BlendPixel(kDestination, WithAlpha(kSource, alpha), op, 255) == reference,
              name + ": source alpha is ignored (alpha " + std::to_string(alpha) + ")");
      }
    }

    for (std::uint32_t alpha : kSourceAlphas) {
      const std::uint32_t source = WithAlpha(kSource, alpha);
      const std::uint32_t actual = BlendPixel(kDestination, source, op, 255);

      // 不变量 3：源完全不透明时，只依赖 src 的 alpha 混合应当完全取用源颜色
      // （读目标像素的操作如 PsLighten/PsOverlay 自然不满足，跳过）。
      if (alpha == 255 && (op == krkr::pixel::ltAlpha || op == krkr::pixel::ltPsNormal)) {
        Check(MaxChannelDelta(actual, (source & 0x00ffffffu) | 0xFF000000u) <= kMaxChannelDelta,
              name + ": opaque source replaces the target colour");
      }

      // 不变量 4 + 文档公式：与浮点模型比较。
      const double a = static_cast<double>(alpha) / 255.0;
      double expected[3] = {0.0, 0.0, 0.0};
      if (!DocumentedResult(op, static_cast<double>(dstChannels[0]) / 255.0,
                            static_cast<double>(srcChannels[0]) / 255.0, a, expected[0]) ||
          !DocumentedResult(op, static_cast<double>(dstChannels[1]) / 255.0,
                            static_cast<double>(srcChannels[1]) / 255.0, a, expected[1]) ||
          !DocumentedResult(op, static_cast<double>(dstChannels[2]) / 255.0,
                            static_cast<double>(srcChannels[2]) / 255.0, a, expected[2])) {
        ++documentedSkipped;
        continue;
      }

      int delta = 0;
      for (int i = 0; i < 3; ++i) {
        const double clamped = expected[i] < 0.0 ? 0.0 : (expected[i] > 1.0 ? 1.0 : expected[i]);
        const int reference = static_cast<int>(clamped * 255.0 + 0.5);
        int d = Channel(actual, i == 0 ? 16 : (i == 1 ? 8 : 0)) - reference;
        if (d < 0) {
          d = -d;
        }
        if (d > delta) {
          delta = d;
        }
      }
      ++documentedChecks;
      if (delta > worstDelta) {
        worstDelta = delta;
        worstCase = name + ": alpha " + std::to_string(alpha);
      }
      Check(delta <= kMaxChannelDelta,
            name + ": matches the documented formula (alpha " + std::to_string(alpha) +
                ", max delta " + std::to_string(delta) + ")");
    }
  }

  // 便于人工核对的样点。
  std::printf("%-14s %-8s %s\n", "operation", "opacity", "result(dst=0x4080C0, src=0xC86432@50%%, opacity=128)");
  const std::uint32_t halfSource = WithAlpha(kSource, 128);
  for (LayerOp sample : {krkr::pixel::ltOpaque, krkr::pixel::ltAlpha, krkr::pixel::ltAdditive,
                         krkr::pixel::ltPsOverlay, krkr::pixel::ltPsDifference5}) {
    std::printf("%-14s %-8d 0x%08X\n", LayerOpName(sample), 128,
                BlendPixel(kDestination, halfSource, sample, 128));
  }

  std::printf("\ncovered: %d documented comparison(s), %d skipped (no usable formula)\n",
              documentedChecks, documentedSkipped);
  if (worstDelta > 0) {
    std::printf("largest deviation from the float model: %d channel(s) at %s\n", worstDelta,
                worstCase.c_str());
  }
  std::printf("%s (%d failure(s) / %d check(s))\n", failures == 0 ? "ALL PASS" : "FAILED",
              failures, checks);
  return failures == 0 ? 0 : 1;
}
