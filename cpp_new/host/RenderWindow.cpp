#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "platform/desktop/win32/Win32WindowTarget.h"
#include "visual/DrawList.h"
#include "visual/SoftwareBackend.h"
#include "visual/Surface.h"

// Windows 窗口演示：用同一个软件后端持续出帧，验证"平台呈现"这一段。
//
//   render_window [帧数]
//
// 8c 换成 EGL/WGL 时只替换 target，DrawList 与后端不动。

namespace {

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

  const int frames = argc >= 2 ? std::atoi(argv[1]) : 600;
  krkr::platform::Win32WindowTarget target("krkr_next render core", 640, 480);
  if (!target.IsValid()) {
    std::fprintf(stderr, "cannot create the window\n");
    return 1;
  }

  Surface frame(target.GetWidth(), target.GetHeight());
  for (int i = 0; i < frames; ++i) {
    if (!target.PumpEvents()) {
      break;
    }

    DrawList list;
    list.Reset(0xff101820u);
    // 三个随时间移动的方块，覆盖纯色与三种混合模式。
    const float t = static_cast<float>(i) * 0.03f;
    const float x = 40.0f + std::sin(t) * 120.0f + 200.0f;
    const float y = 60.0f + std::cos(t * 0.7f) * 60.0f + 150.0f;
    list.items.push_back(Quad(x, y, x + 120, y + 120, 0xffff4040u, ltOpaque));
    list.items.push_back(Quad(x + 60, y + 40, x + 220, y + 160, 0x8040ff40u, ltAlpha));
    list.items.push_back(Quad(30, 300, 610, 440, 0x402040ffu, ltAdditive));

    RenderStats stats;
    Render(list, frame, &stats);
    if (!target.Present(frame.Pixels(), frame.Width(), frame.Height(), frame.Width())) {
      std::fprintf(stderr, "present failed at frame %d\n", i);
      return 1;
    }
  }

  std::printf("presented frames, drawn items in the last frame: %d\n", frames);
  return 0;
}
