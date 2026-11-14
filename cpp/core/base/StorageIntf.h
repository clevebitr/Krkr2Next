//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#ifndef StorageIntfH
#define StorageIntfH

#include "tjsNative.h"
#include "tjsHashSearch.h"
#include <vector>

//---------------------------------------------------------------------------
// archive delimiter
//---------------------------------------------------------------------------
namespace krkr::io
{
    extern tjs_char TVPArchiveDelimiter; //  = '>';
} // namespace krkr::io

using krkr::io::TVPArchiveDelimiter;

//---------------------------------------------------------------------------
// utilities
//---------------------------------------------------------------------------
namespace krkr::io {

ttstr TVPStringFromBMPUnicode(const tjs_uint16 *src, tjs_int maxlen = -1);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPArchive base archive class
//---------------------------------------------------------------------------
class tTVPArchive {
private:
    tjs_uint RefCount;

public:
    //-- constructor
    tTVPArchive(const ttstr &name) {
        ArchiveName = name;
        Init = false;
        RefCount = 1;
    }

    virtual ~tTVPArchive() {}

    //-- AddRef and Release
    void AddRef() { RefCount++; }

    void Release() {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
    }

    //-- must be implemented by delivered class
    virtual tjs_uint GetCount() = 0;

    virtual ttstr GetName(tjs_uint idx) = 0;
    // returned name must be already normalized using
    // NormalizeInArchiveStorageName and the index must be sorted by
    // its name, using ttstr::operator < . this is needed by fast
    // directory search.

    virtual tTJSBinaryStream *CreateStreamByIndex(tjs_uint idx) = 0;

    //-- others, implemented in this class
private:
    tTJSHashTable<ttstr, tjs_uint, tTJSHashFunc<ttstr>, 1024> Hash;
    bool Init;

public:
    ttstr ArchiveName;

public:
    static void NormalizeInArchiveStorageName(ttstr &name);

private:
    void AddToHash();

public:
    tTJSBinaryStream *CreateStream(const ttstr &name);

    bool IsExistent(const ttstr &name);

    tjs_int GetFirstIndexStartsWith(const ttstr &prefix);
    // returns first index which have 'prefix' at start of the name.
};

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPStringFromBMPUnicode;
using krkr::io::tTVPArchive;

//---------------------------------------------------------------------------

/*[*/
//---------------------------------------------------------------------------
// iTVPStorageMedia
//---------------------------------------------------------------------------
/*
        abstract class for managing media ( like file: http: etc.)
*/
/*]*/
#if 0
/*[*/
    // for plug-in
class tTJSBinaryStream;
/*]*/
#endif
/*[*/
namespace krkr::io {

//---------------------------------------------------------------------------
class iTVPStorageLister // callback class for GetListAt
{
public:
    virtual void Add(const ttstr &file) = 0;
};

//---------------------------------------------------------------------------
class iTVPStorageMedia {
public:
    virtual ~iTVPStorageMedia() {} // add by ZeaS
    virtual void AddRef() = 0;

    virtual void Release() = 0;

    virtual void GetName(ttstr &name) = 0;
    // returns media name like "file", "http" etc.

    //	virtual bool IsCaseSensitive() = 0;
    // returns whether this media is case sensitive or not

    virtual void NormalizeDomainName(ttstr &name) = 0;
    // normalize domain name according with the media's rule

    virtual void NormalizePathName(ttstr &name) = 0;
    // normalize path name according with the media's rule

    // "name" below is normalized but does not contain media, eg.
    // not "media://domain/path" but "domain/path"

    virtual bool CheckExistentStorage(const ttstr &name) = 0;
    // check file existence

    virtual tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) = 0;
    // open a storage and return a tTJSBinaryStream instance.
    // name does not contain in-archive storage name but
    // is normalized.

    virtual void GetListAt(const ttstr &name, iTVPStorageLister *lister) = 0;
    // list files at given place

