#pragma once

#include <filesystem>
#include <string>

namespace krkr::io {
  std::filesystem::path PathFromUtf8(const std::string &utf8);

  std::string PathToUtf8(const std::filesystem::path &path);
} // namespace krkr::io
