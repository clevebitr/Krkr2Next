#include <cstdio>
#include <cstdlib>
#include <vector>

#include "platform/desktop/gl/GLContext.h"
#include "visual/DrawList.h"
#include "visual/GLBackend.h"
#include "visual/SoftwareBackend.h"

// GL 后端验收（8b）：同一个 DrawList 分别走软件后端与 GL 后端，逐像素比较。
//
//   render_gl_probe
//
// 退出码 77 = 环境没有可用的 GL（CTest 记为 skip）。像素容差 ±1：
// 软件后端是定点整数，GPU 是浮点混合，逐位相等不现实，能保证的只有 ±1。

namespace {

int failures = 0;

void Check(bool condition, const char *what) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) {
    ++failures;
  }
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

krkr::visual::DrawItem Textured(const krkr::visual::Texture &texture, std::uint32_t textureId,
                                float left, float top, float right, float bottom,
                                krkr::visual::LayerType blend) {
  krkr::visual::DrawItem item = Quad(left, top, right, bottom, 0xffffffffu, blend);
  item.textureId = textureId;
  item.uvs[0][0] = 0.0f;
  item.uvs[0][1] = 0.0f;
  item.uvs[1][0] = 1.0f;
  item.uvs[1][1] = 0.0f;
  item.uvs[2][0] = 0.0f;
  item.uvs[2][1] = 1.0f;
  item.uvs[3][0] = 1.0f;
  item.uvs[3][1] = 1.0f;
  (void)texture;
  return item;
}

int Channel(std::uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 0xffu); }

bool Near(std::uint32_t a, std::uint32_t b) {
  return std::abs(Channel(a, 16) - Channel(b, 16)) <= 1 &&
         std::abs(Channel(a, 8) - Channel(b, 8)) <= 1 &&
         std::abs(Channel(a, 0) - Channel(b, 0)) <= 1;
}

} // namespace

