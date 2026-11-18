#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "core/io/Storage.h"
#include "platform/desktop/win32/Win32WindowTarget.h"
#include "visual/DrawList.h"
#include "visual/Image.h"
#include "visual/ImageDecoder.h"
#include "visual/SoftwareBackend.h"
#include "visual/Surface.h"

// Windows 端到端演示：真包素材 → 解码 → 软件合成 → 窗口呈现。
//
//   image_window <root-path-file> <storage-name> [帧数]
//
// 用来人工确认"素材解码 + 合成 + 平台呈现"整条链路；自动化断言在 image_probe。

namespace {

bool ReadPathFile(const char *path, std::string &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  std::getline(file, out);
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) {
    out.pop_back();
  }
  return !out.empty();
}

bool ReadEntry(krkr::io::Storage &storage, const char *name, std::string &bytes) {
  const std::unique_ptr<krkr::io::IStream> stream = storage.Open(name);
  if (stream == nullptr) {
    return false;
  }
  char buffer[64 * 1024];
  std::int64_t read = 0;
  while ((read = stream->Read(buffer, static_cast<std::int64_t>(sizeof(buffer)))) > 0) {
    bytes.append(buffer, static_cast<std::size_t>(read));
  }
  return read == 0;
}

} // namespace

int main(int argc, char **argv) {
  using namespace krkr::visual;

  if (argc < 3) {
    std::fprintf(stderr, "usage: image_window <root-path-file> <storage-name> [frames]\n");
    return 2;
  }

  std::string root;
  if (!ReadPathFile(argv[1], root)) {
    std::fprintf(stderr, "cannot read root path file: %s\n", argv[1]);
    return 2;
  }

  krkr::io::Storage storage(root);
  if (!storage.Mounted()) {
    std::fprintf(stderr, "cannot mount %s: %s\n", root.c_str(), storage.GetLastErrorText());
    return 2;
  }

  std::string bytes;
  if (!ReadEntry(storage, argv[2], bytes)) {
    std::fprintf(stderr, "cannot read %s: %s\n", argv[2], storage.GetLastErrorText());
    return 2;
  }

  Image image;
  std::string error;
  if (!DecodeImage(bytes.data(), bytes.size(), image, error)) {
    std::fprintf(stderr, "decode failed: %s\n", error.c_str());
    return 1;
  }
  std::printf("image: %s %dx%d\n", argv[2], image.width, image.height);

  krkr::platform::Win32WindowTarget target("krkr_next image", 960, 540);
  if (!target.IsValid()) {
    std::fprintf(stderr, "cannot create the window\n");
    return 1;
  }

  Texture texture;
  texture.width = image.width;
  texture.height = image.height;
  texture.pixels = image.pixels;

  Surface frame(target.GetWidth(), target.GetHeight());
  const int frames = argc >= 4 ? std::atoi(argv[3]) : 300;

  for (int i = 0; i < frames; ++i) {
    if (!target.PumpEvents()) {
      break;
    }

    DrawList list;
    list.Reset(0xff000000u);
    const std::uint32_t textureId = list.AddTexture(&texture);

    // 图片按窗口宽度等比铺满。
    const float scale = static_cast<float>(frame.Width()) / static_cast<float>(image.width);
    DrawItem photo;
    photo.textureId = textureId;
    photo.blend = ltOpaque;
    photo.vertices[0][0] = 0.0f;
    photo.vertices[0][1] = 0.0f;
    photo.vertices[1][0] = static_cast<float>(frame.Width());
    photo.vertices[1][1] = 0.0f;
    photo.vertices[2][0] = 0.0f;
    photo.vertices[2][1] = static_cast<float>(image.height) * scale;
    photo.vertices[3][0] = static_cast<float>(frame.Width());
    photo.vertices[3][1] = static_cast<float>(image.height) * scale;
    photo.uvs[0][0] = 0.0f;
    photo.uvs[0][1] = 0.0f;
    photo.uvs[1][0] = 1.0f;
    photo.uvs[1][1] = 0.0f;
    photo.uvs[2][0] = 0.0f;
    photo.uvs[2][1] = 1.0f;
    photo.uvs[3][0] = 1.0f;
    photo.uvs[3][1] = 1.0f;
    list.items.push_back(photo);

    // 一条随时间移动的半透明条：直观确认混合路径也在工作。
    const float t = static_cast<float>(i) * 0.04f;
    const float y = (std::sin(t) * 0.5f + 0.5f) * static_cast<float>(frame.Height() - 60);
    DrawItem bar;
    bar.color = 0x60ffffffu;
    bar.blend = ltAlpha;
    bar.vertices[0][0] = 0.0f;
    bar.vertices[0][1] = y;
    bar.vertices[1][0] = static_cast<float>(frame.Width());
    bar.vertices[1][1] = y;
    bar.vertices[2][0] = 0.0f;
    bar.vertices[2][1] = y + 60.0f;
    bar.vertices[3][0] = static_cast<float>(frame.Width());
    bar.vertices[3][1] = y + 60.0f;
    list.items.push_back(bar);

    RenderStats stats;
    Render(list, frame, &stats);
    if (!target.Present(frame.Pixels(), frame.Width(), frame.Height(), frame.Width())) {
      std::fprintf(stderr, "present failed at frame %d\n", i);
      return 1;
    }
  }

  std::printf("presented %d frame(s)\n", frames);
  return 0;
}
