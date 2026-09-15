//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// XP3 virtual file system support
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "XP3Archive.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "UtilStreams.h"
#include "SysInitIntf.h"

#include <zlib.h>
#include <algorithm>

#include "TVPMmapAlloc.h"

bool TVPAllowExtractProtectedStorage = true;

//---------------------------------------------------------------------------
// archive filter related
//---------------------------------------------------------------------------
tTVPXP3ArchiveExtractionFilter TVPXP3ArchiveExtractionFilter = nullptr;

void TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter filter) {
    TVPXP3ArchiveExtractionFilter = filter;
}
//---------------------------------------------------------------------------

static tTVPXP3ArchiveContentFilter TVPXP3ArchiveContentFilter = nullptr;

void TVPSetXP3ArchiveContentFilter(tTVPXP3ArchiveContentFilter filter) {
    TVPXP3ArchiveContentFilter = filter;
}

//---------------------------------------------------------------------------
// tTVPXP3ArchiveHandleCache
//---------------------------------------------------------------------------
#define TVP_MAX_ARCHIVE_HANDLE_CACHE 8
static tjs_uint TVPArchiveHandleCacheAge = 0;
struct tTVPArchiveHandleCacheItem {
    void *Pointer;
    tTJSBinaryStream *Stream;
    tjs_uint Age;
};
//---------------------------------------------------------------------------
static tTVPArchiveHandleCacheItem *TVPArchiveHandleCachePool = nullptr;
static bool TVPArchiveHandleCacheInit = false;
static bool TVPArchiveHandleCacheShutdown = false;
static tTJSCriticalSection TVPArchiveHandleCacheCS;

//---------------------------------------------------------------------------
tTJSBinaryStream *TVPGetCachedArchiveHandle(void *pointer, const ttstr &name) {
    // get cached archive file handle from the pool
    if(TVPArchiveHandleCacheShutdown) {
        // the pool has shutdown
        return TVPCreateStream(name);
    }

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    if(!TVPArchiveHandleCacheInit) {
        // initialize the pool
        TVPArchiveHandleCachePool =
            new tTVPArchiveHandleCacheItem[TVP_MAX_ARCHIVE_HANDLE_CACHE];
        for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++) {
            TVPArchiveHandleCachePool[i].Pointer = nullptr;
            TVPArchiveHandleCachePool[i].Stream = nullptr;
            TVPArchiveHandleCachePool[i].Age = 0;
        }
        TVPArchiveHandleCacheInit = true;
    }

    // linear search wiil be enough here because the
    // TVP_MAX_ARCHIVE_HANDLE_CACHE is relatively small
    for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++) {
        tTVPArchiveHandleCacheItem *item = TVPArchiveHandleCachePool + i;
        if(item->Stream && item->Pointer == pointer) {
            // found in the pool
            tTJSBinaryStream *stream = item->Stream;
            item->Stream = nullptr;
            return stream;
        }
    }

    // not found in the pool
    // simply create a stream and return it
    return TVPCreateStream(name);
}
//---------------------------------------------------------------------------
/*static*/ void TVPReleaseCachedArchiveHandle(void *pointer,
                                              tTJSBinaryStream *stream) {
    // release archive file handle
    if(TVPArchiveHandleCacheShutdown)
        return;
    if(!TVPArchiveHandleCacheInit)
        return;

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    // search empty cell in the pool
    tjs_uint oldest_age = 0;
    tjs_int oldest = 0;
    for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++) {
        tTVPArchiveHandleCacheItem *item = TVPArchiveHandleCachePool + i;
        if(item->Stream == nullptr) {
            // found the empty cell; fill it
            item->Pointer = pointer;
            item->Stream = stream;
            item->Age = ++TVPArchiveHandleCacheAge;
            // counter overflow in TVPArchiveHandleCacheAge
            // is not so a big problem.
            // counter overflow can worsen the cache performance,
            // but it occurs only when the counter is overflowed
            // (it's too far less than usual)
            return;
        }

        if(i == 0 || oldest_age > item->Age) {
            oldest_age = item->Age;
            oldest = i;
        }
    }

    // empty cell not found
    // free oldest cell and fill it
    tTVPArchiveHandleCacheItem *oldest_item =
        TVPArchiveHandleCachePool + oldest;
    delete oldest_item->Stream, oldest_item->Stream = nullptr;
    oldest_item->Pointer = pointer;
    oldest_item->Stream = stream;
    oldest_item->Age = ++TVPArchiveHandleCacheAge;
}
//---------------------------------------------------------------------------
/*static*/ void TVPFreeArchiveHandlePoolByPointer(void *pointer) {
    // free all streams which have specified pointer
    if(TVPArchiveHandleCacheShutdown)
        return;
    if(!TVPArchiveHandleCacheInit)
        return;

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++) {
        tTVPArchiveHandleCacheItem *item = TVPArchiveHandleCachePool + i;
        if(item->Stream && item->Pointer == pointer) {
            delete item->Stream, item->Stream = nullptr;
            item->Pointer = nullptr;
            item->Age = 0;
        }
    }
}

