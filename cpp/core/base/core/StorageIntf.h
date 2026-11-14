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

ttstr krkr::io::StringFromBMPUnicode(const tjs_uint16 *src, tjs_int maxlen = -1);
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
using krkr::io::StringFromBMPUnicode;
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

extern tTVPArchive *krkr::io::OpenArchive(const ttstr &name, bool normalizeFileName);
// open archive and return tTVPArchive instance.

TJS_EXP_FUNC_DEF(ttstr, krkr::io::GetTemporaryName, ());
// retrieve file name to store temporary data ( must be unique, local
// name )

TJS_EXP_FUNC_DEF(ttstr, krkr::io::GetAppPath, ());
// retrieve program path, in normalized storage name

void krkr::io::PreNormalizeStorageName(ttstr &name);
// called by krkr::io::NormalizeStorageName before it process the storage
// name. user may pass the OS's native filename to the TVP storage
// system, so that this function must convert it to the TVP storage
// name rules.

iTVPStorageMedia *krkr::io::CreateFileMedia();
// create basic default "file:" storage media
// /*
// extern void krkr::io::PreNormalizeStorageName(ttstr &name);
//
tTJSBinaryStream *krkr::io::OpenStream(const ttstr &name, tjs_uint32 flags);
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
// extern ttstr krkr::io::GetLocallyAccessiblePath(const ttstr &name);
//         // check wether given name is easily accessible from local OS
// filesystem.
//         // if true, returns local OS native name. otherwise returns an
// empty string.
//
// */
extern bool krkr::io::RemoveFile(const ttstr &name);

// remove local file ( "name" is a local *native* name )
// this must not throw an exception ( return false if error )
extern bool krkr::io::RemoveFolder(const ttstr &name);

