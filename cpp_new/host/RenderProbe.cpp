#include <cstdio>
#include <string>

#include "visual/Surface.h"

// 软件合成测试台（渲染第 8a 片的第一条断言）。
//
//   render_probe [输出.ppm]
//
// 断言的期望值是手算出来的定点结果，不依赖任何后端：GL 后端以后要以同一份断言为准。

namespace {

int failures = 0;

void Check(bool condition, const char *what) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) {
    ++failures;
  }
}

bool Equal(std::uint32_t actual, std::uint32_t expected) {
  return actual == expected;
}

std::string Hex(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
  return buffer;
}

void CheckPixel(const krkr::visual::Surface &surface, int x, int y,
                std::uint32_t expected, const char *what) {
  const std::uint32_t actual = surface.Get(x, y);
  if (Equal(actual, expected)) {
    std::printf("PASS  %s\n", what);
    return;
  }
  std::printf("FAIL  %s: got %s want %s\n", what, Hex(actual).c_str(),
              Hex(expected).c_str());
  ++failures;
}

} // namespace

int main(int argc, char **argv) {
  using namespace krkr::visual;

  // 1) 纯色填充与越界裁剪
  Surface canvas(4, 4);
  canvas.Clear(0xff000000u);
  canvas.FillRect({1, 1, 3, 3}, 0xffff0000u);
  CheckPixel(canvas, 1, 1, 0xffff0000u, "FillRect writes inside the rect");
  CheckPixel(canvas, 0, 0, 0xff000000u, "FillRect leaves outside untouched");
  canvas.FillRect({-2, -2, 2, 2}, 0xff00ff00u);
  CheckPixel(canvas, 0, 0, 0xff00ff00u, "FillRect clips negative coordinates");
  CheckPixel(canvas, 2, 2, 0xffff0000u, "FillRect treats right/bottom as exclusive");

  // 2) alpha 混合：white 50% over red
  //    a = 255*128/255 = 128; keep = 127
  //    r = 255*128/255 + 255*127/255 = 128+127 = 255
  //    g = 255*128/255 + 0*127/255 = 128（源是白色，绿蓝通道来自源）
  Surface overlay(2, 2);
  overlay.Clear(0x80ffffffu);
  Surface target(2, 2);
  target.Clear(0xffff0000u);
  target.Composite(overlay, {0, 0, 2, 2}, {0, 0, 2, 2}, ltAlpha, 255);
  CheckPixel(target, 0, 0, 0xffff8080u, "alpha blend mixes source over destination");

  // 3) 不透明覆盖忽略源 alpha
  target.Composite(overlay, {0, 0, 2, 2}, {0, 0, 2, 2}, ltOpaque, 255);
  CheckPixel(target, 0, 0, 0xffffffffu, "opaque copies source rgb");

  // 4) 加法与乘法
  Surface add(1, 1);
  add.Clear(0xff203040u);
  Surface base(1, 1);
  base.Clear(0xff102030u);
  base.Composite(add, {0, 0, 1, 1}, {0, 0, 1, 1}, ltAdditive, 255);
  CheckPixel(base, 0, 0, 0xff305070u, "additive sums channels");

  Surface mul(1, 1);
  mul.Clear(0xff808080u);
  base.Clear(0xffff8000u);
  base.Composite(mul, {0, 0, 1, 1}, {0, 0, 1, 1}, ltMultiplicative, 255);
  CheckPixel(base, 0, 0, 0xff804000u, "multiplicative scales channels");

  // 5) 目标之外的合成被裁剪
  Surface clipped(2, 2);
  clipped.Clear(0xff000000u);
  clipped.Composite(overlay, {0, 0, 2, 2}, {1, 1, 3, 3}, ltOpaque, 255);
  CheckPixel(clipped, 1, 1, 0xffffffffu, "composite writes the visible part");
  CheckPixel(clipped, 0, 0, 0xff000000u, "composite skips outside destination");

  if (argc >= 2) {
    const bool saved = canvas.SavePpm(argv[1]);
    Check(saved, "SavePpm writes the output file");
  }

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