    virtual void GetLocallyAccessibleName(ttstr &name) = 0;
    // basically the same as above,
    // check wether given name is easily accessible from local OS
    // filesystem. if true, returns local OS native name. otherwise
    // returns an empty string.
};

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::iTVPStorageLister;
using krkr::io::iTVPStorageMedia;

//---------------------------------------------------------------------------
/*]*/

//---------------------------------------------------------------------------
// must be implemented in each platform
//---------------------------------------------------------------------------
namespace krkr::io {

extern tTVPArchive *TVPOpenArchive(const ttstr &name, bool normalizeFileName);
// open archive and return tTVPArchive instance.

TJS_EXP_FUNC_DEF(ttstr, TVPGetTemporaryName, ());
// retrieve file name to store temporary data ( must be unique, local
// name )

TJS_EXP_FUNC_DEF(ttstr, TVPGetAppPath, ());
// retrieve program path, in normalized storage name

void TVPPreNormalizeStorageName(ttstr &name);
// called by TVPNormalizeStorageName before it process the storage
// name. user may pass the OS's native filename to the TVP storage
// system, so that this function must convert it to the TVP storage
// name rules.

iTVPStorageMedia *TVPCreateFileMedia();
// create basic default "file:" storage media
// /*
// extern void TVPPreNormalizeStorageName(ttstr &name);
//
tTJSBinaryStream *TVPOpenStream(const ttstr &name, tjs_uint32 flags);
//         // open a storage and return a tTJSBinaryStream instance.
//         // name does not contain in-archive storage name but
//         // is normalized.
//
// extern bool TVPCheckExistentStorage(const ttstr &name);
//         // check file existence
//
// extern void TVPGetStorageListAt(const ttstr &name, std::vector<ttstr>
// & list);
//
// extern ttstr TVPGetMediaCurrent(const ttstr & name);
// extern void TVPSetMediaCurrent(const ttstr & name, const ttstr & dir);
//
// extern ttstr TVPGetNativeName(const ttstr &name);
//         // retrieve OS native name
//
// extern ttstr TVPGetLocallyAccessibleName(const ttstr &name);
//         // check wether given name is easily accessible from local OS
// filesystem.
//         // if true, returns local OS native name. otherwise returns an
// empty string.
//
// */
extern bool TVPRemoveFile(const ttstr &name);

// remove local file ( "name" is a local *native* name )
// this must not throw an exception ( return false if error )
extern bool TVPRemoveFolder(const ttstr &name);

// remove local directory ( "name" is a local *native* name )
// this must not throw an exception ( return false if error )
bool TVPCreateFolders(const ttstr &folder);
// create folder along with the argument recursively (like mkdir -p).
// 'folder' must be a local native name.

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPOpenArchive;
using krkr::io::TVPGetTemporaryName;
using krkr::io::TVPGetAppPath;
using krkr::io::TVPPreNormalizeStorageName;
using krkr::io::TVPCreateFileMedia;
using krkr::io::TVPOpenStream;
using krkr::io::TVPRemoveFile;
using krkr::io::TVPRemoveFolder;
using krkr::io::TVPCreateFolders;

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// implementation in this unit
//---------------------------------------------------------------------------
namespace krkr::io {

TJS_EXP_FUNC_DEF(void, TVPRegisterStorageMedia, (iTVPStorageMedia * media));
// register storage media
TJS_EXP_FUNC_DEF(void, TVPUnregisterStorageMedia, (iTVPStorageMedia * media));
// remove storage media

extern tTJSBinaryStream *TVPCreateStream(const ttstr &name,
                                         tjs_uint32 flags = 0);
// open "name" and return tTJSBinaryStream instance.
// name will be local storage, network storage, in-archive storage,
// etc...

TJS_EXP_FUNC_DEF(bool, TVPIsExistentStorageNoSearch, (const ttstr &name));
// if "name" is exists, return true. otherwise return false.
// this does not search any auto search path.

TJS_EXP_FUNC_DEF(bool, TVPIsExistentStorageNoSearchNoNormalize,
                 (const ttstr &name));

TJS_EXP_FUNC_DEF(ttstr, TVPNormalizeStorageName, (const ttstr &name));

TJS_EXP_FUNC_DEF(void, TVPSetCurrentDirectory, (const ttstr &name));
// set system current directory.
// directory must end with path delimiter '/',
// or archive delimiter '>'.

TJS_EXP_FUNC_DEF(void, TVPGetLocalName, (ttstr & name));

ttstr TVPGetLocallyAccessibleName(const ttstr &name);

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPRegisterStorageMedia;
using krkr::io::TVPUnregisterStorageMedia;
using krkr::io::TVPCreateStream;
using krkr::io::TVPIsExistentStorageNoSearch;
using krkr::io::TVPIsExistentStorageNoSearchNoNormalize;
using krkr::io::TVPNormalizeStorageName;
using krkr::io::TVPSetCurrentDirectory;
using krkr::io::TVPGetLocalName;
using krkr::io::TVPGetLocallyAccessibleName;

namespace krkr::io {

TJS_EXP_FUNC_DEF(ttstr, TVPExtractStorageExt, (const ttstr &name));
// extract "name"'s extension and return it.

TJS_EXP_FUNC_DEF(ttstr, TVPExtractStorageName, (const ttstr &name));
// extract "name"'s storage name ( excluding path ) and return it.

TJS_EXP_FUNC_DEF(ttstr, TVPExtractStoragePath, (const ttstr &name));
// extract "name"'s path ( including last delimiter ) and return it.

TJS_EXP_FUNC_DEF(ttstr, TVPChopStorageExt, (const ttstr &name));
// chop storage's extension and return it.
// extensition delimiter '.' will not be held.

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPExtractStorageExt;
using krkr::io::TVPExtractStorageName;
using krkr::io::TVPExtractStoragePath;
using krkr::io::TVPChopStorageExt;

namespace krkr::io {

TJS_EXP_FUNC_DEF(void, TVPAddAutoPath, (const ttstr &name));
// add given path to auto search path

TJS_EXP_FUNC_DEF(void, TVPRemoveAutoPath, (const ttstr &name));
// remove given path from auto search path

extern void TVPClearAutoPathListForRestart();
// runtime-restart 时清空累积的 auto-path（上一游戏归档路径），供 reset 链调用

TJS_EXP_FUNC_DEF(ttstr, TVPGetPlacedPath, (const ttstr &name));
// search path and return the path which the "name" is placed.

extern ttstr TVPSearchPlacedPath(const ttstr &name);
// the same as TVPGetPlacedPath, except for rising exception when the
// storage is not found.

TJS_EXP_FUNC_DEF(bool, TVPIsExistentStorage, (const ttstr &name));
// if "name" is exists, return true. otherwise return false.
// this searches auto search path.

TJS_EXP_FUNC_DEF(void, TVPClearStorageCaches, ());

// clear all internal storage related caches.
void TVPRemoveFromStorageCache(const ttstr &name);
void TVPClearArchiveCache();
void TVPSetArchiveCacheCount(tjs_uint max_count);
tjs_uint TVPGetArchiveCacheCount();
tjs_uint TVPGetArchiveCacheLimit();

void TVPSetAutoPathCacheMaxCount(tjs_uint max_count);
tjs_uint TVPGetAutoPathCacheCount();
tjs_uint TVPGetAutoPathCacheLimit();
tjs_uint TVPGetAutoPathTableCount();

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPAddAutoPath;
using krkr::io::TVPRemoveAutoPath;
using krkr::io::TVPClearAutoPathListForRestart;
using krkr::io::TVPGetPlacedPath;
using krkr::io::TVPSearchPlacedPath;
using krkr::io::TVPIsExistentStorage;
using krkr::io::TVPClearStorageCaches;
using krkr::io::TVPRemoveFromStorageCache;
using krkr::io::TVPClearArchiveCache;
using krkr::io::TVPSetArchiveCacheCount;
using krkr::io::TVPGetArchiveCacheCount;
using krkr::io::TVPGetArchiveCacheLimit;
using krkr::io::TVPSetAutoPathCacheMaxCount;
using krkr::io::TVPGetAutoPathCacheCount;
using krkr::io::TVPGetAutoPathCacheLimit;
using krkr::io::TVPGetAutoPathTableCount;

namespace krkr::io {

extern tjs_uint TVPSegmentCacheLimit; // XP3 segment cache limit, in bytes.
tjs_uint TVPGetXP3SegmentCacheTotalBytes();

// XP3 段缓存预算的显式覆盖（低内存路径用）。
//
// 为什么覆盖状态放在这里而不是 io/IoPolicy.cpp 的匿名命名空间里：
// 低内存判定在 base/impl/SysInitImpl.cpp，而 `TVPSegmentCacheLimit` 是静态初始化期
// 定型的全局、层策略激活又更晚（engine_create），所以需要一份“显式覆盖”标记。
// 状态放在 base 拥有的这个头里，就变成 io 读 base（已存在的正向边），
// 而不是 base 反向 include io/IoPolicy.h。
extern bool TVPSegmentCacheLimitOverridden;
extern unsigned long long TVPSegmentCacheLimitOverrideBytes;

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPSegmentCacheLimit;
using krkr::io::TVPGetXP3SegmentCacheTotalBytes;
using krkr::io::TVPSegmentCacheLimitOverridden;
using krkr::io::TVPSegmentCacheLimitOverrideBytes;

namespace krkr::io {

void TVPAutoMountSiblingXP3Archives();
void TVPAutoMountProjectXP3Archives();
void TVPBoostAutoMountPaths();

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPAutoMountSiblingXP3Archives;
using krkr::io::TVPAutoMountProjectXP3Archives;
using krkr::io::TVPBoostAutoMountPaths;

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Storages : TJS Storages class
//---------------------------------------------------------------------------
namespace krkr::io {

class tTJSNC_Storages : public tTJSNativeClass {
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_Storages();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::tTJSNC_Storages;

//---------------------------------------------------------------------------
namespace krkr::io {

extern tTJSNativeClass *TVPCreateNativeClass_Storages();

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPCreateNativeClass_Storages;
//---------------------------------------------------------------------------

namespace krkr::io {

class tTVPStorageMedia : public iTVPStorageMedia {
protected:
    tjs_int refCount;

