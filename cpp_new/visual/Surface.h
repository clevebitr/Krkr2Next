#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace krkr::visual {
  // 图层类型原值，与参照实现的 tTVPLayerType 对齐（不重新编号，便于对照）。
  enum LayerType : std::uint32_t {
    ltOpaque = 1,
    ltAlpha = 2,
    ltAdditive = 3,
    ltSubtractive = 4,
    ltMultiplicative = 5,
  };

  struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
  };

  // 32bpp RGBA8888 表面，逐像素以 0xAARRGGBB 存储（与 KiriKiri 的位图一致）。
  //
  // 软件合成是本层的第一权威：GL 后端之后要以它为逐像素参照，
  // 因此这里的算子与舍入必须能独立测试（见 host/RenderProbe.cpp）。
  class Surface final {
    public:
      Surface(int width, int height);

      int Width() const { return width_; }
      int Height() const { return height_; }
      std::uint32_t *Pixels() { return pixels_.data(); }
      const std::uint32_t *Pixels() const { return pixels_.data(); }

      std::uint32_t Get(int x, int y) const;

      void Clear(std::uint32_t argb);
      void FillRect(const Rect &rect, std::uint32_t argb);

      // 把 source 的 sourceRect 区域按 type/opacity 合成到本表面的 destination 左上角。
      // 越界部分被裁剪，不做环绕。source 必须与 sourceRect 同尺寸语义（越界读同样裁剪）。
      void Composite(const Surface &source, const Rect &sourceRect,
                     const Rect &destination, LayerType type, std::uint8_t opacity);

      // PPM 便于人工查看与 CI 落盘；失败返回 false。
      bool SavePpm(const std::string &pathUtf8) const;

    private:
      int width_ = 0;
      int height_ = 0;
      std::vector<std::uint32_t> pixels_;
  };
} // namespace krkr::visual
