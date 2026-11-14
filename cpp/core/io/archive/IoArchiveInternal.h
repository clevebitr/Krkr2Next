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

tTVPArchive *OpenZIPArchive(const ttstr &name, tTJSBinaryStream *st,
                               bool normalizeFileName);

tTVPArchive *Open7ZArchive(const ttstr &name, tTJSBinaryStream *st,
                              bool normalizeFileName);

tTVPArchive *OpenTARArchive(const ttstr &name, tTJSBinaryStream *st,
                               bool normalizeFileName);

void FreeArchiveHandlePoolByPointer(void *pointer);

void ReleaseCachedArchiveHandle(void *pointer, tTJSBinaryStream *stream);

tTJSBinaryStream *GetCachedArchiveHandle(void *pointer, const ttstr &name);

void storeFilename(ttstr &name, const char *narrowName, const ttstr &filename);

} // namespace krkr::io

// TODO: 过渡兼容，IoStorageLocal.cpp 整体迁移完成后删除

#endif // KRKR_IO_ARCHIVE_INTERNAL_H
