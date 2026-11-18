#pragma once

#include <cstdint>

namespace krkr::render {
  // 呈现目标：平台层实现（窗口、Surface、离屏文件），只接受像素，不认识 visual 类型。
  //
  // 方向：core 定义契约 → platform 实现 → engine/host 组装；
  // visual 与 platform 互不引用，二者只在本接口相遇。
  class IRenderTarget {
    public:
      virtual ~IRenderTarget() = default;

      virtual bool IsValid() const = 0;
      virtual int GetWidth() const = 0;
      virtual int GetHeight() const = 0;

      // 提交一帧 0xAARRGGBB 像素。stride 以像素为单位（可大于 width）。
      // 返回 false 表示这一帧没有被接受；调用方据此记录并降级，不得静默忽略。
      virtual bool Present(const std::uint32_t *pixels, int width, int height,
                           int stride) = 0;

      // 处理平台消息（桌面窗口必需；无窗口的实现返回 true）。
      // 返回 false 表示窗口已关闭或会话结束。
      virtual bool PumpEvents() = 0;
  };
} // namespace krkr::render
