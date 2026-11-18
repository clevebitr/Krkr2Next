#include "core/io/archive/Xp3Index.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>

#include <cctype>

#include "core/io/IoName.h"
#include "core/io/PathUtil.h"
#include "core/log/Logger.h"

namespace krkr::io {

namespace {

constexpr std::uint8_t kMagic[11]         = {0x58, 0x50, 0x33, 0x0d, 0x0a, 0x20,
                                            0x0a, 0x1a, 0x8b, 0x67, 0x01};
constexpr std::size_t kMagicSize          = sizeof(kMagic);

constexpr std::uint64_t kMaxIndexBytes    = 64ull * 1024ull * 1024ull;

constexpr std::uint32_t kEncodeMethodMask = 0x07;
constexpr std::uint32_t kEncodeRaw        = 0;
constexpr std::uint32_t kSegmEncodeZlib   = 1;

constexpr std::size_t kSegmentRecordSize  = 28;
constexpr std::size_t kInfoHeaderSize     = 22;   
constexpr std::size_t kChunkHeaderSize    = 12;  

std::uint32_t ReadU32LE(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t ReadU64LE(const std::uint8_t *p) {
  std::uint64_t value = 0;
  for (int i = 7; i >= 0; --i) {
    value = (value << 8) | static_cast<std::uint64_t>(p[i]);
  }
  return value;
}

void AppendUtf8(std::string &out, std::uint32_t cp) {
  if (cp <= 0x7f) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp <= 0xffff) {
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

std::string Utf16LeToUtf8(const std::uint8_t *data, std::size_t units) {
  std::string out;
  out.reserve(units);
  for (std::size_t i = 0; i < units; ++i) {
    std::uint32_t unit = static_cast<std::uint32_t>(data[i * 2]) |
                         (static_cast<std::uint32_t>(data[i * 2 + 1]) << 8);
    if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < units) {
      const std::uint32_t low = static_cast<std::uint32_t>(data[(i + 1) * 2]) |
                                (static_cast<std::uint32_t>(data[(i + 1) * 2 + 1]) << 8);
      if (low >= 0xdc00 && low <= 0xdfff) {
        AppendUtf8(out, 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
        ++i;
        continue;
      }
    }
    if (unit >= 0xd800 && unit <= 0xdfff) {
      unit = 0xfffd;
    }
    AppendUtf8(out, unit);
  }
  return out;
}

std::string ToHex(std::uint64_t value) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  do {
    out.insert(out.begin(), kDigits[value & 0x0f]);
    value >>= 4;
  } while (value != 0);
  return "0x" + out;
}

bool FindChunk(const std::vector<std::uint8_t> &buffer, const char *tag, std::size_t pos,
               std::size_t end, std::size_t &dataStart, std::size_t &dataEnd) {
  while (pos + kChunkHeaderSize <= end) {
    const std::uint8_t *header = buffer.data() + pos;
    const std::uint64_t size = ReadU64LE(header + 4);
    const std::size_t bodyStart = pos + kChunkHeaderSize;
    if (size > end - bodyStart) {
      return false;
    }
    if (std::memcmp(header, tag, 4) == 0) {
      dataStart = bodyStart;
      dataEnd = bodyStart + static_cast<std::size_t>(size);
      return true;
    }
    pos = bodyStart + static_cast<std::size_t>(size);
  }
  return false;
}

} // namespace

class Xp3Index::Impl {
  public:
    std::string path;
    std::string lastError;

    std::uint64_t archiveSize = 0;
    std::uint64_t indexOffset = 0;
    std::uint64_t indexSize = 0;
    std::uint32_t duplicateCount = 0;

    std::vector<Xp3Entry> entries;
    std::unordered_map<std::string, std::size_t> byKey;
    // 兼容性索引：Windows 文件系统不区分大小写，而脚本中的名字大小写常与包内不一致。
    std::unordered_map<std::string, std::size_t> byLowerKey;

    void Fail(const std::string &message) { lastError = message; }

    bool ReadIndexPayload(std::ifstream &file, std::vector<std::uint8_t> &buffer);
    bool ParseEntries(const std::vector<std::uint8_t> &buffer);
};

bool Xp3Index::Impl::ReadIndexPayload(std::ifstream &file, std::vector<std::uint8_t> &buffer) {
  std::uint8_t header[kMagicSize + 8] = {};
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(header), static_cast<std::streamsize>(sizeof(header)));
  if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) {
    Fail("file is too short to be an XP3 archive");
    return false;
  }
  if (std::memcmp(header, kMagic, kMagicSize) != 0) {
    Fail("bad XP3 magic");
    return false;
  }

