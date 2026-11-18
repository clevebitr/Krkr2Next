#include "visual/Blend.h"

#include <algorithm>

namespace krkr::visual {

namespace {

std::uint32_t Channel(std::uint32_t pixel, int shift) {
  return (pixel >> shift) & 0xffu;
}

std::uint32_t Pack(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
  return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace

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
      const std::uint32_t ar = MulDiv255(sr, a);
      const std::uint32_t ag = MulDiv255(sg, a);
      const std::uint32_t ab = MulDiv255(sb, a);
      return Pack(0xffu, dr > ar ? dr - ar : 0u, dg > ag ? dg - ag : 0u,
                  db > ab ? db - ab : 0u);
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

} // namespace krkr::visual
