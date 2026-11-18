#include <cstdio>
#include <string>

#include "platform/desktop/PpmTarget.h"
#include "visual/DrawList.h"
#include "visual/SoftwareBackend.h"
#include "visual/Surface.h"

// 软件合成测试台（渲染 8a 的断言）。
//
//   render_probe [输出.ppm]
//
// 两条通道都测：Surface 的原语（FillRect/Composite）与 DrawList 的后端路径。
// 期望值是手算的定点结果，不依赖任何后端；GL 后端以后要用同一份断言（允许 ±1）。

namespace {

int failures = 0;

void Check(bool condition, const char *what) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) {
    ++failures;
  }
}

std::string Hex(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
  return buffer;
}

void CheckPixel(const krkr::visual::Surface &surface, int x, int y,
                std::uint32_t expected, const char *what) {
  const std::uint32_t actual = surface.Get(x, y);
  if (actual == expected) {
    std::printf("PASS  %s\n", what);
    return;
  }
  std::printf("FAIL  %s: got %s want %s\n", what, Hex(actual).c_str(), Hex(expected).c_str());
  ++failures;
}

krkr::visual::DrawItem Quad(float left, float top, float right, float bottom,
                            std::uint32_t color, krkr::visual::LayerType blend,
                            std::uint32_t opacity = 255) {
  krkr::visual::DrawItem item;
  item.color = color;
  item.blend = blend;
  item.opacity = opacity;
  item.vertices[0][0] = left;
  item.vertices[0][1] = top;
  item.vertices[1][0] = right;
  item.vertices[1][1] = top;
  item.vertices[2][0] = left;
  item.vertices[2][1] = bottom;
  item.vertices[3][0] = right;
  item.vertices[3][1] = bottom;
  return item;
}

} // namespace

int main(int argc, char **argv) {
  using namespace krkr::visual;

  // --- 1) Surface 原语 -------------------------------------------------------
  Surface canvas(4, 4);
  canvas.Clear(0xff000000u);
  canvas.FillRect({1, 1, 3, 3}, 0xffff0000u);
  CheckPixel(canvas, 1, 1, 0xffff0000u, "FillRect writes inside the rect");
  CheckPixel(canvas, 0, 0, 0xff000000u, "FillRect leaves outside untouched");
  canvas.FillRect({-2, -2, 2, 2}, 0xff00ff00u);
  CheckPixel(canvas, 0, 0, 0xff00ff00u, "FillRect clips negative coordinates");
  CheckPixel(canvas, 2, 2, 0xffff0000u, "FillRect treats right/bottom as exclusive");

  Surface overlay(2, 2);
  overlay.Clear(0x80ffffffu);
  Surface target(2, 2);
  target.Clear(0xffff0000u);
  target.Composite(overlay, {0, 0, 2, 2}, {0, 0, 2, 2}, ltAlpha, 255);
  // a = 128；keep = 127；r = 255*128/255 + 255*127/255 = 255；g = 255*128/255 = 128
  CheckPixel(target, 0, 0, 0xffff8080u, "alpha blend mixes source over destination");
  target.Composite(overlay, {0, 0, 2, 2}, {0, 0, 2, 2}, ltOpaque, 255);
  CheckPixel(target, 0, 0, 0xffffffffu, "opaque copies source rgb");

  // --- 2) DrawList 后端路径 --------------------------------------------------
  Surface frame(8, 6);
  DrawList list;
  list.Reset(0xff000000u);
  list.items.push_back(Quad(1, 1, 3, 3, 0xffff0000u, ltOpaque));
  // 2x2 纯白纹理拉伸到 4x2：最近邻采样，前两列取源第 0 列、后两列取源第 1 列。
  Texture texture;
  texture.width = 2;
  texture.height = 2;
  texture.pixels = {0xffffffffu, 0xff00ff00u, 0xff0000ffu, 0xffffffffu};
  const std::uint32_t textureId = list.AddTexture(&texture);
  {
    DrawItem item = Quad(4, 1, 8, 3, 0, ltOpaque);
    item.textureId = textureId;
    list.items.push_back(item);
  }
  // 被 clip 裁掉一半的纯蓝块。
  {
    DrawItem item = Quad(1, 4, 5, 6, 0xff0000ffu, ltOpaque);
    item.clip = {0, 0, 3, 6};
    list.items.push_back(item);
  }
  // 旋转项：8a 不支持，必须被跳过并计入统计。
  {
    DrawItem item = Quad(0, 0, 2, 2, 0xffffffffu, ltAlpha);
    item.vertices[0][0] = 5.5f;
    item.vertices[0][1] = 3.5f;
    item.vertices[1][0] = 7.5f;
    item.vertices[1][1] = 3.0f;
    item.vertices[2][0] = 6.0f;
    item.vertices[2][1] = 5.5f;
    item.vertices[3][0] = 8.0f;
    item.vertices[3][1] = 5.0f;
    list.items.push_back(item);
  }

  RenderStats stats;
  Check(Render(list, frame, &stats), "Render accepts the draw list");
  Check(stats.itemsDrawn == 3, "three axis-aligned items are drawn");
  Check(stats.itemsSkipped == 1, "rotated item is skipped, not drawn wrong");
  CheckPixel(frame, 1, 1, 0xffff0000u, "solid quad covers its rect");
  CheckPixel(frame, 0, 0, 0xff000000u, "clear color survives outside items");
  CheckPixel(frame, 4, 1, 0xffffffffu, "texture column 0 maps to the left half");
  CheckPixel(frame, 6, 1, 0xff00ff00u, "texture column 1 maps to the right half");
  CheckPixel(frame, 4, 2, 0xff0000ffu, "texture row 1 uses the second source row");
  CheckPixel(frame, 2, 4, 0xff0000ffu, "clip keeps the visible part");
  CheckPixel(frame, 3, 4, 0xff000000u, "clip rejects the hidden part");

  // --- 3) 呈现目标（离屏 PPM） -----------------------------------------------
  if (argc >= 2) {
    krkr::platform::PpmTarget target(argv[1], frame.Width(), frame.Height());
    Check(target.IsValid(), "PPM target is valid");
    Check(target.Present(frame.Pixels(), frame.Width(), frame.Height(), frame.Width()),
          "PPM target accepts the frame");
    std::printf("       wrote %s\n", target.GetLastPath().c_str());
  }

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
