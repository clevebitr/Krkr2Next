#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace krkr::io {
  struct Xp3Segment {
    std::uint32_t flags        = 0;
    bool compressed            = false;      
    std::uint64_t start        = 0;  
    std::uint64_t offsetInFile = 0;
    std::uint64_t orgSize      = 0;
    std::uint64_t arcSize      = 0;
  };

  struct Xp3Entry {
    std::string name;            
    std::string key;             
    std::uint32_t flags = 0;
    std::uint64_t orgSize = 0;
    std::uint64_t arcSize = 0;
    std::vector<Xp3Segment> segments;
  };

  class Xp3Index final {
    public:
      Xp3Index();
      ~Xp3Index();

      Xp3Index(const Xp3Index &) = delete;
      Xp3Index &operator = (const Xp3Index &) = delete;

      bool Load(const std::string &archivePathUtf8);

      const std::vector<Xp3Entry> &GetEntries() const;

      const Xp3Entry *Find(const std::string &name) const;

      std::uint64_t GetArchiveSize() const;
      std::uint64_t GetIndexOffset() const;
      std::uint64_t GetIndexSize() const;
      std::uint32_t GetDuplicateCount() const;

      const char *GetLastErrorText() const;

    private:
      class Impl;
      std::unique_ptr<Impl> impl_;
  };
} // namespace krkr::io
