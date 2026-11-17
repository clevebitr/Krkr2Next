#include "core/io/archive/Xp3Stream.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "core/io/archive/Inflate.h"

namespace krkr::io {

namespace {
constexpr std::size_t kNoSegment = static_cast<std::size_t>(-1);
} // namespace

std::unique_ptr<IStream> Xp3Stream::Open(const std::filesystem::path &archivePath,
                                        const Xp3Entry &entry, std::string &errorOut) {
  if (entry.segments.empty()) {
    errorOut = "entry \"" + entry.name + "\" has no segments";
    return nullptr;
  }
  std::unique_ptr<Xp3Stream> stream(new Xp3Stream());

  stream->file_.open(archivePath, std::ios::binary);
  if (!stream->file_.is_open()) {
    errorOut = "cannot open archive for entry \"" + entry.name + "\"";
    return nullptr;
  }

  stream->segments_ = entry.segments;
  stream->size_ = static_cast<std::int64_t>(entry.orgSize);

  std::int64_t expected = 0;
  for (const auto &segment : stream->segments_) {
    if (static_cast<std::int64_t>(segment.offsetInFile) != expected) {
      errorOut = "segment table of \"" + entry.name + "\" is not contiguous";
      return nullptr;
    }
    expected += static_cast<std::int64_t>(segment.orgSize);
  }
  if (expected != stream->size_) {
    errorOut = "segment sizes of \"" + entry.name + "\" do not add up to " +
               std::to_string(stream->size_);
    return nullptr;
  }

  errorOut.clear();
  return stream;
}

bool Xp3Stream::EnsureSegment(std::size_t index, std::string &errorOut) {
  if (cacheIndex_ == index) {
    return true;
  }
  if (index >= segments_.size()) {
    errorOut = "segment index out of range";
    return false;
  }

  const Xp3Segment &segment = segments_[index];
  const std::size_t arcSize = static_cast<std::size_t>(segment.arcSize);
  const std::size_t orgSize = static_cast<std::size_t>(segment.orgSize);

  std::vector<std::uint8_t> raw(arcSize);
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(segment.start), std::ios::beg);
  if (!file_) {
    errorOut = "cannot seek to segment data";
    return false;
  }
  file_.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(arcSize));
  if (static_cast<std::size_t>(file_.gcount()) != arcSize) {
    errorOut = "short read while loading segment (archive truncated?)";
    return false;
  }

  if (segment.compressed) {
    cache_.resize(orgSize);
    if (!Inflate(InflateFormat::Zlib, raw.data(), arcSize, cache_.data(), orgSize,
                 errorOut)) {
      return false;
    }
  } else {
    if (arcSize != orgSize) {
      errorOut = "uncompressed segment has mismatched sizes";
      return false;
    }
    cache_ = std::move(raw);
  }

  cacheIndex_ = index;
  return true;
}

std::int64_t Xp3Stream::Read(void *buffer, std::int64_t bytes) {
  if (buffer == nullptr || bytes < 0) {
    return -1;
  }
  if (bytes == 0 || position_ >= size_) {
    return 0;
  }

  auto *out = static_cast<std::uint8_t *>(buffer);
  std::int64_t remaining = std::min<std::int64_t>(bytes, size_ - position_);
  std::int64_t written = 0;
  std::string error;

  while (remaining > 0) {
    std::size_t segmentIndex = kNoSegment;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
      const std::int64_t start = static_cast<std::int64_t>(segments_[i].offsetInFile);
      const std::int64_t end = start + static_cast<std::int64_t>(segments_[i].orgSize);
      if (position_ >= start && position_ < end) {
        segmentIndex = i;
        break;
      }
    }
    if (segmentIndex == kNoSegment) {
      break;
    }
    if (!EnsureSegment(segmentIndex, error)) {
      return written > 0 ? written : -1;
    }

    const Xp3Segment &segment = segments_[segmentIndex];
    const std::int64_t segmentStart = static_cast<std::int64_t>(segment.offsetInFile);
    const std::int64_t offsetInSegment = position_ - segmentStart;
    const std::int64_t available = static_cast<std::int64_t>(segment.orgSize) - offsetInSegment;
    const std::int64_t chunk = std::min(remaining, available);

    std::memcpy(out + written, cache_.data() + offsetInSegment,
                static_cast<std::size_t>(chunk));
    written += chunk;
    position_ += chunk;
    remaining -= chunk;
  }

  return written;
}

bool Xp3Stream::Seek(std::int64_t offset) {
  if (offset < 0 || offset > size_) {
    return false;
  }
  position_ = offset;
  return true;
}

std::int64_t Xp3Stream::GetSize() const { return size_; }

std::int64_t Xp3Stream::GetPosition() const { return position_; }

} // namespace krkr::io