  indexOffset = ReadU64LE(header + kMagicSize);
  if (indexOffset == 0 || indexOffset >= archiveSize) {
    Fail("index offset " + ToHex(indexOffset) + " is out of range");
    return false;
  }
  if (indexOffset + 1 + 8 > archiveSize) {
    Fail("index header is past end of file");
    return false;
  }

  file.seekg(static_cast<std::streamoff>(indexOffset), std::ios::beg);
  std::uint8_t indexHeader[9] = {};
  file.read(reinterpret_cast<char *>(indexHeader), static_cast<std::streamsize>(sizeof(indexHeader)));
  if (file.gcount() != static_cast<std::streamsize>(sizeof(indexHeader))) {
    Fail("cannot read index header");
    return false;
  }

  const std::uint32_t flag = indexHeader[0];
  const std::uint32_t method = flag & kEncodeMethodMask;
  indexSize = ReadU64LE(indexHeader + 1);

  if (method != kEncodeRaw) {
    Fail("index is compressed (encode method " + std::to_string(method) +
         "); only raw indexes are supported so far");
    return false;
  }
  if (indexSize == 0) {
    Fail("index is empty");
    return false;
  }
  if (indexSize > kMaxIndexBytes) {
    Fail("index size " + std::to_string(indexSize) + " exceeds the 64 MiB sanity limit");
    return false;
  }
  if (indexOffset + 9 + indexSize > archiveSize) {
    Fail("index (" + std::to_string(indexSize) + " bytes) runs past end of file");
    return false;
  }

  buffer.resize(static_cast<std::size_t>(indexSize));
  file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(indexSize));
  if (static_cast<std::uint64_t>(file.gcount()) != indexSize) {
    Fail("short read while loading index");
    return false;
  }
  return true;
}

bool Xp3Index::Impl::ParseEntries(const std::vector<std::uint8_t> &buffer) {
  const std::size_t end = buffer.size();
  std::size_t pos = 0;
  std::size_t fileCount = 0;

  while (pos + kChunkHeaderSize <= end) {
    const std::uint8_t *header = buffer.data() + pos;
    const std::uint64_t chunkSize = ReadU64LE(header + 4);
    const std::size_t bodyStart = pos + kChunkHeaderSize;
    if (chunkSize > end - bodyStart) {
      Fail("top-level chunk at " + ToHex(pos) + " runs past the index buffer");
      return false;
    }
    const std::size_t bodyEnd = bodyStart + static_cast<std::size_t>(chunkSize);

    if (std::memcmp(header, "File", 4) != 0) {
      pos = bodyEnd;
      continue;
    }
    ++fileCount;

    std::size_t infoStart = 0;
    std::size_t infoEnd = 0;
    if (!FindChunk(buffer, "info", bodyStart, bodyEnd, infoStart, infoEnd)) {
      Fail("'File' chunk at " + ToHex(pos) + " has no 'info' sub-chunk");
      return false;
    }
    if (infoEnd - infoStart < kInfoHeaderSize) {
      Fail("'info' chunk at " + ToHex(infoStart) + " is shorter than its header");
      return false;
    }

    Xp3Entry entry;
    entry.flags = ReadU32LE(buffer.data() + infoStart);
    entry.orgSize = ReadU64LE(buffer.data() + infoStart + 4);
    entry.arcSize = ReadU64LE(buffer.data() + infoStart + 12);

    const std::int16_t nameLength =
        static_cast<std::int16_t>(static_cast<std::uint16_t>(buffer[infoStart + 20]) |
                                  (static_cast<std::uint16_t>(buffer[infoStart + 21]) << 8));
    if (nameLength < 0) {
      Fail("negative file name length at " + ToHex(infoStart + 20));
      return false;
    }
    const std::size_t nameUnits = static_cast<std::size_t>(nameLength);
    if (nameUnits * 2 > infoEnd - infoStart - kInfoHeaderSize) {
      Fail("file name runs past the 'info' chunk");
      return false;
    }
    entry.name = Utf16LeToUtf8(buffer.data() + infoStart + kInfoHeaderSize, nameUnits);
    entry.key = NormalizeStorageName(entry.name);

    std::size_t segmStart = 0;
    std::size_t segmEnd = 0;
    if (!FindChunk(buffer, "segm", bodyStart, bodyEnd, segmStart, segmEnd)) {
      Fail("'File' chunk for \"" + entry.name + "\" has no 'segm' sub-chunk");
      return false;
    }
    const std::size_t segmBytes = segmEnd - segmStart;
    if (segmBytes % kSegmentRecordSize != 0) {
      Fail("'segm' chunk for \"" + entry.name + "\" is not a multiple of 28 bytes");
      return false;
    }

    const std::size_t segmentCount = segmBytes / kSegmentRecordSize;
    std::uint64_t offsetInFile = 0;
    entry.segments.reserve(segmentCount);
    for (std::size_t i = 0; i < segmentCount; ++i) {
      const std::uint8_t *record = buffer.data() + segmStart + i * kSegmentRecordSize;
      Xp3Segment segment;
      segment.flags = ReadU32LE(record);
      const std::uint32_t method = segment.flags & kEncodeMethodMask;
      if (method == kEncodeRaw) {
        segment.compressed = false;
      } else if (method == kSegmEncodeZlib) {
        segment.compressed = true;
      } else {
        Fail("segment of \"" + entry.name + "\" uses unknown encode method " +
             std::to_string(method));
        return false;
      }
      segment.start = ReadU64LE(record + 4);
      segment.orgSize = ReadU64LE(record + 12);
      segment.arcSize = ReadU64LE(record + 20);
      segment.offsetInFile = offsetInFile;

      if (segment.start > indexOffset) {
        Fail("segment of \"" + entry.name + "\" starts past the index offset");
        return false;
      }
      if (offsetInFile > UINT64_MAX - segment.orgSize) {
        Fail("segment offsets of \"" + entry.name + "\" overflow");
        return false;
      }
      offsetInFile += segment.orgSize;
      entry.segments.push_back(segment);
    }

    std::string lowerKey = entry.key;
    for (char &ch : lowerKey) {
      const unsigned char byte = static_cast<unsigned char>(ch);
      if (byte < 0x80) {
        ch = static_cast<char>(std::tolower(byte));
      }
    }
    byLowerKey.emplace(lowerKey, entries.size());

    const auto inserted = byKey.emplace(entry.key, entries.size());
    if (!inserted.second) {
      ++duplicateCount;
    }
    entries.push_back(std::move(entry));
    pos = bodyEnd;
  }

  if (fileCount == 0) {
    Fail("index contains no 'File' chunk");
    return false;
  }
  return true;
}