//---------------------------------------------------------------------------
static void TVPFreeArchiveHandlePool() {
    // free all streams
    if(TVPArchiveHandleCacheShutdown)
        return;
    if(!TVPArchiveHandleCacheInit)
        return;

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++) {
        tTVPArchiveHandleCacheItem *item = TVPArchiveHandleCachePool + i;
        if(item->Stream) {
            delete item->Stream, item->Stream = nullptr;
            item->Pointer = nullptr;
            item->Age = 0;
        }
    }
}

//---------------------------------------------------------------------------
static void TVPShutdownArchiveHandleCache() {
    // free all stream and shutdown the pool
    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    TVPArchiveHandleCacheShutdown = true;
    if(!TVPArchiveHandleCacheInit)
        return;

    for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++) {
        if(TVPArchiveHandleCachePool[i].Stream)
            delete TVPArchiveHandleCachePool[i].Stream;
    }
    delete[] TVPArchiveHandleCachePool;
}

//---------------------------------------------------------------------------
static tTVPAtExit TVPShutdownArchiveCacheAtExit(TVP_ATEXIT_PRI_CLEANUP,
                                                TVPShutdownArchiveHandleCache);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPXP3Archive
//---------------------------------------------------------------------------
/*
        TVP XPK3 virtual file system support. (in short : XP3)
        TVP supports no longer archive type of "XPK1/XPK2"
        ( XPK1/XPK2 is used by TVP ver under 0.9x ).

        here word "in-archive" is used for the storages which are
   contained in archive.
*/
//---------------------------------------------------------------------------
bool TVPGetXP3ArchiveOffset(tTJSBinaryStream *st, const ttstr name,
                            tjs_uint64 &offset, bool raise) {
    st->SetPosition(0);
    tjs_uint8 mark[11 + 1];
    static tjs_uint8 XP3Mark1[] = { 0x58 /*'X'*/,  0x50 /*'P'*/,
                                    0x33 /*'3'*/,  0x0d /*'\r'*/,
                                    0x0a /*'\n'*/, 0x20 /*' '*/,
                                    0x0a /*'\n'*/, 0x1a /*EOF*/,
                                    0xff /* sentinel */ };
    static tjs_uint8 XP3Mark2[] = { 0x8b, 0x67, 0x01, 0xff /* sentinel */ };

    // XP3 header mark contains:
    // 1. line feed and carriage return to detect corruption by
    // unnecessary
    //    line-feeds convertion
    // 2. 1A EOF mark which indicates file's text readable header
    // ending.
    // 3. 8B67 KANJI-CODE to detect curruption by unnecessary code
    // convertion
    // 4. 01 file structure version and character coding
    //    higher 4 bits are file structure version, currently 0.
    //    lower 4 bits are character coding, currently 1, is BMP 16bit
    //    Unicode.

    static tjs_uint8 XP3Mark[11 + 1];
    // +1: I was warned by CodeGuard that the code will do
    // access overrun... because a number of 11 is not aligned by
    // DWORD, and the processor may read the value of DWORD at last of
    // this array from offset 8. Then the last 1 byte would cause a
    // fail.
    static bool DoInit = true;
    if(DoInit) {
        // the XP3 header above is splitted into two part; to avoid
        // mis-finding of the header in the program's initialized data
        // area.
        DoInit = false;
        memcpy(XP3Mark, XP3Mark1, 8);
        memcpy(XP3Mark + 8, XP3Mark2, 3);
        // here joins it.
    }

    mark[0] = 0; // sentinel
    st->ReadBuffer(mark, 11);
    if(mark[0] == 0x4d /*'M'*/ && mark[1] == 0x5a /*'Z'*/) {
        // "MZ" is a mark of Win32/DOS executables,
        // TVP searches the first mark of XP3 archive
        // in the executeble file.
        bool found = false;

        offset = 16;
        st->SetPosition(16);

        // XP3 mark must be aligned by a paragraph ( 16 bytes )
        const tjs_uint one_read_size = 256 * 1024;
        tjs_uint read;
        tjs_uint8 *buffer =
            new tjs_uint8[one_read_size]; // read 256kbytes at once

        while(0 != (read = st->Read(buffer, one_read_size))) {
            tjs_uint p = 0;
            while(p < read) {
                if(!memcmp(XP3Mark, buffer + p, 11)) {
                    // found the mark
                    offset += p;
                    found = true;
                    break;
                }
                p += 16;
            }
            if(found)
                break;
            offset += one_read_size;
        }
        delete[] buffer;
        if(!found) {
            if(raise)
                TVPThrowExceptionMessage(TVPCannotUnbindXP3EXE, name);
            else
                return false;
        }
    } else if(!memcmp(XP3Mark, mark, 11)) {
        // XP3 mark found
        offset = 0;
    } else {
        if(raise) {
            ttstr msg(TVPGetMessageByLocale("err_not_xp3_archive"));
            TVPThrowExceptionMessage(msg.c_str(), name);
            // TVPThrowExceptionMessage(TVPCannotFindXP3Mark, name);
        }
        return false;
    }

    return true;
}