int main() {
  using namespace krkr::visual;
  using krkr::render::GLApi;
  using krkr::render::GLenum;
  using krkr::render::GLuint;

  const int width = 64;
  const int height = 48;

  Texture texture;
  texture.width = 4;
  texture.height = 4;
  texture.pixels.assign(16, 0xff3060c0u);
  texture.pixels[0] = 0xffff2020u;   // 左上红
  texture.pixels[3] = 0xff20ff20u;   // 右上绿
  texture.pixels[12] = 0xff2040ffu;  // 左下蓝
  texture.pixels[15] = 0xffffffffu;  // 右下白

  DrawList list;
  list.Reset(0xff102030u);
  list.items.push_back(Quad(4, 4, 28, 28, 0xffc04020u, ltOpaque));
  const std::uint32_t textureId = list.AddTexture(&texture);
  list.items.push_back(Textured(texture, textureId, 20, 12, 52, 44, ltAlpha));
  list.items.push_back(Quad(8, 30, 40, 46, 0x60ffffffu, ltAlpha));
  {
    DrawItem item = Quad(0, 0, width, 8, 0xff00ff00u, ltOpaque);
    item.clip = {0, 0, 16, 8};
    list.items.push_back(item);
  }

  Surface software(width, height);
  Render(list, software, nullptr);

  krkr::platform::GLContext context(width, height);
  if (!context.IsValid()) {
    std::printf("SKIP  no usable GL context (%s)\n", context.GetLastErrorText());
    return 77;
  }
  if (!context.MakeCurrent()) {
    std::printf("SKIP  cannot make the context current\n");
    return 77;
  }

  GLApi api;
  if (!context.LoadApi(api)) {
    std::printf("SKIP  %s\n", context.GetLastErrorText());
    return 77;
  }

  GLBackend backend;
  if (!backend.Initialize(api)) {
    std::printf("FAIL  backend initialize: %s\n", backend.GetLastErrorText().c_str());
    return 1;
  }

  // 离屏帧缓冲：渲染到纹理再读回，避免依赖窗口可见性。
  GLuint colorTexture = 0;
  api.GenTextures(1, &colorTexture);
  api.BindTexture(KRKR_GL_TEXTURE_2D, colorTexture);
  api.TexParameteri(KRKR_GL_TEXTURE_2D, KRKR_GL_TEXTURE_MIN_FILTER, KRKR_GL_NEAREST);
  api.TexParameteri(KRKR_GL_TEXTURE_2D, KRKR_GL_TEXTURE_MAG_FILTER, KRKR_GL_NEAREST);
  api.TexImage2D(KRKR_GL_TEXTURE_2D, 0, KRKR_GL_RGBA, width, height, 0, KRKR_GL_RGBA,
                 KRKR_GL_UNSIGNED_BYTE, nullptr);

  GLuint framebuffer = 0;
  api.GenFramebuffers(1, &framebuffer);
  api.BindFramebuffer(KRKR_GL_FRAMEBUFFER, framebuffer);
  api.FramebufferTexture2D(KRKR_GL_FRAMEBUFFER, KRKR_GL_COLOR_ATTACHMENT0, KRKR_GL_TEXTURE_2D,
                           colorTexture, 0);
  const GLenum status = api.CheckFramebufferStatus(KRKR_GL_FRAMEBUFFER);
  if (status != KRKR_GL_FRAMEBUFFER_COMPLETE) {
    std::printf("FAIL  framebuffer incomplete: 0x%04x\n", status);
    return 1;
  }
  api.Viewport(0, 0, width, height);

  GLRenderStats stats;
  const bool rendered = backend.Render(list, width, height, &stats);
  std::printf("      render=%d drawn=%llu skipped=%llu glError=0x%04x lastError=%s\n",
              rendered ? 1 : 0,
              static_cast<unsigned long long>(stats.itemsDrawn),
              static_cast<unsigned long long>(stats.itemsSkipped),
              static_cast<unsigned>(api.GetError()), backend.GetLastErrorText().c_str());
  Check(rendered, "GL backend renders the draw list");
  Check(stats.itemsDrawn == 4, "GL draws all four axis-aligned items");

  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
  api.ReadPixels(0, 0, width, height, KRKR_GL_RGBA, KRKR_GL_UNSIGNED_BYTE, pixels.data());

  int mismatches = 0;
  int worst = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // glReadPixels 原点在左下；Surface 原点在左上。
      const std::size_t index =
          (static_cast<std::size_t>(height - 1 - y) * width + static_cast<std::size_t>(x)) * 4;
      const std::uint32_t gl = (static_cast<std::uint32_t>(pixels[index + 3]) << 24) |
                               (static_cast<std::uint32_t>(pixels[index + 0]) << 16) |
                               (static_cast<std::uint32_t>(pixels[index + 1]) << 8) |
                               static_cast<std::uint32_t>(pixels[index + 2]);
      const std::uint32_t sw = software.Get(x, y);
      if (!Near(gl, sw)) {
        ++mismatches;
        const int delta = std::max(std::abs(Channel(gl, 16) - Channel(sw, 16)),
                                   std::max(std::abs(Channel(gl, 8) - Channel(sw, 8)),
                                            std::abs(Channel(gl, 0) - Channel(sw, 0))));
        worst = std::max(worst, delta);
        if (mismatches <= 5) {
          std::printf("      mismatch at (%d,%d): gl=0x%08X sw=0x%08X\n", x, y, gl, sw);
        }
      }
    }
  }
  std::printf("      compare: %d mismatching pixel(s), worst channel delta %d\n", mismatches,
              worst);
  Check(mismatches == 0, "GL output matches the software backend within ±1");

  api.BindFramebuffer(KRKR_GL_FRAMEBUFFER, 0);
  api.DeleteFramebuffers(1, &framebuffer);
  api.DeleteTextures(1, &colorTexture);
  backend.Shutdown();

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