Xp3Index::Xp3Index() : impl_(std::make_unique<Impl>()) {}

Xp3Index::~Xp3Index() = default;

bool Xp3Index::Load(const std::string &archivePathUtf8) {
  impl_->path = archivePathUtf8;
  impl_->lastError.clear();
  impl_->entries.clear();
  impl_->byKey.clear();
  impl_->byLowerKey.clear();
  impl_->duplicateCount = 0;

  const std::filesystem::path path = PathFromUtf8(archivePathUtf8);
  if (path.empty()) {
    impl_->Fail("archive path is not valid UTF-8");
    return false;
  }

  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    impl_->Fail("cannot stat \"" + archivePathUtf8 + "\": " + ec.message());
    return false;
  }
  impl_->archiveSize = static_cast<std::uint64_t>(size);

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    impl_->Fail("cannot open \"" + archivePathUtf8 + "\" for reading");
    return false;
  }

  std::vector<std::uint8_t> buffer;
  if (!impl_->ReadIndexPayload(file, buffer)) {
    return false;
  }
  return impl_->ParseEntries(buffer);
}

const std::vector<Xp3Entry> &Xp3Index::GetEntries() const { return impl_->entries; }

const Xp3Entry *Xp3Index::Find(const std::string &name) const {
  const std::string key = NormalizeStorageName(name);
  const auto exact = impl_->byKey.find(key);
  if (exact != impl_->byKey.end()) {
    return &impl_->entries[exact->second];
  }

  std::string lowerKey = key;
  for (char &ch : lowerKey) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (byte < 0x80) {
      ch = static_cast<char>(std::tolower(byte));
    }
  }
  const auto relaxed = impl_->byLowerKey.find(lowerKey);
  if (relaxed == impl_->byLowerKey.end()) {
    return nullptr;
  }
  krkr::log::Debug("Xp3Index: case-insensitive hit for \"", name, "\"");
  return &impl_->entries[relaxed->second];
}

std::uint64_t Xp3Index::GetArchiveSize() const { return impl_->archiveSize; }
std::uint64_t Xp3Index::GetIndexOffset() const { return impl_->indexOffset; }
std::uint64_t Xp3Index::GetIndexSize() const { return impl_->indexSize; }
std::uint32_t Xp3Index::GetDuplicateCount() const { return impl_->duplicateCount; }
const char *Xp3Index::GetLastErrorText() const { return impl_->lastError.c_str(); }

} // namespace krkr::io
