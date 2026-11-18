#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/io/PathUtil.h"

// XP3 打包器（只用于造测试夹具）。
//
//   xp3pack <out.xp3> <file> [<file> ...]
//
// 条目名取输入文件的**文件名**，内容**原样写入（不压缩）**。
// 之所以只写 raw 段：夹具要的是"名字和内容我可控"，不是压缩率；
// 而且 raw 段让 6b 的解析器能当它的反向校验工具。
//
// 布局（与 core/io/archive/Xp3Index.cpp 的解析一一对应）：
//
//   [0..10]   magic (11 字节)
//   [11..18]  index_offset (u64 LE) = 19 + 所有数据长度之和
//   [19..]    依次拼接每个文件的数据
//   [index..] flag=0(u8) + index_size(u64 LE) + payload
//             payload = 每个文件一个 "File" 块：
//               "File" + u64 size {
//                 "info" + u64 size { flags=0(u32) org(u64) arc=org(u64)
//                                     nameLen(u16 字符数) name(UTF-16LE) }
//                 "segm" + u64 size=28 { flags=0(u32) start(u64) org(u64) arc(u64) }
//               }

namespace {

constexpr std::uint8_t kMagic[11] = {0x58, 0x50, 0x33, 0x0d, 0x0a, 0x20,
                                     0x0a, 0x1a, 0x8b, 0x67, 0x01};

using Bytes = std::vector<std::uint8_t>;

void PutU16(Bytes &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void PutU32(Bytes &out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));
  }
}

void PutU64(Bytes &out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));
  }
}

void PutTag(Bytes &out, const char *tag) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>(tag[i]));
  }
}

// UTF-8 -> UTF-16LE（XP3 的 info 块里名字就是 UTF-16LE）。
// 非法字节用 U+FFFD 替换，不抛异常。
bool Utf8ToUtf16Le(const std::string &utf8, Bytes &out) {
  std::size_t i = 0;
  std::size_t units = 0;
  while (i < utf8.size()) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    std::uint32_t cp = 0xfffd;
    std::size_t extra = 0;

    if (lead < 0x80) {
      cp = lead;
    } else if ((lead & 0xe0) == 0xc0) {
      cp = lead & 0x1f;
      extra = 1;
    } else if ((lead & 0xf0) == 0xe0) {
      cp = lead & 0x0f;
      extra = 2;
    } else if ((lead & 0xf8) == 0xf0) {
      cp = lead & 0x07;
      extra = 3;
    } else {
      ++i; // 无效首字节
      PutU16(out, 0xfffd);
      ++units;
      continue;
    }

    if (i + extra >= utf8.size()) {
      i = utf8.size();
      PutU16(out, 0xfffd);
      ++units;
      break;
    }
    bool ok = true;
    for (std::size_t k = 1; k <= extra; ++k) {
      const unsigned char cont = static_cast<unsigned char>(utf8[i + k]);
      if ((cont & 0xc0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (cont & 0x3f);
    }
    i += extra + 1;
    if (!ok) {
      PutU16(out, 0xfffd);
      ++units;
      continue;
    }

    if (cp <= 0xffff) {
      PutU16(out, static_cast<std::uint16_t>(cp));
      ++units;
    } else {
      const std::uint32_t v = cp - 0x10000;
      PutU16(out, static_cast<std::uint16_t>(0xd800 + (v >> 10)));
      PutU16(out, static_cast<std::uint16_t>(0xdc00 + (v & 0x3ff)));
      units += 2;
    }
  }
  return units <= 0x7fff; // nameLen 是有符号 16 位
}

struct InputFile {
  std::string name;  // UTF-8，取自文件名
  Bytes data;
};

bool ReadWholeFile(const std::filesystem::path &path, Bytes &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: xp3pack <out.xp3> <file> [<file> ...]\n");
    return 2;
  }

  std::vector<InputFile> inputs;
  for (int i = 2; i < argc; ++i) {
    const std::filesystem::path path = krkr::io::PathFromUtf8(argv[i]);
    if (path.empty()) {
      std::fprintf(stderr, "not valid UTF-8: %s\n", argv[i]);
      return 2;
    }
    InputFile input;
    input.name = krkr::io::PathToUtf8(path.filename());
    if (!ReadWholeFile(path, input.data)) {
      std::fprintf(stderr, "cannot read %s\n", argv[i]);
      return 1;
    }
    inputs.push_back(std::move(input));
  }

  // ---- 数据区：从偏移 19 开始依次拼接 ----
  Bytes dataArea;
  std::vector<std::uint64_t> starts;
  for (const auto &input : inputs) {
    starts.push_back(19 + dataArea.size());
    dataArea.insert(dataArea.end(), input.data.begin(), input.data.end());
  }

  // ---- 索引 payload ----
  Bytes payload;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const InputFile &input = inputs[i];

    Bytes info;
    PutU32(info, 0);                                  // flags
    PutU64(info, input.data.size());                   // org
    PutU64(info, input.data.size());                   // arc == org（未压缩）
    Bytes nameUtf16;
    if (!Utf8ToUtf16Le(input.name, nameUtf16)) {
      std::fprintf(stderr, "name too long: %s\n", input.name.c_str());
      return 1;
    }
    PutU16(info, static_cast<std::uint16_t>(nameUtf16.size() / 2)); // 字符数
    info.insert(info.end(), nameUtf16.begin(), nameUtf16.end());

    Bytes segm;
    PutU32(segm, 0);                        // flags=0 -> raw
    PutU64(segm, starts[i]);                // 数据在文件里的绝对偏移
    PutU64(segm, input.data.size());        // org
    PutU64(segm, input.data.size());        // arc

    Bytes fileChunk;
    PutTag(fileChunk, "info");
    PutU64(fileChunk, info.size());
    fileChunk.insert(fileChunk.end(), info.begin(), info.end());
    PutTag(fileChunk, "segm");
    PutU64(fileChunk, segm.size());
    fileChunk.insert(fileChunk.end(), segm.begin(), segm.end());

    PutTag(payload, "File");
    PutU64(payload, fileChunk.size());
    payload.insert(payload.end(), fileChunk.begin(), fileChunk.end());
  }

  // ---- 组装 ----
  Bytes out;
  out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
  const std::uint64_t indexOffset = 19 + dataArea.size();
  PutU64(out, indexOffset);
  out.insert(out.end(), dataArea.begin(), dataArea.end());
  out.push_back(0); // index flag: 0 = raw
  PutU64(out, payload.size());
  out.insert(out.end(), payload.begin(), payload.end());

  const std::filesystem::path outPath = krkr::io::PathFromUtf8(argv[1]);
  if (outPath.empty()) {
    std::fprintf(stderr, "not valid UTF-8: %s\n", argv[1]);
    return 2;
  }
  std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    std::fprintf(stderr, "cannot write %s\n", argv[1]);
    return 1;
  }
  file.write(reinterpret_cast<const char *>(out.data()),
             static_cast<std::streamsize>(out.size()));
  file.close();

  std::printf("wrote %s: %zu entry(ies), %zu bytes, index_offset=%llu\n", argv[1],
              inputs.size(), out.size(), static_cast<unsigned long long>(indexOffset));
  return 0;
}
