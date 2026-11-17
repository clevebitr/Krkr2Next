#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "core/io/archive/Xp3Index.h"

// XP3 索引测试台（6b）。不读数据、不解压，只验证「文件名 -> 偏移/大小」解析对不对。
//
//   xp3_probe <archive>                 摘要 + 前 5 条
//   xp3_probe <archive> --list          全部条目（name<TAB>org<TAB>arc<TAB>segs），用来和参照实现 diff
//   xp3_probe <archive> --find <name>   查一个名字
//   xp3_probe --path-file <file> [...]  从文件第一行读包路径（UTF-8）
//
// 为什么需要 --path-file：Windows 上 main 的 argv 会被 CRT 转成 ANSI 代码页
// （中文系统上是 GBK），所以带日文/中文的路径根本传不进来。测试台从文件读
// UTF-8 字节就能绕开；Android 走 JNI，不受影响。
//
// 退出码：0 = 成功，1 = 解析/查找失败，2 = 用法错误。

namespace {

int PrintSummary(const krkr::io::Xp3Index &index) {
  const auto &entries = index.GetEntries();
  std::printf("archive_size = %llu\n", static_cast<unsigned long long>(index.GetArchiveSize()));
  std::printf("index_offset = %llu\n", static_cast<unsigned long long>(index.GetIndexOffset()));
  std::printf("index_size   = %llu\n", static_cast<unsigned long long>(index.GetIndexSize()));
  std::printf("entries      = %zu\n", entries.size());
  std::printf("duplicates   = %u\n", index.GetDuplicateCount());

  std::size_t compressed = 0;
  std::size_t multiSegment = 0;
  for (const auto &entry : entries) {
    if (!entry.segments.empty() && entry.segments.front().compressed) {
      ++compressed;
    }
    if (entry.segments.size() > 1) {
      ++multiSegment;
    }
  }
  std::printf("compressed   = %zu (first segment is zlib)\n", compressed);
  std::printf("multi-segmt  = %zu\n", multiSegment);

  std::printf("\nfirst 5:\n");
  const std::size_t shown = entries.size() < 5 ? entries.size() : 5;
  for (std::size_t i = 0; i < shown; ++i) {
    const auto &entry = entries[i];
    std::printf("  %-40s org=%-8llu arc=%-8llu segs=%zu\n", entry.name.c_str(),
                static_cast<unsigned long long>(entry.orgSize),
                static_cast<unsigned long long>(entry.arcSize), entry.segments.size());
  }
  return 0;
}

int PrintList(const krkr::io::Xp3Index &index) {
  for (const auto &entry : index.GetEntries()) {
    std::printf("%s\t%llu\t%llu\t%zu\n", entry.name.c_str(),
                static_cast<unsigned long long>(entry.orgSize),
                static_cast<unsigned long long>(entry.arcSize), entry.segments.size());
  }
  return 0;
}

int PrintFind(const krkr::io::Xp3Index &index, const char *name) {
  const krkr::io::Xp3Entry *entry = index.Find(name);
  if (entry == nullptr) {
    std::printf("NOT FOUND  %s\n", name);
    return 1;
  }
  std::printf("FOUND      %s\n", entry->name.c_str());
  std::printf("  key         = %s\n", entry->key.c_str());
  std::printf("  flags       = %u\n", entry->flags);
  std::printf("  org_size    = %llu\n", static_cast<unsigned long long>(entry->orgSize));
  std::printf("  arc_size    = %llu\n", static_cast<unsigned long long>(entry->arcSize));
  std::printf("  segments    = %zu\n", entry->segments.size());
  for (const auto &segment : entry->segments) {
    std::printf("    start=%-12llu in_file=%-12llu org=%-10llu arc=%-10llu compressed=%d\n",
                static_cast<unsigned long long>(segment.start),
                static_cast<unsigned long long>(segment.offsetInFile),
                static_cast<unsigned long long>(segment.orgSize),
                static_cast<unsigned long long>(segment.arcSize),
                segment.compressed ? 1 : 0);
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: xp3_probe <archive.xp3> [--list | --find <name>]\n"
                 "       xp3_probe --path-file <file> [--list | --find <name>]\n");
    return 2;
  }

  std::string archivePath = argv[1];
  int optionIndex = 2;
  if (archivePath == "--path-file") {
    if (argc < 3) {
      std::fprintf(stderr, "--path-file needs a file argument\n");
      return 2;
    }
    std::ifstream listFile(argv[2], std::ios::binary);
    if (!listFile.is_open()) {
      std::fprintf(stderr, "cannot open path file: %s\n", argv[2]);
      return 2;
    }
    std::getline(listFile, archivePath);
    while (!archivePath.empty() &&
           (archivePath.back() == '\r' || archivePath.back() == '\n')) {
      archivePath.pop_back();
    }
    optionIndex = 3;
  }

  krkr::io::Xp3Index index;
  if (!index.Load(archivePath)) {
    std::fprintf(stderr, "load failed: %s\n", index.GetLastErrorText());
    return 1;
  }

  const char *option = optionIndex < argc ? argv[optionIndex] : nullptr;
  if (option != nullptr && std::strcmp(option, "--list") == 0) {
    return PrintList(index);
  }
  if (option != nullptr && std::strcmp(option, "--find") == 0 &&
      optionIndex + 1 < argc) {
    return PrintFind(index, argv[optionIndex + 1]);
  }
  if (option != nullptr) {
    std::fprintf(stderr, "unknown option: %s\n", option);
    return 2;
  }
  return PrintSummary(index);
}
