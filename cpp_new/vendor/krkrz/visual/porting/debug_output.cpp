#include "vendor/krkrz/visual/porting/debug_output.h"

#if !defined(_WIN32)
#include <string>

#include "core/log/Logger.h"

void OutputDebugString(const char *text) {
  if (text != nullptr) {
    krkr::log::Debug("[legacy] ", text);
  }
}

void OutputDebugString(const tjs_char *text) {
  if (text == nullptr) {
    return;
  }
  std::string utf8;
  for (const tjs_char *cursor = text; *cursor != 0; ++cursor) {
    const unsigned long code = static_cast<unsigned long>(*cursor);
    if (code < 0x80) {
      utf8.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      utf8.push_back(static_cast<char>(0xC0 | (code >> 6)));
      utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      utf8.push_back(static_cast<char>(0xE0 | (code >> 12)));
      utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      utf8.push_back(static_cast<char>(0xF0 | (code >> 18)));
      utf8.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }
  krkr::log::Debug("[legacy] ", utf8);
}
#endif
