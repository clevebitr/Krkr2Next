#pragma once

#include <cstdint>
#include <memory>

namespace krkr::io {
  class IStream {
    public:
      virtual ~IStream() = default;

      virtual std::int64_t Read(void *buffer, std::int64_t bytes) = 0;

      virtual bool Seek(std::int64_t offset) = 0;

      virtual std::int64_t GetSize() const = 0;
      virtual std::int64_t GetPosition() const = 0;
  };
} // namespace krkr::io
