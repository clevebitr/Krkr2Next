#include "tjsCommHead.h"
#include "StorageIntf.h"
#include "UtilStreams.h"
#include <algorithm>

extern "C" {
#include <7zip/C/7z.h>
#include <7zip/C/7zFile.h>
#include <7zip/C/7zCrc.h>
}

#include "StorageImpl.h"

static ISzAlloc allocImp = { [](ISzAllocPtr p, size_t size) -> void * {
                                return malloc(size);
                            },
                             [](ISzAllocPtr p, void *addr) { free(addr); } };

class SevenZipStreamWrap {
public:
    CSzArEx db;
    tTJSBinaryStream *_stream;
    CLookToRead2 lookStream;
    struct CSeekInStream : public ISeekInStream {
        SevenZipStreamWrap *Host;
    } archiveStream;

public:
    SevenZipStreamWrap(tTJSBinaryStream *st) : _stream(st) {
        archiveStream.Host = this;
        archiveStream.Read = [](ISeekInStreamPtr p, void *buf,
                                size_t *size) -> SRes {
            return ((CSeekInStream *)p)->Host->StreamRead(buf, size);
        };
        archiveStream.Seek = [](ISeekInStreamPtr p, Int64 *pos,
                                ESzSeek origin) -> SRes {
            return ((CSeekInStream *)p)->Host->StreamSeek(pos, origin);
        };
        lookStream.buf = (Byte *)ISzAlloc_Alloc(&allocImp, 1 << 18);
        lookStream.bufSize = 1 << 18;
        LookToRead2_CreateVTable(&lookStream, false);
        LookToRead2_INIT(&lookStream);
        lookStream.realStream = &archiveStream;
        SzArEx_Init(&db);
        if(!g_CrcTable[1])
            CrcGenerateTable();
    }

    ~SevenZipStreamWrap() {
        SzArEx_Free(&db, &allocImp);
        if(lookStream.buf)
            ISzAlloc_Free(&allocImp, lookStream.buf);
        delete _stream;
    }

    SRes StreamRead(void *buf, size_t *size) {
        *size = _stream->Read(buf, *size);
        return SZ_OK;
    }

    SRes StreamSeek(Int64 *pos, ESzSeek origin) {
        tjs_int whence = TJS_BS_SEEK_SET;
        switch(origin) {
            case SZ_SEEK_CUR:
                whence = TJS_BS_SEEK_CUR;
                break;
            case SZ_SEEK_END:
                whence = TJS_BS_SEEK_END;
                break;
            case SZ_SEEK_SET:
                whence = TJS_BS_SEEK_SET;
                break;
            default:
                break;
        }

        *pos = _stream->Seek(*pos, whence);
        return SZ_OK;
    }
};

class SevenZipArchive : public tTVPArchive, public SevenZipStreamWrap {
    std::vector<std::pair<ttstr, tjs_uint>> filelist;

public:
    SevenZipArchive(const ttstr &name, tTJSBinaryStream *st) :
        tTVPArchive(name), SevenZipStreamWrap(st) {}

    ~SevenZipArchive() override {}

    tjs_uint GetCount() override { return filelist.size(); }

    ttstr GetName(tjs_uint idx) override { return filelist[idx].first; }