//---------------------------------------------------------------------------
bool TVPIsXP3Archive(const ttstr &name) {
    tTVPStreamHolder holder(name);
    try {
        tjs_uint64 offset;
        return TVPGetXP3ArchiveOffset(holder.Get(), name, offset, false);
    } catch(...) {
        return false;
    }
}

//---------------------------------------------------------------------------
void tTVPXP3Archive::Init(tTJSBinaryStream *st, tjs_int64 off,
                          bool normalizeName) {
    tjs_uint64 offset = off;

    tjs_uint8 *indexdata = nullptr;

    static const tjs_uint8 cn_File[] = { 0x46 /*'F'*/, 0x69 /*'i'*/,
                                         0x6c /*'l'*/, 0x65 /*'e'*/ };
    static const tjs_uint8 cn_info[] = { 0x69 /*'i'*/, 0x6e /*'n'*/,
                                         0x66 /*'f'*/, 0x6f /*'o'*/ };
    static const tjs_uint8 cn_segm[] = { 0x73 /*'s'*/, 0x65 /*'e'*/,
                                         0x67 /*'g'*/, 0x6d /*'m'*/ };
    static const tjs_uint8 cn_adlr[] = { 0x61 /*'a'*/, 0x64 /*'d'*/,
                                         0x6c /*'l'*/, 0x72 /*'r'*/ };

    TVPAddLog(TVPFormatMessage(
        TVPInfoTryingToReadXp3VirtualFileSystemInformationFrom, ArchiveName));

    int segmentcount = 0;
    try {
        // retrieve archive offset
        if(off < 0)
            TVPGetXP3ArchiveOffset(st, ArchiveName, offset, true);

        // read index position and seek
        st->SetPosition(11 + offset);

        // read all XP3 indices
        while(true) {
            if(indexdata)
                delete[] indexdata;

            tjs_uint64 index_ofs = st->ReadI64LE();
            st->SetPosition(index_ofs + offset);

            // read index to memory
            tjs_uint8 index_flag;
            st->ReadBuffer(&index_flag, 1);
            tjs_uint index_size;

            if((index_flag & TVP_XP3_INDEX_ENCODE_METHOD_MASK) ==
               TVP_XP3_INDEX_ENCODE_ZLIB) {
                // compressed index
                tjs_uint64 compressed_size = st->ReadI64LE();
                tjs_uint64 r_index_size = st->ReadI64LE();

                if((tjs_uint)compressed_size != compressed_size ||
                   (tjs_uint)r_index_size != r_index_size)
                    TVPThrowExceptionMessage(TVPReadError);
                // too large to handle, or corrupted
                index_size = (tjs_int)r_index_size;
                indexdata = new tjs_uint8[index_size];
                tjs_uint8 *compressed =
                    new tjs_uint8[(tjs_uint)compressed_size];
                try {
                    st->ReadBuffer(compressed, (tjs_uint)compressed_size);

                    unsigned long destlen = (unsigned long)index_size;

                    int result =
                        uncompress(/* uncompress from zlib */
                                   (unsigned char *)indexdata, &destlen,
                                   (unsigned char *)compressed,
                                   (unsigned long)compressed_size);
                    if(result != Z_OK || destlen != (unsigned long)index_size)
                        TVPThrowExceptionMessage(TVPUncompressionFailed);
                } catch(...) {
                    delete[] compressed;
                    throw;
                }
                delete[] compressed;
            } else if((index_flag & TVP_XP3_INDEX_ENCODE_METHOD_MASK) ==
                      TVP_XP3_INDEX_ENCODE_RAW) {
                // uncompressed index
                tjs_uint64 r_index_size = st->ReadI64LE();
                if((tjs_uint)r_index_size != r_index_size)
                    TVPThrowExceptionMessage(TVPReadError);
                // too large to handle or corrupted
                index_size = (tjs_uint)r_index_size;
                indexdata = new tjs_uint8[index_size];
                st->ReadBuffer(indexdata, index_size);
            } else {
                // unknown encode method
                TVPThrowExceptionMessage(TVPReadError);
            }

            // read index information from memory
            tjs_uint ch_file_start = 0;
            tjs_uint ch_file_size = index_size;
            // Count = 0;
            for(;;) {
                // find 'File' chunk
                if(!FindChunk(indexdata, cn_File, ch_file_start, ch_file_size))
                    break; // not found

                // find 'info' sub-chunk
                tjs_uint ch_info_start = ch_file_start;
                tjs_uint ch_info_size = ch_file_size;
                if(!FindChunk(indexdata, cn_info, ch_info_start, ch_info_size))
                    TVPThrowExceptionMessage(TVPReadError);

                // read info sub-chunk
                // 'info' 布局：flags(4) + orgSize(8) + arcSize(8) + nameLen(2)
                // + name(nameLen*2)。ch_info_size 现在已被 FindChunk 限制在索引
                // 缓冲区之内，但还要确认它装得下头部与名字本身——nameLen 直接
                // 来自文件（最大 32767），照着它去读名字会越界最多 64KB。
                if(ch_info_size < 22)
                    TVPThrowExceptionMessage(TVPReadError);

                tArchiveItem item;
                tjs_uint32 flags =
                    ReadI32FromMem(indexdata + ch_info_start + 0);
                if(!TVPAllowExtractProtectedStorage &&
                   (flags & TVP_XP3_FILE_PROTECTED))
                    TVPThrowExceptionMessage(
                        TVPSpecifiedStorageHadBeenProtected);
                item.OrgSize = ReadI64FromMem(indexdata + ch_info_start + 4);
                item.ArcSize = ReadI64FromMem(indexdata + ch_info_start + 12);

                tjs_int len = ReadI16FromMem(indexdata + ch_info_start + 20);
                // len 是有符号 16 位，负值同样非法
                if(len < 0 || ch_info_size < 22 + (tjs_uint)len * 2)
                    TVPThrowExceptionMessage(TVPReadError);
                ttstr name = TVPStringFromBMPUnicode(
                    (const tjs_uint16 *)(indexdata + ch_info_start + 22), len);
                item.Name = name;
                if(normalizeName)
                    NormalizeInArchiveStorageName(item.Name);

                // find 'segm' sub-chunk
                // Each of in-archive storages can be splitted into
                // some segments. Each segment can be compressed or
                // uncompressed independently. segments can share
                // partial area of archive storage. ( this is used for
                // OggVorbis' VQ code book sharing )
                tjs_uint ch_segm_start = ch_file_start;
                tjs_uint ch_segm_size = ch_file_size;
                if(!FindChunk(indexdata, cn_segm, ch_segm_start, ch_segm_size))
                    TVPThrowExceptionMessage(TVPReadError);

                // read segm sub-chunk
                tjs_int segment_count = ch_segm_size / 28;
                tjs_uint64 offset_in_archive = 0;
                for(tjs_int i = 0; i < segment_count; i++) {
                    tjs_uint pos_base = i * 28 + ch_segm_start;
                    tTVPXP3ArchiveSegment seg;
                    tjs_uint32 flags = ReadI32FromMem(indexdata + pos_base);

                    if((flags & TVP_XP3_SEGM_ENCODE_METHOD_MASK) ==
                       TVP_XP3_SEGM_ENCODE_RAW)
                        seg.IsCompressed = false;
                    else if((flags & TVP_XP3_SEGM_ENCODE_METHOD_MASK) ==
                            TVP_XP3_SEGM_ENCODE_ZLIB)
                        seg.IsCompressed = true;
                    else
                        TVPThrowExceptionMessage(
                            TVPReadError); // unknown encode method

                    seg.Start =
                        ReadI64FromMem(indexdata + pos_base + 4) + offset;
                    // data offset in archive
                    seg.Offset = offset_in_archive; // offset in in-archive
                                                    // storage
                    seg.OrgSize = ReadI64FromMem(indexdata + pos_base +
                                                 12); // original size
                    seg.ArcSize = ReadI64FromMem(indexdata + pos_base +
                                                 20); // archived size
                    // 段大小是累加进 offset_in_archive 的：一旦它回绕，Offset
                    // 表 就不再单调，SeekToPosition 的二分查找会落到 Offset >
                    // pos 的 段上，pos - Offset 在 uint64 下下溢。同时下游
                    // SetData 收的是 tjs_uint，超过 32
                    // 位会被截断，缓冲区比声明的小。两种都只
                    // 能由伪造的包触发，直接判损坏。
                    if(seg.OrgSize > 0xFFFFFFFFull ||
                       seg.ArcSize > 0xFFFFFFFFull)
                        TVPThrowExceptionMessage(TVPReadError);
                    item.Segments.push_back(seg);
                    offset_in_archive += seg.OrgSize;
                    segmentcount++;
                }

                // find 'aldr' sub-chunk
                tjs_uint ch_adlr_start = ch_file_start;
                tjs_uint ch_adlr_size = ch_file_size;
                if(!FindChunk(indexdata, cn_adlr, ch_adlr_start, ch_adlr_size))
                    TVPThrowExceptionMessage(TVPReadError);

                // read 'aldr' sub-chunk
                if(ch_adlr_size < 4)
                    TVPThrowExceptionMessage(TVPReadError);
                item.FileHash = ReadI32FromMem(indexdata + ch_adlr_start);

                // push information
                ItemVector.push_back(item);

                // to next file
                ch_file_start += ch_file_size;
                ch_file_size = index_size - ch_file_start;
                Count++;
            }

            if(!(index_flag & TVP_XP3_INDEX_CONTINUE))
                break; // continue reading index when the bit sets
        }

        // sort item vector by its name (required for tTVPArchive
        // specification)
        if(normalizeName)
            std::stable_sort(ItemVector.begin(), ItemVector.end());
    } catch(...) {
        if(indexdata)
            delete[] indexdata;
        delete st;
        TVPAddLog((const tjs_char *)TVPInfoFailed);
        throw;
    }
    if(indexdata)
        delete[] indexdata;
    delete st;

    TVPAddLog(TVPFormatMessage(TVPInfoDoneWithContains, ttstr(Count),
                               ttstr(segmentcount)));
}

