#include "visual/SoftwareBackend.h"

#include <algorithm>
#include <cmath>

#include "pixel/Blend.h"

namespace krkr::visual {

namespace {

bool IsAxisAligned(const DrawItem &item) {
  // 轴对齐判定：上边水平、左边垂直（容许 0.001 的浮点误差）。
  const float epsilon = 0.001f;
  const bool topLevel = std::fabs(item.vertices[0][1] - item.vertices[1][1]) < epsilon;
  const bool bottomLevel = std::fabs(item.vertices[2][1] - item.vertices[3][1]) < epsilon;
  const bool leftLevel = std::fabs(item.vertices[0][0] - item.vertices[2][0]) < epsilon;
  const bool rightLevel = std::fabs(item.vertices[1][0] - item.vertices[3][0]) < epsilon;
  return topLevel && bottomLevel && leftLevel && rightLevel;
}

Rect ToRect(const DrawItem &item) {
  Rect rect;
  rect.left = static_cast<int>(std::floor(std::min(item.vertices[0][0], item.vertices[2][0]) + 0.5f));
  rect.top = static_cast<int>(std::floor(std::min(item.vertices[0][1], item.vertices[1][1]) + 0.5f));
  rect.right = static_cast<int>(std::floor(std::max(item.vertices[1][0], item.vertices[3][0]) + 0.5f));
  rect.bottom = static_cast<int>(std::floor(std::max(item.vertices[2][1], item.vertices[3][1]) + 0.5f));
  return rect;
}

Rect Intersect(const Rect &a, const Rect &b) {
  Rect out;
  out.left = std::max(a.left, b.left);
  out.top = std::max(a.top, b.top);
  out.right = std::min(a.right, b.right);
  out.bottom = std::min(a.bottom, b.bottom);
  return out;
}

bool IsEmpty(const Rect &rect) { return rect.Width() <= 0 || rect.Height() <= 0; }

void RenderItem(const DrawItem &item, const Texture *texture, Surface &target,
                RenderStats &stats) {
  const Rect destination = ToRect(item);
  Rect clip = Intersect(destination, {0, 0, target.Width(), target.Height()});
  if (!IsEmpty(item.clip)) {
    clip = Intersect(clip, item.clip);
  }
  if (IsEmpty(clip)) {
    ++stats.itemsSkipped;
    return;
  }

  const int destinationWidth = destination.Width();
  const int destinationHeight = destination.Height();
  const std::uint32_t alpha = item.opacity > 255 ? 255 : item.opacity;

  for (int y = clip.top; y < clip.bottom; ++y) {
    const int dy = y - destination.top;
    for (int x = clip.left; x < clip.right; ++x) {
      const int dx = x - destination.left;

      std::uint32_t source = item.color;
      if (texture != nullptr && texture->width > 0 && texture->height > 0) {
        // 最近邻采样：8a 不做插值，缩放断言因此是精确值。
        const int sx = destinationWidth > 0
                           ? std::min(texture->width - 1,
                                      dx * texture->width / destinationWidth)
                           : 0;
        const int sy = destinationHeight > 0
                           ? std::min(texture->height - 1,
                                      dy * texture->height / destinationHeight)
                           : 0;
        source = texture->Row(sy)[sx];
      }

      const std::uint32_t dst = target.Get(x, y);
      target.Pixels()[static_cast<std::size_t>(y) * static_cast<std::size_t>(target.Width()) +
                       static_cast<std::size_t>(x)] =
          krkr::pixel::BlendPixel(dst, source, item.blend, alpha);
      ++stats.pixelsWritten;
    }
  }
  ++stats.itemsDrawn;
}

} // namespace

bool Render(const DrawList &list, Surface &target, RenderStats *statsOut) {
  RenderStats stats;

  if (list.clear) {
    target.Clear(list.clearColor);
  }

  for (const DrawItem &item : list.items) {
    if (!IsAxisAligned(item)) {
      // 旋转/斜切属后续分片；跳过而不是用错误的采样糊过去。
      ++stats.itemsSkipped;
      continue;
    }
    RenderItem(item, list.Resolve(item.textureId), target, stats);
  }

  if (statsOut != nullptr) {
    *statsOut = stats;
  }
  return true;
}

} // namespace krkr::visual
