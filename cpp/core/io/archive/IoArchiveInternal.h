#ifndef KRKR_IO_ARCHIVE_INTERNAL_H
#define KRKR_IO_ARCHIVE_INTERNAL_H

#include "StorageIntf.h"

// io 内部归档后端之间共享的声明。
//
// 这些符号不是对外 API：它们只在 IoStorageLocal.cpp / XP3Archive.cpp /
// ZIPArchive.cpp / 7zArchive.cpp / TARArchive.cpp 之间使用。集中在这里是为了
// 避免每个 .cpp 各写一份 forward declaration，并保证 namespace 迁移时声明和
// 定义始终在同一个 namespace。
namespace krkr::io {

tTVPArchive *TVPOpenZIPArchive(const ttstr &name, tTJSBinaryStream *st,
                               bool normalizeFileName);

tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st,
                              bool normalizeFileName);

tTVPArchive *TVPOpenTARArchive(const ttstr &name, tTJSBinaryStream *st,
                               bool normalizeFileName);

void TVPFreeArchiveHandlePoolByPointer(void *pointer);

void TVPReleaseCachedArchiveHandle(void *pointer, tTJSBinaryStream *stream);

tTJSBinaryStream *TVPGetCachedArchiveHandle(void *pointer, const ttstr &name);

void storeFilename(ttstr &name, const char *narrowName, const ttstr &filename);

} // namespace krkr::io

// TODO: 过渡兼容，IoStorageLocal.cpp 整体迁移完成后删除
using krkr::io::TVPOpenZIPArchive;
using krkr::io::TVPOpen7ZArchive;
using krkr::io::TVPOpenTARArchive;
using krkr::io::TVPFreeArchiveHandlePoolByPointer;
using krkr::io::TVPReleaseCachedArchiveHandle;
using krkr::io::TVPGetCachedArchiveHandle;
using krkr::io::storeFilename;

#endif // KRKR_IO_ARCHIVE_INTERNAL_H