//---------------------------------------------------------------------------
tTVPXP3Archive::tTVPXP3Archive(const ttstr &name, tTJSBinaryStream *st,
                               tjs_int64 offset, bool normalizeFileName) :
    tTVPArchive(name) {
    if(!st)
        st = TVPCreateStream(name);
    Init(st, offset, normalizeFileName);
}

//---------------------------------------------------------------------------
tTVPXP3Archive::~tTVPXP3Archive() { TVPFreeArchiveHandlePoolByPointer(this); }

tTVPArchive *tTVPXP3Archive::Create(const ttstr &name, tTJSBinaryStream *st,
                                    bool normalizeFileName) {
    bool refStream = st;
    if(!st) {
        st = TVPCreateStream(name);
    }
    tjs_uint64 offset;
    if(!TVPGetXP3ArchiveOffset(st, name, offset, false)) {
        if(!refStream)
            delete st;
        return nullptr;
    }
    return new tTVPXP3Archive(name, st, offset, normalizeFileName);
}

//---------------------------------------------------------------------------
tTJSBinaryStream *tTVPXP3Archive::CreateStreamByIndex(tjs_uint idx) {
    if(idx >= ItemVector.size())
        TVPThrowExceptionMessage(TVPReadError);

    tArchiveItem &item = ItemVector[idx];

    // 段表为空时不能构造 tTVPXP3ArchiveStream：它的构造函数会直接取
    // Segments[0]，对空 vector 就是越界读。'segm' 子块不足 28 字节（一个段都
    // 装不下）的条目会走到这里，等价的语义就是 0 字节存储。
    if(item.Segments.empty())
        return new tTVPMemoryStream();

    tTJSBinaryStream *stream = TVPGetCachedArchiveHandle(this, ArchiveName);

    tTVPXP3ArchiveStream *out;
    try {
        out = new tTVPXP3ArchiveStream(this, idx, &(item.Segments), stream,
                                       item.OrgSize);
        if(TVPXP3ArchiveContentFilter) {
            tjs_int result = TVPXP3ArchiveContentFilter(
                item.Name, ArchiveName, item.OrgSize, &out->GetFilterContext());
#define XP3_CONTENT_FILTER_FETCH_FULLDATA 1
            if(result == XP3_CONTENT_FILTER_FETCH_FULLDATA) {
                tTVPMemoryStream *memstr = new tTVPMemoryStream();
                memstr->SetSize(item.OrgSize);
                out->ReadBuffer(memstr->GetInternalBuffer(), item.OrgSize);
                delete out;
                return memstr;
            }
        }
    } catch(...) {
        TVPReleaseCachedArchiveHandle(this, stream);
        throw;
    }

    return out;
}

