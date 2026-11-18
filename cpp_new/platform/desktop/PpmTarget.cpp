#include "platform/desktop/PpmTarget.h"

#include <cstdio>

namespace krkr::platform {

PpmTarget::PpmTarget(const std::string &pathUtf8, int width, int height)
    : path_(pathUtf8), width_(width), height_(height) {}

bool PpmTarget::Present(const std::uint32_t *pixels, int width, int height, int stride) {
  if (pixels == nullptr || width <= 0 || height <= 0 || stride < width) {
    return false;
  }
  std::FILE *file = std::fopen(path_.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  std::fprintf(file, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::uint32_t pixel = pixels[static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(stride) +
                                         static_cast<std::size_t>(x)];
      const unsigned char rgb[3] = {
        static_cast<unsigned char>((pixel >> 16) & 0xffu),
        static_cast<unsigned char>((pixel >> 8) & 0xffu),
        static_cast<unsigned char>(pixel & 0xffu)};
      std::fwrite(rgb, 1, sizeof(rgb), file);
    }
  }
  std::fclose(file);
  return true;
}

} // namespace krkr::platform