// remove local directory ( "name" is a local *native* name )
// this must not throw an exception ( return false if error )
bool TVPCreateFolders(const ttstr &folder);
// create folder along with the argument recursively (like mkdir -p).
// 'folder' must be a local native name.

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::OpenArchive;
using krkr::io::GetTemporaryName;
using krkr::io::GetAppPath;
using krkr::io::PreNormalizeStorageName;
using krkr::io::CreateFileMedia;
using krkr::io::OpenStream;
using krkr::io::RemoveFile;
using krkr::io::RemoveFolder;
using krkr::io::TVPCreateFolders;

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// implementation in this unit
//---------------------------------------------------------------------------
namespace krkr::io {

TJS_EXP_FUNC_DEF(void, krkr::io::RegisterStorageMedia, (iTVPStorageMedia * media));
// register storage media
TJS_EXP_FUNC_DEF(void, krkr::io::UnregisterStorageMedia, (iTVPStorageMedia * media));
// remove storage media

extern tTJSBinaryStream *krkr::io::CreateStream(const ttstr &name,
                                         tjs_uint32 flags = 0);
// open "name" and return tTJSBinaryStream instance.
// name will be local storage, network storage, in-archive storage,
// etc...

TJS_EXP_FUNC_DEF(bool, krkr::io::IsExistentStorageNoSearch, (const ttstr &name));
// if "name" is exists, return true. otherwise return false.
// this does not search any auto search path.

TJS_EXP_FUNC_DEF(bool, krkr::io::IsExistentStorageNoSearchNoNormalize,
                 (const ttstr &name));

TJS_EXP_FUNC_DEF(ttstr, krkr::io::NormalizeStorageName, (const ttstr &name));

TJS_EXP_FUNC_DEF(void, krkr::io::SetCurrentStorageDirectory, (const ttstr &name));
// set system current directory.
// directory must end with path delimiter '/',
// or archive delimiter '>'.

TJS_EXP_FUNC_DEF(void, krkr::io::GetLocalPath, (ttstr & name));

ttstr krkr::io::GetLocallyAccessiblePath(const ttstr &name);

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::RegisterStorageMedia;
using krkr::io::UnregisterStorageMedia;
using krkr::io::CreateStream;
using krkr::io::IsExistentStorageNoSearch;
using krkr::io::IsExistentStorageNoSearchNoNormalize;
using krkr::io::NormalizeStorageName;
using krkr::io::SetCurrentStorageDirectory;
using krkr::io::GetLocalPath;
using krkr::io::GetLocallyAccessiblePath;

namespace krkr::io {

TJS_EXP_FUNC_DEF(ttstr, krkr::io::ExtractStorageExt, (const ttstr &name));
// extract "name"'s extension and return it.

TJS_EXP_FUNC_DEF(ttstr, krkr::io::ExtractStorageName, (const ttstr &name));
// extract "name"'s storage name ( excluding path ) and return it.

TJS_EXP_FUNC_DEF(ttstr, krkr::io::ExtractStoragePath, (const ttstr &name));
// extract "name"'s path ( including last delimiter ) and return it.

TJS_EXP_FUNC_DEF(ttstr, krkr::io::ChopStorageExt, (const ttstr &name));
// chop storage's extension and return it.
// extensition delimiter '.' will not be held.

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::ExtractStorageExt;
using krkr::io::ExtractStorageName;
using krkr::io::ExtractStoragePath;
using krkr::io::ChopStorageExt;

namespace krkr::io {

TJS_EXP_FUNC_DEF(void, krkr::io::AddAutoPath, (const ttstr &name));
// add given path to auto search path

TJS_EXP_FUNC_DEF(void, krkr::io::RemoveAutoPath, (const ttstr &name));
// remove given path from auto search path

extern void krkr::io::ClearAutoPathListForRestart();
// runtime-restart 时清空累积的 auto-path（上一游戏归档路径），供 reset 链调用

TJS_EXP_FUNC_DEF(ttstr, krkr::io::GetPlacedPath, (const ttstr &name));
// search path and return the path which the "name" is placed.

extern ttstr krkr::io::SearchPlacedPath(const ttstr &name);
// the same as krkr::io::GetPlacedPath, except for rising exception when the
// storage is not found.

TJS_EXP_FUNC_DEF(bool, krkr::io::IsExistentStorage, (const ttstr &name));
// if "name" is exists, return true. otherwise return false.
// this searches auto search path.

TJS_EXP_FUNC_DEF(void, krkr::io::ClearStorageCaches, ());

// clear all internal storage related caches.
void krkr::io::RemoveFromStorageCache(const ttstr &name);
void krkr::io::ClearArchiveCache();
void krkr::io::SetArchiveCacheCount(tjs_uint max_count);
tjs_uint krkr::io::GetArchiveCacheCount();
tjs_uint krkr::io::GetArchiveCacheLimit();

void krkr::io::SetAutoPathCacheMaxCount(tjs_uint max_count);
tjs_uint krkr::io::GetAutoPathCacheCount();
tjs_uint krkr::io::GetAutoPathCacheLimit();
tjs_uint krkr::io::GetAutoPathTableCount();

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::AddAutoPath;
using krkr::io::RemoveAutoPath;
using krkr::io::ClearAutoPathListForRestart;
using krkr::io::GetPlacedPath;
using krkr::io::SearchPlacedPath;
using krkr::io::IsExistentStorage;
using krkr::io::ClearStorageCaches;
using krkr::io::RemoveFromStorageCache;
using krkr::io::ClearArchiveCache;
using krkr::io::SetArchiveCacheCount;
using krkr::io::GetArchiveCacheCount;
using krkr::io::GetArchiveCacheLimit;
using krkr::io::SetAutoPathCacheMaxCount;
using krkr::io::GetAutoPathCacheCount;
using krkr::io::GetAutoPathCacheLimit;
using krkr::io::GetAutoPathTableCount;

namespace krkr::io {

extern tjs_uint TVPSegmentCacheLimit; // XP3 segment cache limit, in bytes.
tjs_uint krkr::io::GetXP3SegmentCacheTotalBytes();

// XP3 段缓存预算的显式覆盖（低内存路径用）。
//
// 为什么覆盖状态放在这里而不是 io/policy/IoPolicy.cpp 的匿名命名空间里：
// 低内存判定在 base/impl/SysInitImpl.cpp，而 `TVPSegmentCacheLimit` 是静态初始化期
// 定型的全局、层策略激活又更晚（engine_create），所以需要一份“显式覆盖”标记。
// 状态放在 base 拥有的这个头里，就变成 io 读 base（已存在的正向边），
// 而不是 base 反向 include io/policy/IoPolicy.h。
extern bool TVPSegmentCacheLimitOverridden;
extern unsigned long long TVPSegmentCacheLimitOverrideBytes;

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPSegmentCacheLimit;
using krkr::io::GetXP3SegmentCacheTotalBytes;
using krkr::io::TVPSegmentCacheLimitOverridden;
using krkr::io::TVPSegmentCacheLimitOverrideBytes;

namespace krkr::io {

void krkr::io::AutoMountSiblingXP3Archives();
void krkr::io::AutoMountProjectXP3Archives();
void krkr::io::BoostAutoMountPaths();

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::AutoMountSiblingXP3Archives;
using krkr::io::AutoMountProjectXP3Archives;
using krkr::io::BoostAutoMountPaths;

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