//---------------------------------------------------------------------------
bool tTVPXP3Archive::FindChunk(const tjs_uint8 *data, const tjs_uint8 *name,
                               tjs_uint &start, tjs_uint &size) {
    tjs_uint start_save = start;
    tjs_uint size_save = size;

    // 索引缓冲区来自游戏包，长度与 chunk 长度都不可信。搜索区间是
    // [start_save, start_save + size_save)，pos 是已消费的字节数，恒定有
    // start == start_save + pos。每一步都必须在读**之前**确认区间够用：
    //   - 头部 4 字节名字 + 8 字节长度必须在区间内，否则 memcmp/ReadI64FromMem
    //     直接读到堆外；
    //   - chunk 数据长度必须落在剩余区间内，否则 start += size_chunk 会把游标
    //     推出缓冲区，下一轮又是一次堆外读；旧代码里 pos 还会在这里回绕成小数，
    //     使 `pos < size` 永远成立而死循环。
    // 区间不足时按"没找到"收尾（返回 false 并还原 start/size），由调用方决定
    // 是报错还是停止解析——这比抛异常更能容忍尾部填充。
    tjs_uint pos = 0;
    while(pos + 4 + 8 <= size) {
        bool found = !memcmp(data + start, name, 4);
        start += 4;
        tjs_uint64 r_size = ReadI64FromMem(data + start);
        start += 8;
        tjs_uint size_chunk = (tjs_uint)r_size;
        if(size_chunk != r_size)
            TVPThrowExceptionMessage(TVPReadError);
        if(size_chunk > size - pos - 4 - 8)
            break; // 损坏的 chunk 长度：停止搜索
        if(found) {
            // found
            size = size_chunk;
            return true;
        }
        start += size_chunk;
        pos += size_chunk + 4 + 8;
    }

    start = start_save;
    size = size_save;
    return false;
}

//---------------------------------------------------------------------------
tjs_int16 tTVPXP3Archive::ReadI16FromMem(const tjs_uint8 *mem) {
    tjs_uint16 ret = (tjs_uint16)mem[0] | ((tjs_uint16)mem[1] << 8);
    return (tjs_int16)ret;
}

//---------------------------------------------------------------------------
tjs_int32 tTVPXP3Archive::ReadI32FromMem(const tjs_uint8 *mem) {
    tjs_uint32 ret = (tjs_uint32)mem[0] | ((tjs_uint32)mem[1] << 8) |
        ((tjs_uint32)mem[2] << 16) | ((tjs_uint32)mem[3] << 24);
    return (tjs_int32)ret;
}

