#include "core/io/IoName.h"

namespace krkr::io {

std::string NormalizeStorageName(const std::string &name) {
  std::string slashed;
  slashed.reserve(name.size());
  for (const char ch : name) {
    slashed.push_back(ch == '\\' ? '/' : ch);
  }

  std::string cleaned;
  cleaned.reserve(slashed.size());
  for (const char ch : slashed) {
    if (ch == '/' && !cleaned.empty() && cleaned.back() == '/') {
      continue;
    }
    cleaned.push_back(ch);
  }

  while (cleaned.size() >= 2 && cleaned[0] == '.' && cleaned[1] == '/') {
    cleaned.erase(0, 2);
  }
  return cleaned;
}

} // namespace krkr::io
