#include "visual/Surface.h"

#include <algorithm>
#include <cstdio>

namespace krkr::visual {

namespace {

int Clamp(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

std::uint32_t Channel(std::uint32_t pixel, int shift) {
  return (pixel >> shift) & 0xffu;
}

std::uint32_t Pack(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
  return (a << 24) | (r << 16) | (g << 8) | b;
}

// 8 位通道乘除：先乘后除、四舍五入，避免逐分量累积误差。
std::uint32_t MulDiv255(std::uint32_t value, std::uint32_t scale) {
  return (value * scale + 127u) / 255u;
}

std::uint32_t BlendPixel(std::uint32_t dst, std::uint32_t src, LayerType type,
                        std::uint32_t alpha) {
  const std::uint32_t sr = Channel(src, 16);
  const std::uint32_t sg = Channel(src, 8);
  const std::uint32_t sb = Channel(src, 0);
  const std::uint32_t dr = Channel(dst, 16);
  const std::uint32_t dg = Channel(dst, 8);
  const std::uint32_t db = Channel(dst, 0);

  switch (type) {
    case ltOpaque:
      return Pack(0xffu, sr, sg, sb);
    case ltAlpha: {
      const std::uint32_t a = MulDiv255(alpha, Channel(src, 24));
      const std::uint32_t keep = 255u - a;
      return Pack(0xffu, MulDiv255(sr, a) + MulDiv255(dr, keep),
                  MulDiv255(sg, a) + MulDiv255(dg, keep),
                  MulDiv255(sb, a) + MulDiv255(db, keep));
    }
    case ltAdditive: {
      const std::uint32_t a = MulDiv255(alpha, Channel(src, 24));
      return Pack(0xffu, std::min(255u, dr + MulDiv255(sr, a)),
                  std::min(255u, dg + MulDiv255(sg, a)),
                  std::min(255u, db + MulDiv255(sb, a)));
    }
    case ltSubtractive: {
      const std::uint32_t a = MulDiv255(alpha, Channel(src, 24));
      return Pack(0xffu, dr > MulDiv255(sr, a) ? dr - MulDiv255(sr, a) : 0u,
                  dg > MulDiv255(sg, a) ? dg - MulDiv255(sg, a) : 0u,
                  db > MulDiv255(sb, a) ? db - MulDiv255(sb, a) : 0u);
    }
    case ltMultiplicative: {
      const std::uint32_t a = MulDiv255(alpha, Channel(src, 24));
      const std::uint32_t keep = 255u - a;
      const std::uint32_t mr = MulDiv255(dr, sr);
      const std::uint32_t mg = MulDiv255(dg, sg);
      const std::uint32_t mb = MulDiv255(db, sb);
      return Pack(0xffu, MulDiv255(mr, a) + MulDiv255(dr, keep),
                  MulDiv255(mg, a) + MulDiv255(dg, keep),
                  MulDiv255(mb, a) + MulDiv255(db, keep));
    }
  }
  return dst;
}

} // namespace

Surface::Surface(int width, int height)
    : width_(width > 0 ? width : 0), height_(height > 0 ? height : 0),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0) {}

std::uint32_t Surface::Get(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    return 0;
  }
  return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                 static_cast<std::size_t>(x)];
}

void Surface::Clear(std::uint32_t argb) {
  std::fill(pixels_.begin(), pixels_.end(), argb);
}

void Surface::FillRect(const Rect &rect, std::uint32_t argb) {
  const int left = Clamp(rect.left, 0, width_);
  const int right = Clamp(rect.right, 0, width_);
  const int top = Clamp(rect.top, 0, height_);
  const int bottom = Clamp(rect.bottom, 0, height_);
  for (int y = top; y < bottom; ++y) {
    std::fill(pixels_.begin() + static_cast<std::size_t>(y) * width_ + left,
              pixels_.begin() + static_cast<std::size_t>(y) * width_ + right, argb);
  }
}

void Surface::Composite(const Surface &source, const Rect &sourceRect,
                        const Rect &destination, LayerType type, std::uint8_t opacity) {
  const int width = std::min(sourceRect.Width(), destination.Width());
  const int height = std::min(sourceRect.Height(), destination.Height());
  for (int dy = 0; dy < height; ++dy) {
    const int sy = sourceRect.top + dy;
    const int ty = destination.top + dy;
    if (ty < 0 || ty >= height_) {
      continue;
    }
    for (int dx = 0; dx < width; ++dx) {
      const int sx = sourceRect.left + dx;
      const int tx = destination.left + dx;
      if (tx < 0 || tx >= width_ || sx < 0 || sy < 0 || sx >= source.Width() ||
          sy >= source.Height()) {
        continue;
      }
      const std::uint32_t dst = Get(tx, ty);
      const std::uint32_t src = source.Get(sx, sy);
      Pixels()[static_cast<std::size_t>(ty) * width_ + static_cast<std::size_t>(tx)] =
          BlendPixel(dst, src, type, opacity);
    }
  }
}

bool Surface::SavePpm(const std::string &pathUtf8) const {
  std::FILE *file = std::fopen(pathUtf8.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  std::fprintf(file, "P6\n%d %d\n255\n", width_, height_);
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      const std::uint32_t pixel = Get(x, y);
      const unsigned char rgb[3] = {
        static_cast<unsigned char>(Channel(pixel, 16)),
        static_cast<unsigned char>(Channel(pixel, 8)),
        static_cast<unsigned char>(Channel(pixel, 0))};
      std::fwrite(rgb, 1, sizeof(rgb), file);
    }
  }
  std::fclose(file);
  return true;
}

} // namespace krkr::visual