    tTVPStorageMedia() : refCount(1) {}

    void AddRef() override { refCount++; }

    void Release() override {
        if(refCount == 1) {
            delete this;
        } else {
            refCount--;
        }
    }

    void NormalizeDomainName(ttstr &name) override {}

    void NormalizePathName(ttstr &name) override {}

    void GetLocallyAccessibleName(ttstr &name) override {}
};

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::tTVPStorageMedia;

namespace krkr::io {

class TArchiveStream : public tTJSBinaryStream {
    tTVPArchive *Owner;
    tjs_int64 CurrentPos;
    tjs_uint64 StartPos, DataLength;
    tTJSBinaryStream *_instr;

public:
    TArchiveStream(tTVPArchive *owner, tjs_uint64 off, tjs_uint64 len);

    ~TArchiveStream() override;

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override {
        switch(whence) {
            case TJS_BS_SEEK_SET:
                CurrentPos = offset;
                break;

            case TJS_BS_SEEK_CUR:
                CurrentPos = offset + CurrentPos;
                break;

            case TJS_BS_SEEK_END:
                CurrentPos = offset + DataLength;
                break;
        }
        if(CurrentPos < 0)
            CurrentPos = 0;
        else if(CurrentPos > (tjs_int64)DataLength)
            CurrentPos = DataLength;
        _instr->SetPosition(CurrentPos + StartPos);
        return CurrentPos;
    }

    tjs_uint Read(void *buffer, tjs_uint read_size) override {
        if(CurrentPos + read_size >= (tjs_int64)DataLength) {
            read_size = (tjs_uint)(DataLength - CurrentPos);
        }

        _instr->ReadBuffer(buffer, read_size);

        CurrentPos += read_size;

        return read_size;
    }

    tjs_uint Write(const void *buffer, tjs_uint write_size) override {
        return 0;
    }

    tjs_uint64 GetSize() override { return DataLength; }
};

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TArchiveStream;

#endif
