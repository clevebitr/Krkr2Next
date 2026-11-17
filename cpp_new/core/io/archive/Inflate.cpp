#include "core/io/archive/Inflate.h"

#include "third_party/puff.h"

namespace krkr::io {

std::uint32_t Adler32(const std::uint8_t *data, std::size_t size) {
  constexpr std::uint32_t kMod = 65521;
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (std::size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % kMod;
    b = (b + a) % kMod;
  }
  return (b << 16) | a;
}

bool Inflate(InflateFormat format, const std::uint8_t *src, std::size_t srcSize,
             std::uint8_t *dst, std::size_t dstSize, std::string &errorOut) {
  if (src == nullptr || dst == nullptr) {
    errorOut = "inflate: null buffer";
    return false;
  }

  std::size_t offset = 0;
  std::uint32_t expectedAdler = 0;
  if (format == InflateFormat::Zlib) {
    if (srcSize < 6) {
      errorOut = "inflate: zlib stream is too short";
      return false;
    }
    const std::uint8_t cmf = src[0];
    const std::uint8_t flg = src[1];
    if ((cmf & 0x0f) != 8) {
      errorOut = "inflate: not a deflate stream (CM=" + std::to_string(cmf & 0x0f) + ")";
      return false;
    }
    if (((static_cast<std::uint32_t>(cmf) << 8) | flg) % 31 != 0) {
      errorOut = "inflate: bad zlib header check bits";
      return false;
    }
    if ((flg & 0x20) != 0) {
      errorOut = "inflate: preset dictionary is not supported";
      return false;
    }
    offset = 2;
    // adler32 在流尾部
    expectedAdler = (static_cast<std::uint32_t>(src[srcSize - 4]) << 24) |
                    (static_cast<std::uint32_t>(src[srcSize - 3]) << 16) |
                    (static_cast<std::uint32_t>(src[srcSize - 2]) << 8) |
                    static_cast<std::uint32_t>(src[srcSize - 1]);
  }

  unsigned long destLen = static_cast<unsigned long>(dstSize);
  unsigned long srcLen = static_cast<unsigned long>(srcSize - offset);
  const int result = puff(dst, &destLen, src + offset, &srcLen);
  if (result != 0) {
    errorOut = "inflate: puff failed (code " + std::to_string(result) + ")";
    return false;
  }
  if (destLen != static_cast<unsigned long>(dstSize)) {
    errorOut = "inflate: produced " + std::to_string(destLen) + " bytes, expected " +
               std::to_string(dstSize);
    return false;
  }

  if (format == InflateFormat::Zlib) {
    const std::uint32_t actual = Adler32(dst, dstSize);
    if (actual != expectedAdler) {
      errorOut = "inflate: adler32 mismatch (data is corrupt)";
      return false;
    }
  }
  errorOut.clear();
  return true;
}

} // namespace krkr::io
