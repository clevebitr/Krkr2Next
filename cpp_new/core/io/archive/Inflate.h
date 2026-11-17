#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace krkr::io {
  enum class InflateFormat {
    RawDeflate,
    Zlib,
  };

  bool Inflate(InflateFormat format, const std::uint8_t *src, std::size_t srcSize,
               std::uint8_t *dst, std::size_t dstSize, std::string &errorOut);

  std::uint32_t Adler32(const std::uint8_t *data, std::size_t size);
} // namespace krkr::io
