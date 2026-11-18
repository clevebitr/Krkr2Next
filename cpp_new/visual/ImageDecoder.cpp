#include "visual/ImageDecoder.h"

#include <cstring>

#include "visual/third_party/stb_image.h"

namespace krkr::visual {

namespace {

bool StartsWith(const void *data, std::size_t size, const std::uint8_t *magic,
                std::size_t magicSize) {
  return size >= magicSize && std::memcmp(data, magic, magicSize) == 0;
}

} // namespace

const char *DetectImageFormat(const void *data, std::size_t size) {
  static const std::uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  static const std::uint8_t kJpeg[] = {0xff, 0xd8, 0xff};
  static const std::uint8_t kBmp[] = {'B', 'M'};
  static const std::uint8_t kTlg5[] = {'T', 'L', 'G', '5', '.', '0'};
  static const std::uint8_t kTlg6[] = {'T', 'L', 'G', '6', '.', '0'};

  if (StartsWith(data, size, kPng, sizeof(kPng))) {
    return "png";
  }
  if (StartsWith(data, size, kJpeg, sizeof(kJpeg))) {
    return "jpeg";
  }
  if (StartsWith(data, size, kBmp, sizeof(kBmp))) {
    return "bmp";
  }
  if (StartsWith(data, size, kTlg5, sizeof(kTlg5))) {
    return "tlg5";
  }
  if (StartsWith(data, size, kTlg6, sizeof(kTlg6))) {
    return "tlg6";
  }
  return "unknown";
}

bool DecodeImage(const void *data, std::size_t size, Image &out, std::string &error) {
  out = Image{};
  if (data == nullptr || size == 0) {
    error = "empty input";
    return false;
  }

  const char *format = DetectImageFormat(data, size);
  if (std::strcmp(format, "tlg5") == 0 || std::strcmp(format, "tlg6") == 0) {
    // TLG 是 KiriKiri 自有格式，需要单独的解码器（参照 LoadTLG.cpp）。
    error = std::string("format ") + format + " is not implemented yet";
    return false;
  }
  if (std::strcmp(format, "unknown") == 0) {
    error = "unrecognized image format";
    return false;
  }

  int width = 0;
  int height = 0;
  int components = 0;
  // 统一要 RGBA，避免分支；alpha 缺失时 stb 会补 255。
  stbi_uc *decoded = stbi_load_from_memory(static_cast<const stbi_uc *>(data),
                                           static_cast<int>(size), &width, &height,
                                           &components, 4);
  if (decoded == nullptr) {
    const char *reason = stbi_failure_reason();
    error = std::string("decode failed: ") + (reason != nullptr ? reason : "unknown");
    return false;
  }

  out.width = width;
  out.height = height;
  out.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (std::size_t i = 0; i < out.pixels.size(); ++i) {
    const std::uint32_t r = decoded[i * 4 + 0];
    const std::uint32_t g = decoded[i * 4 + 1];
    const std::uint32_t b = decoded[i * 4 + 2];
    const std::uint32_t a = decoded[i * 4 + 3];
    out.pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
  }

  stbi_image_free(decoded);
  if (!out.IsValid()) {
    error = "decoded image has invalid dimensions";
    out = Image{};
    return false;
  }
  return true;
}

} // namespace krkr::visual
