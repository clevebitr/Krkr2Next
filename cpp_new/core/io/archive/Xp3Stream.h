#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/io/IOStream.h"
#include "core/io/archive/Xp3Index.h"

namespace krkr::io {
  class Xp3Stream final : public IStream {
    public:
      static std::unique_ptr<IStream> Open(const std::filesystem::path &archivePath,
                                           const Xp3Entry &entry, std::string &errorOut);

      std::int64_t Read(void *buffer, std::int64_t bytes) override;
      bool Seek(std::int64_t offset) override;
      std::int64_t GetSize() const override;
      std::int64_t GetPosition() const override;

    private:
      Xp3Stream() = default;

      bool EnsureSegment(std::size_t index, std::string &errorOut);

      std::ifstream file_;
      std::vector<Xp3Segment> segments_;
      std::int64_t size_ = 0;
      std::int64_t position_ = 0;

      std::size_t cacheIndex_ = static_cast<std::size_t>(-1);
      std::vector<std::uint8_t> cache_;
  };
} // namespace krkr::io