//---------------------------------------------------------------------------
tjs_int64 tTVPXP3Archive::ReadI64FromMem(const tjs_uint8 *mem) {
    tjs_uint64 ret = (tjs_uint64)mem[0] | ((tjs_uint64)mem[1] << 8) |
        ((tjs_uint64)mem[2] << 16) | ((tjs_uint64)mem[3] << 24) |
        ((tjs_uint64)mem[4] << 32) | ((tjs_uint64)mem[5] << 40) |
        ((tjs_uint64)mem[6] << 48) | ((tjs_uint64)mem[7] << 56);
    return (tjs_int64)ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Compressed segment cache related
//---------------------------------------------------------------------------
#define TVP_SEGCACHE_ONE_LIMIT (1024 * 1024) // max size limit for each segment
#define TVP_SEGCACHE_TOTAL_LIMIT (1024 * 1024) // total segment cache size
tjs_uint TVPSegmentCacheLimit = TVP_SEGCACHE_TOTAL_LIMIT;

//---------------------------------------------------------------------------
struct tTVPSegmentCacheSearchData {
    ttstr Name; // archive name
    tjs_int StorageIndex; // storage index in archive
    tjs_int SegmentIndex; // segment index in storage

    bool operator==(const tTVPSegmentCacheSearchData &rhs) const {
        return Name == rhs.Name && StorageIndex == rhs.StorageIndex &&
            SegmentIndex == rhs.SegmentIndex;
    }
};

//---------------------------------------------------------------------------
class tTVPSegmentCacheSearchHashFunc {
public:
    static tjs_uint32 Make(const tTVPSegmentCacheSearchData &val) {
        tjs_uint32 v = tTJSHashFunc<ttstr>::Make(val.Name);

        v ^= val.StorageIndex;
        v ^= (val.SegmentIndex << 2);
        return v;
    }
};

//---------------------------------------------------------------------------
class tTVPSegmentData {
    tjs_int RefCount;
    tjs_uint Size;
    tjs_uint8 *Data;

public:
    tTVPSegmentData() {
        RefCount = 1;
        Size = 0;
        Data = nullptr;
    }

    ~tTVPSegmentData() {
        if(Data) {
#ifdef TVP_USE_MMAP_TEMP
            TVPMmapFree(Data);
#else
            delete[] Data;
#endif
        }
    }

    void SetData(unsigned long outsize, tTJSBinaryStream *instream,
                 unsigned long insize) {
#ifdef TVP_USE_MMAP_TEMP
        tjs_uint8 *indata = (tjs_uint8 *)TVPMmapAlloc(insize);
#else
        tjs_uint8 *indata = new tjs_uint8[insize];
#endif
        try {
            instream->Read(indata, insize);

#ifdef TVP_USE_MMAP_TEMP
            Data = (tjs_uint8 *)TVPMmapAlloc(outsize);
#else
            Data = new tjs_uint8[outsize];
#endif
            unsigned long destlen = outsize;
            int result = uncompress((unsigned char *)Data, &outsize,
                                    (unsigned char *)indata, insize);
            if(result != Z_OK || destlen != outsize)
                TVPThrowExceptionMessage(TVPUncompressionFailed);
            Size = outsize;
        } catch(...) {
#ifdef TVP_USE_MMAP_TEMP
            TVPMmapFree(indata);
#else
            delete[] indata;
#endif
            throw;
        }
#ifdef TVP_USE_MMAP_TEMP
        TVPMmapFree(indata);
#else
        delete[] indata;
#endif
    }

    const tjs_uint8 *GetData() const { return Data; }

    tjs_uint GetSize() const { return Size; }

    void AddRef() { RefCount++; }

    void Release() {
        if(RefCount == 1) {
            delete this;
        } else {
            RefCount--;
        }
    }
};

//---------------------------------------------------------------------------
typedef tTJSRefHolder<tTVPSegmentData> tTVPSegmentDataHolder;

typedef tTJSHashTable<tTVPSegmentCacheSearchData, tTVPSegmentDataHolder,
                      tTVPSegmentCacheSearchHashFunc>
    tTVPSegmentCache;
static tTVPSegmentCache TVPSegmentCache;
static tjs_uint TVPSegmentCacheTotalBytes = 0;

static tTJSCriticalSection TVPSegmentCacheCS;

//---------------------------------------------------------------------------
static void TVPCheckSegmentCacheLimit() {
    tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

    while(TVPSegmentCacheTotalBytes > TVPSegmentCacheLimit) {
        // chop last segment
        tTVPSegmentCache::tIterator i;
        i = TVPSegmentCache.GetLast();
        if(!i.IsNull()) {
            tjs_uint size = i.GetValue().GetObjectNoAddRef()->GetSize();
            TVPSegmentCacheTotalBytes -= size;
            TVPSegmentCache.ChopLast(1);
        } else {
            break;
        }
    }
}

//---------------------------------------------------------------------------
void TVPClearXP3SegmentCache() {
    tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

    TVPSegmentCache.Clear();
    TVPSegmentCacheTotalBytes = 0;
}

tjs_uint TVPGetXP3SegmentCacheTotalBytes() {
    tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);
    return TVPSegmentCacheTotalBytes;
}

