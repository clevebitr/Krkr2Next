#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "core/io/Storage.h"

// 独立的 io 测试台：只测 krkr::io，不碰引擎。
// 用法：storage_probe <game_root_dir>
// 退出码：0 = 全过，1 = 有 FAIL。

namespace {

int g_failures = 0;

void Check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}

std::string ReadAll(krkr::io::IStream &stream) {
  std::string out;
  char buffer[128];
  std::int64_t got = 0;
  while ((got = stream.Read(buffer, static_cast<std::int64_t>(sizeof(buffer)))) > 0) {
    out.append(buffer, static_cast<std::size_t>(got));
  }
  return out;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: storage_probe <game_root_dir>\n");
    return 2;
  }

  krkr::io::Storage storage(argv[1]);
  std::printf("root = \"%s\" mounted=%d\n", storage.GetRootUtf8().c_str(),
              storage.Mounted() ? 1 : 0);
  Check(storage.Mounted(), "mount demo dir");
  if (!storage.Mounted()) {
    std::fprintf(stderr, "mount failed: %s\n", storage.GetLastErrorText());
    return 1;
  }

  // --- 1) 打开 + 尺寸 + 全量读取 -------------------------------------------
  std::unique_ptr<krkr::io::IStream> stream = storage.Open("startup.tjs");
  Check(stream != nullptr, "Open(\"startup.tjs\")");
  if (stream == nullptr) {
    std::fprintf(stderr, "   error: %s\n", storage.GetLastErrorText());
    return 1;
  }

  const std::int64_t size = stream->GetSize();
  const std::string content = ReadAll(*stream);
  std::printf("     size=%lld read=%zu\n", static_cast<long long>(size), content.size());
  Check(size == static_cast<std::int64_t>(content.size()), "GetSize() == bytes actually read");
  Check(!content.empty(), "content is not empty");

  // 读完再读一次应该是 EOF（返回 0，不是 -1）
  char probe[8];
  Check(stream->Read(probe, 1) == 0, "Read at EOF returns 0");

  // --- 2) Seek(0) 之后内容必须一致 ------------------------------------------
  Check(stream->Seek(0), "Seek(0)");
  Check(stream->GetPosition() == 0, "GetPosition() == 0 after Seek(0)");
  const std::string again = ReadAll(*stream);
  Check(again == content, "re-read after Seek(0) is byte-identical");

  // --- 3) 读超尾部不越界（哨兵字节）----------------------------------------
  if (size >= 3) {
    char buffer[8];
    std::memset(buffer, 0xAA, sizeof(buffer));
    Check(stream->Seek(size - 3), "Seek(size - 3)");
    const std::int64_t got = stream->Read(buffer, static_cast<std::int64_t>(sizeof(buffer)));
    bool sentinel_ok = true;
    for (std::size_t i = 3; i < sizeof(buffer); ++i) {
      if (static_cast<unsigned char>(buffer[i]) != 0xAA) {
        sentinel_ok = false;
      }
    }
    Check(got == 3, "Read past EOF returns only the remaining 3 bytes");
    Check(sentinel_ok, "buffer beyond returned bytes is untouched");
  }
  Check(!stream->Seek(size + 1), "Seek(size + 1) is rejected");
  Check(!stream->Seek(-1), "Seek(-1) is rejected");
  stream.reset();

  // --- 4) 反斜杠分隔符归一化 -----------------------------------------------
  std::unique_ptr<krkr::io::IStream> sub = storage.Open("sub\\a.tjs");
  Check(sub != nullptr, "Open(\"sub\\\\a.tjs\") normalizes separators");
  if (sub == nullptr) {
    std::fprintf(stderr, "   error: %s\n", storage.GetLastErrorText());
  }

  // --- 5) 不存在的名字 ------------------------------------------------------
  std::unique_ptr<krkr::io::IStream> missing = storage.Open("nope.tjs");
  Check(missing == nullptr, "Open(\"nope.tjs\") returns nullptr");
  Check(std::string(storage.GetLastErrorText()).find("nope.tjs") != std::string::npos,
        "error text mentions the name");
  std::printf("     error: %s\n", storage.GetLastErrorText());

  // --- 6) 目录当文件开 ------------------------------------------------------
  std::unique_ptr<krkr::io::IStream> dir = storage.Open("sub");
  Check(dir == nullptr, "Open(\"sub\") (a directory) returns nullptr");
  std::printf("     error: %s\n", storage.GetLastErrorText());

  // --- 7) '..' 放行（本步拍板的决定：完全跟随系统路径语义）-----------------
  std::unique_ptr<krkr::io::IStream> escaped = storage.Open("../escape.txt");
  Check(escaped != nullptr, "Open(\"../escape.txt\") is allowed (documented decision)");
  if (escaped != nullptr) {
    std::printf("     escaped content = \"%s\"\n", ReadAll(*escaped).c_str());
  }

  std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
  return g_failures == 0 ? 0 : 1;
}
