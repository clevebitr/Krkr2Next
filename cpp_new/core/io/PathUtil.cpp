#include "core/io/PathUtil.h"

namespace krkr::io {

    std::filesystem::path PathFromUtf8(const std::string &utf8) {
        const auto *raw = reinterpret_cast<const char8_t *>(utf8.data());
        return std::filesystem::path(std::u8string(raw, utf8.size()));
    }

    std::string PathToUtf8(const std::filesystem::path &path) {
        const std::u8string u8 = path.generic_u8string();
        return std::string(reinterpret_cast<const char *>(u8.data()),
                           u8.size());
    }
} // namespace krkr::io
