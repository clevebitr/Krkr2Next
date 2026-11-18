#pragma once

#include <cstdint>
#include <vector>

#include "visual/Surface.h"

namespace krkr::visual {
  // 32bpp 纹理：像素格式与 Surface 一致（0xAARRGGBB，直通 alpha）。
  struct Texture {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    const std::uint32_t *Row(int y) const {
      return pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    }
  };

  // 一次绘制：顶点已含变换（引擎侧算好），后端只做采样 + 混合。
  // 字段与 docs/render-spec.md §11.1 对齐；8a 只用到其中一部分。
  struct DrawItem {
    std::uint32_t textureId = 0;      // 0 = 纯色填充
    std::uint32_t maskTextureId = 0;  // 0 = 无遮罩（8d）
    std::uint32_t color = 0;          // textureId == 0 时的颜色
    float vertices[4][2] = {};        // TL, TR, BL, BR（目标坐标）
    float uvs[4][2] = {};             // 归一化纹理坐标
    LayerType blend = ltOpaque;
    std::uint32_t opacity = 255;      // 0..255，乘到源 alpha 上
    Rect clip = {};                   // 与目标求交后生效
    Rect dirty = {};                  // 该项影响的目标区域（8e 用）
  };

  // 一帧的绘制数据。后端不允许读引擎侧状态，只认这张表。
  struct DrawList {
    std::uint32_t clearColor = 0xff000000u;
    bool clear = true;
    // 纹理表：下标从 1 开始，0 表示"无纹理"。指针由调用方保证在本帧内有效。
    std::vector<const Texture *> textures{nullptr};
    std::vector<DrawItem> items;

    void Reset(std::uint32_t color) {
      clearColor = color;
      clear = true;
      textures.assign(1, nullptr);
      items.clear();
    }

    // 登记纹理并返回其 id。重复登记同一指针不会去重（调用方负责缓存）。
    std::uint32_t AddTexture(const Texture *texture) {
      textures.push_back(texture);
      return static_cast<std::uint32_t>(textures.size() - 1);
    }

    const Texture *Resolve(std::uint32_t textureId) const {
      return textureId < textures.size() ? textures[textureId] : nullptr;
    }
  };
} // namespace krkr::visual