//---------------------------------------------------------------------------
struct tTVPClearSegmentCacheCallback : public tTVPCompactEventCallbackIntf {
    void OnCompact(tjs_int level) override {
        if(level >= TVP_COMPACT_LEVEL_DEACTIVATE) {
            // clear the segment cache on application deactivate
            TVPClearXP3SegmentCache();
            // also free archive handle pool
            TVPFreeArchiveHandlePool();
        }
    }
} static TVPClearSegmentCacheCallback;

static bool TVPClearSegmentCacheCallbackInit = false;

//---------------------------------------------------------------------------
static tTVPSegmentData *
TVPSearchFromSegmentCache(const tTVPSegmentCacheSearchData &sdata,
                          tjs_uint32 hash) {
    tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

    tTVPSegmentDataHolder *ptr =
        TVPSegmentCache.FindAndTouchWithHash(sdata, hash);
    if(ptr) {
        // found in cache
        return ptr->GetObject(); // add-refed
    }

    return nullptr; // not found in cache
}

//---------------------------------------------------------------------------
static void TVPPushToSegmentCache(const tTVPSegmentCacheSearchData &sdata,
                                  tjs_uint32 hash, tTVPSegmentData *data) {
    if(!TVPClearSegmentCacheCallbackInit) {
        TVPAddCompactEventHook(&TVPClearSegmentCacheCallback);
        TVPClearSegmentCacheCallbackInit = true;
    }

    tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

    tTVPSegmentDataHolder holder(data);
    TVPSegmentCache.AddWithHash(sdata, hash, holder);
    TVPSegmentCacheTotalBytes += data->GetSize();

    TVPCheckSegmentCacheLimit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPXP3ArchiveStream : stream class for in-archive storage
//---------------------------------------------------------------------------
tTVPXP3ArchiveStream::tTVPXP3ArchiveStream(
    tTVPXP3Archive *owner, tjs_int storageindex,
    std::vector<tTVPXP3ArchiveSegment> *segments, tTJSBinaryStream *stream,
    tjs_uint64 orgsize) {
    StorageIndex = storageindex;
    Segments = segments;
    SegmentData = nullptr;
    CurSegmentNum = 0;
    CurSegment = &(Segments->operator[](0));
    SegmentPos = 0;
    SegmentRemain = CurSegment->OrgSize;
    SegmentOpened = false;
    CurPos = 0;

    LastOpenedSegmentNum = -1;

    Owner = owner;
    Owner->AddRef(); // hook
    Stream = stream;
    OrgSize = orgsize;
}

//---------------------------------------------------------------------------
tTVPXP3ArchiveStream::~tTVPXP3ArchiveStream() {
    TVPReleaseCachedArchiveHandle(Owner, Stream);
    Owner->Release(); // unhook
    if(SegmentData)
        SegmentData->Release();
}

//---------------------------------------------------------------------------
void tTVPXP3ArchiveStream::EnsureSegment() {
    // ensure accessing to current segment
    if(SegmentOpened)
        return;

    if(LastOpenedSegmentNum == CurSegmentNum) {
        if(!CurSegment->IsCompressed)
            Stream->SetPosition(CurSegment->Start + SegmentPos);
        return;
    }

    // erase buffer
    if(SegmentData)
        SegmentData->Release(), SegmentData = nullptr;

    // is compressed segment ?
    if(CurSegment->IsCompressed) {
        // a compressed segment

        if(CurSegment->OrgSize >= TVP_SEGCACHE_ONE_LIMIT) {
            // too large to cache
            Stream->SetPosition(CurSegment->Start);
            SegmentData = new tTVPSegmentData;
            SegmentData->SetData((tjs_uint)CurSegment->OrgSize, Stream,
                                 (tjs_uint)CurSegment->ArcSize);
        } else {
            // search thru segment cache
            tTVPSegmentCacheSearchData sdata;
            sdata.Name = Owner->GetName();
            sdata.StorageIndex = StorageIndex;
            sdata.SegmentIndex = CurSegmentNum;

            tjs_uint32 hash;
            hash = tTVPSegmentCacheSearchHashFunc::Make(sdata);

            SegmentData = TVPSearchFromSegmentCache(sdata, hash);
            if(!SegmentData) {
                // not found in cache
                Stream->SetPosition(CurSegment->Start);
                SegmentData = new tTVPSegmentData;
                SegmentData->SetData((tjs_uint)CurSegment->OrgSize, Stream,
                                     (tjs_uint)CurSegment->ArcSize);

                // add to cache
                TVPPushToSegmentCache(sdata, hash, SegmentData);
            }
        }
    } else {
        // not a compressed segment

        Stream->SetPosition(CurSegment->Start + SegmentPos);
    }

    SegmentOpened = true;
    LastOpenedSegmentNum = CurSegmentNum;
}

//---------------------------------------------------------------------------
void tTVPXP3ArchiveStream::SeekToPosition(tjs_uint64 pos) {
    // open segment at 'pos' and seek
    // pos must between zero thru OrgSize
    if(CurPos == pos)
        return;

    // do binary search to determine current segment number
    tjs_int st = 0;
    tjs_int et = (tjs_int)Segments->size();
    tjs_int seg_num;

    while(true) {
        if(et - st <= 1) {
            seg_num = st;
            break;
        }
        tjs_int m = st + (et - st) / 2;
        if(Segments->operator[](m).Offset > pos)
            et = m;
        else
            st = m;
    }

    CurSegmentNum = seg_num;
    CurSegment = &(Segments->operator[](CurSegmentNum));
    SegmentOpened = false;

    // SegmentPos/SegmentRemain 都要能容纳"越界"这一情形：两者都是 uint64，
    // 直接相减会下溢成天文数字，随后 Read 拿 SegmentRemain 当长度、拿
    // GetData() + SegmentPos 当地址去 memcpy，就是堆越界读。先把 SegmentPos
    // 收敛到 [0, OrgSize]，SegmentRemain 就恒在 [0, OrgSize] 内（为 0 时 Read
    // 会自然走到下一段）。
    // 索引解析已保证 Offset 单调（见段大小上限校验），正常情况下不会走到这里。
    if(pos <= CurSegment->Offset)
        SegmentPos = 0;
    else
        SegmentPos = pos - CurSegment->Offset;
    if(SegmentPos > CurSegment->OrgSize)
        SegmentPos = CurSegment->OrgSize;
    SegmentRemain = CurSegment->OrgSize - SegmentPos;
    CurPos = pos;
}

//---------------------------------------------------------------------------
bool tTVPXP3ArchiveStream::OpenNextSegment() {
    // open next segment
    if(CurSegmentNum == (tjs_int)(Segments->size() - 1))
        return false; // no more segments
    CurSegmentNum++;
    CurSegment = &(Segments->operator[](CurSegmentNum));
    SegmentOpened = false;
    SegmentPos = 0;
    SegmentRemain = CurSegment->OrgSize;
    CurPos = CurSegment->Offset;
    EnsureSegment();
    return true;
}

//---------------------------------------------------------------------------
tjs_uint64 tTVPXP3ArchiveStream::Seek(tjs_int64 offset, tjs_int whence) {
    tjs_int64 newpos;
    switch(whence) {
        case TJS_BS_SEEK_SET:
            newpos = offset;
            if(newpos >= 0 && newpos <= static_cast<tjs_int64>(OrgSize)) {
                SeekToPosition(newpos);
            }
            return CurPos;

        case TJS_BS_SEEK_CUR:
            newpos = offset + CurPos;
            if(newpos >= 0 && newpos <= static_cast<tjs_int64>(OrgSize)) {
                SeekToPosition(newpos);
            }
            return CurPos;

        case TJS_BS_SEEK_END:
            newpos = offset + OrgSize;
            if(newpos >= 0 && newpos <= static_cast<tjs_int64>(OrgSize)) {
                SeekToPosition(newpos);
            }
            return CurPos;
    }
    return CurPos;
}

//---------------------------------------------------------------------------
tjs_uint tTVPXP3ArchiveStream::Read(void *buffer, tjs_uint read_size) {
    EnsureSegment();

    tjs_uint write_size = 0;
    while(read_size) {
        while(SegmentRemain == 0) {
            // must go next segment
            if(!OpenNextSegment()) // open next segment
                return write_size; // could not read more
        }

        tjs_uint one_size =
            read_size > SegmentRemain ? (tjs_uint)SegmentRemain : read_size;

        if(CurSegment->IsCompressed) {
            // compressed segment; read from uncompressed data in
            // memory
            memcpy((tjs_uint8 *)buffer + write_size,
                   SegmentData->GetData() + (tjs_uint)SegmentPos, one_size);
        } else {
            // read directly from stream
            Stream->ReadBuffer((tjs_uint8 *)buffer + write_size, one_size);
        }

        // execute filter (for encryption method)
        if(TVPXP3ArchiveExtractionFilter) {
            tTVPXP3ExtractionFilterInfo info(
                CurPos, (tjs_uint8 *)buffer + write_size, one_size,
                Owner->GetFileHash(StorageIndex), Owner->GetName(StorageIndex));
            TVPXP3ArchiveExtractionFilter((tTVPXP3ExtractionFilterInfo *)&info,
                                          &FilterContext);
        }

        // adjust members
        SegmentPos += one_size;
        CurPos += one_size;
        SegmentRemain -= one_size;
        read_size -= one_size;
        write_size += one_size;
    }

    return write_size;
}

//---------------------------------------------------------------------------
tjs_uint tTVPXP3ArchiveStream::Write(const void *buffer, tjs_uint write_size) {
    return 0;
}

//---------------------------------------------------------------------------
tjs_uint64 tTVPXP3ArchiveStream::GetSize() { return OrgSize; }
//---------------------------------------------------------------------------
