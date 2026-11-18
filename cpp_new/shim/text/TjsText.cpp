#include "shim/text/TjsText.h"

#include <cstdint>

namespace krkr::script {

namespace {

void AppendUtf8(std::string &out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

std::uint32_t ReadUtf16Unit(const std::string &bytes, std::size_t offset) {
  return static_cast<unsigned char>(bytes[offset]) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8);
}

} // namespace

std::string Utf8FromWide(const wchar_t *text) {
  std::string out;
  if (text == nullptr) {
    return out;
  }
  for (const wchar_t *p = text; *p != 0; ++p) {
    std::uint32_t cp = static_cast<std::uint32_t>(*p);
    if (sizeof(wchar_t) == 2 && cp >= 0xd800 && cp <= 0xdbff && p[1] != 0) {
      const std::uint32_t low = static_cast<std::uint32_t>(p[1]);
      if (low >= 0xdc00 && low <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        ++p;
      }
    }
    AppendUtf8(out, cp);
  }
  return out;
}

std::wstring WideFromUtf8(const std::string &utf8) {
  std::wstring out;
  out.reserve(utf8.size());
  std::size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    std::uint32_t cp = lead;
    std::size_t extra = 0;
    if (lead >= 0xf0) {
      cp = lead & 0x07;
      extra = 3;
    } else if (lead >= 0xe0) {
      cp = lead & 0x0f;
      extra = 2;
    } else if (lead >= 0xc0) {
      cp = lead & 0x1f;
      extra = 1;
    }
    if (i + extra >= utf8.size()) {
      break;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3f);
    }
    i += extra + 1;
    if (sizeof(wchar_t) == 2 && cp > 0xffff) {
      cp -= 0x10000;
      out.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10)));
      out.push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff)));
    } else {
      out.push_back(static_cast<wchar_t>(cp));
    }
  }
  return out;
}

std::string DecodeScriptSource(const std::string &bytes) {
  if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
      static_cast<unsigned char>(bytes[1]) == 0xfe) {
    std::string out;
    out.reserve((bytes.size() - 2) / 2);
    std::size_t i = 2;
    while (i + 1 < bytes.size()) {
      const std::uint32_t unit = ReadUtf16Unit(bytes, i);
      i += 2;
      if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < bytes.size()) {
        const std::uint32_t low = ReadUtf16Unit(bytes, i);
        if (low >= 0xdc00 && low <= 0xdfff) {
          i += 2;
          AppendUtf8(out, 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
          continue;
        }
      }
      AppendUtf8(out, unit);
    }
    return out;
  }
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf) {
    return bytes.substr(3);
  }
  return bytes;
}

} // namespace krkr::script
