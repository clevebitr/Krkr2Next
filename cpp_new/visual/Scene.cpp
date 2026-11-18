#include "visual/Scene.h"

#include <algorithm>

namespace krkr::visual {

namespace {

Rect Intersect(const Rect &a, const Rect &b) {
  Rect out;
  out.left = std::max(a.left, b.left);
  out.top = std::max(a.top, b.top);
  out.right = std::min(a.right, b.right);
  out.bottom = std::min(a.bottom, b.bottom);
  return out;
}

bool IsEmpty(const Rect &rect) { return rect.Width() <= 0 || rect.Height() <= 0; }

void EmitOwnQuad(const SceneNode &node, int absX, int absY, const Rect &windowRect,
                 DrawList &out, SceneBuildStats &stats) {
  if (node.width <= 0 || node.height <= 0) {
    return;
  }
  const Texture *texture = node.texture.get();
  if (texture == nullptr && !node.hasFill) {
    return;
  }

  Rect clip = windowRect;
  if (node.hasClip) {
    const Rect localClip{absX + node.clip.left, absY + node.clip.top,
                         absX + node.clip.right, absY + node.clip.bottom};
    clip = Intersect(clip, localClip);
  }
  if (IsEmpty(clip)) {
    // 整块被裁掉：不是错误，但要让调用方能区分"没画"与"算错"。
    ++stats.itemsClippedOut;
    return;
  }

  DrawItem item;
  item.textureId = texture != nullptr ? out.AddTexture(texture) : 0;
  item.color = node.hasFill ? node.fill : 0xffffffffu;
  item.blend = static_cast<LayerType>(node.type);
  item.opacity = node.opacity > 255 ? 255 : node.opacity;
  item.clip = clip;

  const float left = static_cast<float>(absX);
  const float top = static_cast<float>(absY);
  const float right = static_cast<float>(absX + node.width);
  const float bottom = static_cast<float>(absY + node.height);
  item.vertices[0][0] = left;
  item.vertices[0][1] = top;
  item.vertices[1][0] = right;
  item.vertices[1][1] = top;
  item.vertices[2][0] = left;
  item.vertices[2][1] = bottom;
  item.vertices[3][0] = right;
  item.vertices[3][1] = bottom;

  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  if (texture != nullptr && texture->width > 0 && texture->height > 0) {
    const int imageWidth = node.imageWidth > 0 ? node.imageWidth : texture->width;
    const int imageHeight = node.imageHeight > 0 ? node.imageHeight : texture->height;
    const float width = static_cast<float>(texture->width);
    const float height = static_cast<float>(texture->height);
    u0 = std::clamp(static_cast<float>(node.imageLeft) / width, 0.0f, 1.0f);
    v0 = std::clamp(static_cast<float>(node.imageTop) / height, 0.0f, 1.0f);
    u1 = std::clamp(static_cast<float>(node.imageLeft + imageWidth) / width, 0.0f, 1.0f);
    v1 = std::clamp(static_cast<float>(node.imageTop + imageHeight) / height, 0.0f, 1.0f);
  }
  item.uvs[0][0] = u0;
  item.uvs[0][1] = v0;
  item.uvs[1][0] = u1;
  item.uvs[1][1] = v0;
  item.uvs[2][0] = u0;
  item.uvs[2][1] = v1;
  item.uvs[3][0] = u1;
  item.uvs[3][1] = v1;

  item.dirty = clip;
  // 图层 opacity 只作用于本层位图，不向子层累积（与参照实现一致）。
  out.items.push_back(item);
  ++stats.itemsEmitted;
}

void Walk(const SceneNode &node, int originX, int originY, const Rect &windowRect,
          DrawList &out, SceneBuildStats &stats) {
  if (!node.visible) {
    ++stats.nodesHidden;
    return;
  }
  ++stats.nodesVisited;

  const int absX = originX + node.left;
  const int absY = originY + node.top;

  if (node.type != ltBinder) {
    if (!IsLayerOpDrawable(node.type)) {
      ++stats.nodesUnsupported;
    } else {
      EmitOwnQuad(node, absX, absY, windowRect, out, stats);
    }
  }

  // 子层原点就是本层原点：left/top 相对父层，而不是相对父层位图的左上角。
  for (const std::shared_ptr<SceneNode> &child : node.children) {
    if (child != nullptr) {
      Walk(*child, absX, absY, windowRect, out, stats);
    }
  }
}

std::size_t CountRecursive(const SceneNode &node) {
  std::size_t count = 1;
  for (const std::shared_ptr<SceneNode> &child : node.children) {
    if (child != nullptr) {
      count += CountRecursive(*child);
    }
  }
  return count;
}

} // namespace

void AttachNode(const std::shared_ptr<SceneNode> &parent,
                const std::shared_ptr<SceneNode> &child) {
  if (parent == nullptr || child == nullptr || parent == child) {
    return;
  }
  DetachNode(child);
  child->parent = parent.get();
  parent->children.push_back(child);
}

void DetachNode(const std::shared_ptr<SceneNode> &child) {
  if (child == nullptr || child->parent == nullptr) {
    return;
  }
  SceneNode *parent = child->parent;
  child->parent = nullptr;
  const auto same = [&child](const std::shared_ptr<SceneNode> &item) { return item == child; };
  parent->children.erase(std::remove_if(parent->children.begin(), parent->children.end(), same),
                         parent->children.end());
}

Scene::Scene(int width, int height) : width_(width), height_(height) {
  root_ = std::make_shared<SceneNode>();
  root_->name = "<window>";
  root_->visible = true;
  root_->type = ltBinder;
  root_->width = width;
  root_->height = height;
}

bool Scene::SetSize(int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  width_ = width;
  height_ = height;
  root_->width = width;
  root_->height = height;
  return true;
}

void Scene::BuildDrawList(DrawList &out, SceneBuildStats *statsOut) const {
  SceneBuildStats stats;
  out.Reset(clearColor);
  if (root_ == nullptr) {
    if (statsOut != nullptr) {
      *statsOut = stats;
    }
    return;
  }
  const Rect windowRect{0, 0, width_, height_};
  Walk(*root_, 0, 0, windowRect, out, stats);
  if (statsOut != nullptr) {
    *statsOut = stats;
  }
}

std::size_t Scene::CountNodes() const { return root_ == nullptr ? 0 : CountRecursive(*root_); }

} // namespace krkr::visual
