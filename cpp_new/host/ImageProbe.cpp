#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include "core/io/Storage.h"
#include "visual/DrawList.h"
#include "visual/Image.h"
#include "visual/ImageDecoder.h"
#include "visual/SoftwareBackend.h"
#include "visual/Surface.h"

// 图片解码 + 端到端渲染测试台（8a 的验收项）。
//
//   image_probe <root-path-file> <storage-name> [输出.ppm]
//
// 做的事：Storage 读出字节 → 按 magic 判格式 → 解码成 Image →
// 作为纹理铺一次 DrawList → 软件后端合成 → 写 PPM。
// 退出码 77 = 输入不存在（没有游戏包的环境），CTest 记为 skip。

namespace {

int failures = 0;

void Check(bool condition, const char *what) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) {
    ++failures;
  }
}

bool ReadAll(krkr::io::IStream &stream, std::string &out) {
  stream.Seek(0);
  out.clear();
  char buffer[64 * 1024];
  std::int64_t read = 0;
  while ((read = stream.Read(buffer, static_cast<std::int64_t>(sizeof(buffer)))) > 0) {
    out.append(buffer, static_cast<std::size_t>(read));
  }
  return read == 0;
}

// FNV-1a：把"解码结果没变"变成可打印、可比较的一行。
std::uint64_t Checksum(const krkr::visual::Image &image) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const std::uint32_t pixel : image.pixels) {
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= (pixel >> shift) & 0xffu;
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

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

} // namespace

int main(int argc, char **argv) {
  using namespace krkr::visual;

  if (argc < 3) {
    std::fprintf(stderr, "usage: image_probe <root-path-file> <storage-name> [out.ppm]\n");
    return 2;
  }

  std::string root;
  if (!ReadPathFile(argv[1], root)) {
    std::printf("SKIP  cannot read root path file %s\n", argv[1]);
    return 77;
  }

  krkr::io::Storage storage(root);
  if (!storage.Mounted()) {
    std::printf("SKIP  cannot mount %s: %s\n", root.c_str(), storage.GetLastErrorText());
    return 77;
  }

  std::unique_ptr<krkr::io::IStream> stream = storage.Open(argv[2]);
  if (stream == nullptr) {
    std::printf("SKIP  %s not found: %s\n", argv[2], storage.GetLastErrorText());
    return 77;
  }

  std::string bytes;
  Check(ReadAll(*stream, bytes), "storage read returns the whole file");
  const std::int64_t expectedSize = stream->GetSize();
  Check(static_cast<std::int64_t>(bytes.size()) == expectedSize,
        "read size matches GetSize()");
  std::printf("      %s: %lld byte(s)\n", argv[2], static_cast<long long>(expectedSize));

  const char *format = DetectImageFormat(bytes.data(), bytes.size());
  std::printf("      format = %s\n", format);

  Image image;
  std::string error;
  if (!DecodeImage(bytes.data(), bytes.size(), image, error)) {
    std::printf("FAIL  decode: %s\n", error.c_str());
    return 1;
  }
  Check(image.IsValid(), "decoded image has valid dimensions");
  std::printf("      %dx%d, %zu pixel(s), fnv1a=0x%016llX\n", image.width, image.height,
              image.pixels.size(), static_cast<unsigned long long>(Checksum(image)));

  // 端到端：解码结果当纹理，铺满一个按屏幕比例缩放的四边形。
  const int targetWidth = 320;
  const int targetHeight = image.width > 0
                               ? targetWidth * image.height / image.width
                               : targetWidth;
  Surface surface(targetWidth, targetHeight);
  DrawList list;
  list.Reset(0xff000000u);

  Texture texture;
  texture.width = image.width;
  texture.height = image.height;
  texture.pixels = image.pixels;
  const std::uint32_t textureId = list.AddTexture(&texture);

  DrawItem item;
  item.textureId = textureId;
  item.blend = ltOpaque;
  item.opacity = 255;
  item.vertices[0][0] = 0.0f;
  item.vertices[0][1] = 0.0f;
  item.vertices[1][0] = static_cast<float>(targetWidth);
  item.vertices[1][1] = 0.0f;
  item.vertices[2][0] = 0.0f;
  item.vertices[2][1] = static_cast<float>(targetHeight);
  item.vertices[3][0] = static_cast<float>(targetWidth);
  item.vertices[3][1] = static_cast<float>(targetHeight);
  item.uvs[0][0] = 0.0f;
  item.uvs[0][1] = 0.0f;
  item.uvs[1][0] = 1.0f;
  item.uvs[1][1] = 0.0f;
  item.uvs[2][0] = 0.0f;
  item.uvs[2][1] = 1.0f;
  item.uvs[3][0] = 1.0f;
  item.uvs[3][1] = 1.0f;
  list.items.push_back(item);

  RenderStats stats;
  Check(Render(list, surface, &stats), "software backend renders the decoded texture");
  Check(stats.itemsDrawn == 1, "one textured item is drawn");
  Check(stats.pixelsWritten == static_cast<std::size_t>(targetWidth * targetHeight),
        "every target pixel was written");

  if (argc >= 4) {
    Check(surface.SavePpm(argv[3]), "PPM written");
    std::printf("      wrote %s\n", argv[3]);
  }

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
