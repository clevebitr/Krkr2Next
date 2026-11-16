#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "core/io/IOStream.h"

namespace krkr::io {
  std::unique_ptr<IStream> OpenDirectoryFile(const std::filesystem::path &path,
                                            std::string &errorOut);
} // namespace krkr::io