    tTJSBinaryStream *CreateStreamByIndex(tjs_uint idx) override {
        tjs_uint fileIndex = filelist[idx].second;
        UInt64 fileSize = SzArEx_GetFileSize(&db, fileIndex);
        if(fileSize <= 0)
            return new tTVPMemoryStream();

        UInt32 folderIndex = db.FileToFolder[fileIndex];
        if(folderIndex == (UInt32)-1)
            return nullptr;

        const CSzAr *p = &db.db;
        CSzFolder folder;
        CSzData sd;
        const Byte *data = p->CodersData + p->FoCodersOffsets[folderIndex];
        sd.Data = data;
        sd.Size = p->FoCodersOffsets[folderIndex + 1] -
            p->FoCodersOffsets[folderIndex];

        if(SzGetNextFolderItem(&folder, &sd) != SZ_OK)
            return nullptr;
        if(folder.NumCoders == 1) {
            UInt64 startPos = db.dataPos;
            const UInt64 *packPositions =
                p->PackPositions + p->FoStartPackStreamIndex[folderIndex];
            UInt64 offset = packPositions[0];
            UInt64 inSize = packPositions[1] - offset;
#define k_Copy 0
            if(folder.Coders[0].MethodID == k_Copy && inSize == fileSize) {
                return new TArchiveStream(this, startPos + offset, inSize);
            }
        }

        // outBuffer 由 SzArEx_Extract 以 allocMain 分配，**释放责任在调用方**：
        // 7z.h 把它定义成"可跨次复用的解压缓存"，CSzArEx 只是 const
        // 入参，不持有 它。原实现用 delete 释放 malloc
        // 出来的内存，分配器不匹配。 这里每个调用都用新的局部缓冲（接口要求
        // *outBuffer 每次从 0 开始）， 不开缓存：CreateStreamByIndex
        // 可能被多个加载线程并发调用。
        UInt32 blockIndex = 0;
        Byte *outBuffer = nullptr;
        size_t outBufferSize = 0;
        size_t offset = 0, outSizeProcessed = 0;
        SRes res = SzArEx_Extract(&db, &lookStream.vt, fileIndex, &blockIndex,
                                  &outBuffer, &outBufferSize, &offset,
                                  &outSizeProcessed, &allocImp, &allocImp);

        // 有效数据是 outBuffer + offset 起的 outSizeProcessed 字节，不是整个
        // outBuffer。原来的 `offset == 0 && fileSize <= outBufferSize` 分支之外
        // 一律从 outBuffer 起拷 fileSize 字节：offset 非 0 时读错位置，fileSize
        // 大于缓冲区时越界。这里把两个区间都验一遍，并且要求解出的长度与索引
        // 声明的一致——不一致说明包已损坏，宁可不返回流。
        const bool ok = res == SZ_OK && outBuffer != nullptr &&
            outSizeProcessed == fileSize && fileSize <= 0xFFFFFFFFull &&
            offset <= outBufferSize &&
            outSizeProcessed <= outBufferSize - offset;

        tTVPMemoryStream *mem = nullptr;
        if(ok) {
            // 一律拷贝，不把 outBuffer 直接交给 tTVPMemoryStream(const void*,
            // size)：那个构造函数是"引用"语义（Reference =
            // true，不接管所有权）， 而 outBuffer
            // 马上就要在这里释放，挂上去等于交出一个悬垂指针。 同理不能 new
            // Byte[] 再交给它——那会漏掉这块内存。走 SetSize + GetInternalBuffer
            // 让流自己持有，与 XP3Archive 里一致。
            mem = new tTVPMemoryStream();
            mem->SetSize((tjs_uint)fileSize);
            memcpy(mem->GetInternalBuffer(), outBuffer + offset,
                   (size_t)fileSize);
        }
        if(outBuffer)
            ISzAlloc_Free(&allocImp, outBuffer);
        return mem;
    }

    bool Open(bool normalizeFileName) {
        SRes res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocImp);
        if(res != SZ_OK) {
            _stream = nullptr;
            return false;
        }
        for(int i = 0; i < db.NumFiles; i++) {
            size_t offset = 0;
            size_t outSizeProcessed = 0;
            bool isDir = SzArEx_IsDir(&db, i);
            if(isDir)
                continue;
            size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
            ttstr filename;
            SzArEx_GetFileNameUtf16(&db, i,
                                    (UInt16 *)filename.AllocBuffer(len));
            filename.FixLen();
            if(normalizeFileName)
                NormalizeInArchiveStorageName(filename);
            filelist.emplace_back(filename, i);
        }
        if(normalizeFileName) {
            std::sort(filelist.begin(), filelist.end(),
                      [](const std::pair<ttstr, tjs_uint> &a,
                         const std::pair<ttstr, tjs_uint> &b) {
                          return a.first < b.first;
                      });
        }
        return true;
    }
};

tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st,
                              bool normalizeFileName) {
    tjs_uint64 pos = st->GetPosition();
    bool checkZIP = st->ReadI16LE() == 0x7A37; // '7z'
    st->SetPosition(pos);
    if(!checkZIP)
        return nullptr;
    SevenZipArchive *arc = new SevenZipArchive(name, st);
    if(!arc->Open(normalizeFileName)) {
        delete arc;
        return nullptr;
    }
    return arc;
}

#if 0
void TVPUnpack7ZArchive(tTJSBinaryStream *st, ttstr outpath) {
    tjs_uint64 origpos = st->GetPosition();
    SevenZipStreamWrap szsw(st);
    CSzArEx &db = szsw.db;
    SRes res = SzArEx_Open(&db, &szsw.lookStream.s, &allocImp, &allocImp);
    if (res != SZ_OK) return;
    outpath += TJS_W("/");
    for (int i = 0; i < db.db.NumFolders; ++i) {
        ;
    }
    for (int i = 0; i < db.NumFiles; i++) {
        size_t offset = 0;
        size_t outSizeProcessed = 0;
        size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
        ttstr filename;
        SzArEx_GetFileNameUtf16(&db, i, (UInt16*)filename.AllocBuffer(len));
        filename.FixLen();
        bool isDir = SzArEx_IsDir(&db, i);
        ttstr fullpath = outpath + filename;
        if (isDir) {
            if (!TVPCheckExistentLocalFolder(fullpath))
                TVPCreateFolders(fullpath);
        } else {
            tjs_uint fileIndex = i;
            UInt64 fileSize = SzArEx_GetFileSize(&db, fileIndex);
            if (fileSize <= 0) {
                FILE *fp = fopen(fullpath.AsStdString().c_str(), "wb");
                fclose(fp);
            }

            UInt32 folderIndex = db.FileToFolder[fileIndex];
            if (folderIndex == (UInt32)-1) continue;

            const CSzAr *p = &db.db;
            CSzFolder folder;
            CSzData sd;
            const Byte *data = p->CodersData + p->FoCodersOffsets[folderIndex];
            sd.Data = data;
            sd.Size = p->FoCodersOffsets[folderIndex + 1] - p->FoCodersOffsets[folderIndex];

            if (SzGetNextFolderItem(&folder, &sd) != SZ_OK) continue;
            if (folder.NumCoders == 1) {
                UInt64 startPos = db.dataPos;
                const UInt64 *packPositions = p->PackPositions + p->FoStartPackStreamIndex[folderIndex];
                UInt64 offset = packPositions[0];
                UInt64 inSize = packPositions[1] - offset;
                if (folder.Coders[0].MethodID == k_Copy && inSize == fileSize) {
                    CopyStreamToFile(st, origpos + startPos + offset, inSize, fullpath);
                    continue;
                }
            }

            UInt32 blockIndex;
            Byte *outBuffer = nullptr;
            size_t outBufferSize;
            size_t offset, outSizeProcessed;
            SRes res = SzArEx_Extract(&db, &szsw.lookStream.s, fileIndex, &blockIndex, &outBuffer, &outBufferSize,
                &offset, &outSizeProcessed, &allocImp, &allocImp);
            tTVPMemoryStream *mem;
            if (offset == 0 && fileSize <= outBufferSize) {
                mem = new tTVPMemoryStream(outBuffer, outBufferSize);
            } else {
                Byte *buf = new Byte[fileSize];
                memcpy(buf, outBuffer, fileSize);
                mem = new tTVPMemoryStream(buf, fileSize);
                delete outBuffer;
            }
            return mem;
        }
    }
}
#endif