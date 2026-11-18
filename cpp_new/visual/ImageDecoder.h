#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "visual/Image.h"

namespace krkr::visual {
  // 按文件头判定格式，返回 "png" / "jpeg" / "bmp" / "tlg5" / "tlg6" / "unknown"。
  // 判定只看 magic，不依赖扩展名（包内扩展名与内容不一致的情况真实存在）。
  const char *DetectImageFormat(const void *data, std::size_t size);

  // 解码为 Image。失败返回 false，error 里带格式与失败原因（不含像素内容）。
  //
  // 尚未支持：TLG5/TLG6（KiriKiri 自有格式）。命中时显式报 "not implemented"，
  // 不用占位图掩盖。
  bool DecodeImage(const void *data, std::size_t size, Image &out, std::string &error);
} // namespace krkr::visual
