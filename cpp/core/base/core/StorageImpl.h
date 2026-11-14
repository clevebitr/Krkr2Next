//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#ifndef StorageImplH
#define StorageImplH
//---------------------------------------------------------------------------
#include "StorageIntf.h"
#include "UtilStreams.h"
#include <functional>

#ifndef S_IFMT
#define S_IFDIR 0x4000 // Directory
#define S_IFREG 0x8000 // Regular
#endif

// 全局 COM 类型，不能跟着 io 一起进 namespace。
struct IStream;

namespace krkr::io {

struct tTVPLocalFileInfo {
    const char *NativeName;
    unsigned short Mode; // S_IFMT
    tjs_uint64 Size;
    time_t AccessTime;
    time_t ModifyTime;
    time_t CreationTime;
};

void krkr::io::GetLocalFileListAt(
    const ttstr &name,
    const std::function<void(const ttstr &, tTVPLocalFileInfo *)> &cb);

//---------------------------------------------------------------------------
// krkr::io::LocalFileStream
//---------------------------------------------------------------------------
class krkr::io::LocalFileStream : public tTJSBinaryStream {
    int Handle;
    krkr::io::MemoryStream *MemBuffer = nullptr;
    ttstr FileName;

public:
    krkr::io::LocalFileStream(const ttstr &origname, const ttstr &localname,
                        tjs_uint32 flag);

    ~krkr::io::LocalFileStream() override;

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override;

    tjs_uint Read(void *buffer, tjs_uint read_size) override;

    tjs_uint Write(const void *buffer, tjs_uint write_size) override;

    void SetEndOfStorage() override;

    tjs_uint64 GetSize() override;

    [[nodiscard]] int GetHandle() const { return Handle; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(bool, krkr::io::CheckExistentLocalFolder, (const ttstr &name));
/* name must be an OS's NATIVE folder name */

TJS_EXP_FUNC_DEF(bool, krkr::io::CheckExistentLocalFile, (const ttstr &name));
/* name must be an OS's NATIVE file name */

TJS_EXP_FUNC_DEF(bool, TVPCreateFolders, (const ttstr &folder));
/* make folders recursively, like mkdir -p. folder must be OS NATIVE
 * folder name
 */
//---------------------------------------------------------------------------

#ifdef TJS_SUPPORT_VCL
//---------------------------------------------------------------------------
// TTVPStreamAdapter
//---------------------------------------------------------------------------
/*
    this class provides VCL's TStream adapter for tTJSBinaryStream
*/
class TTVPStreamAdapter : public TStream {
private:
    tTJSBinaryStream *Stream;

public:
    __fastcall TTVPStreamAdapter(tTJSBinaryStream *ref);
    /*
        the stream passed by argument here is freed by this instance'
        destruction.
    */

    __fastcall ~TTVPStreamAdapter();

    int __fastcall Read(void *Buffer, int Count);
    // read
    int __fastcall Seek(int Offset, WORD Origin);
    // seek
    int __fastcall Write(const void *Buffer, int Count);

    __property Size;
    __property Position;
};
//---------------------------------------------------------------------------
#endif

class krkr::io::IStreamAdapter;

//---------------------------------------------------------------------------
// IStream creator
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(IStream *, krkr::io::CreateIStream,
                 (const ttstr &name, tjs_uint32 flags));

TJS_EXP_FUNC_DEF(IStream *, krkr::io::CreateIStream, (tTJSBinaryStream *));
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::io::BinaryStreamAdapter
//---------------------------------------------------------------------------
/*
        this class provides tTJSBinaryStream adapter for IStream
*/
class krkr::io::BinaryStreamAdapter : public tTJSBinaryStream {
    typedef tTJSBinaryStream inherited;

private:
    IStream *Stream;

public:
    krkr::io::BinaryStreamAdapter(IStream *ref);

    /*
        the stream passed by argument here is freed by this instance'
        destruction.
    */

    ~krkr::io::BinaryStreamAdapter() override;

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override;

    tjs_uint Read(void *buffer, tjs_uint read_size) override;

    tjs_uint Write(const void *buffer, tjs_uint write_size) override;

    tjs_uint64 GetSize() override;

    void SetEndOfStorage() override;
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// krkr::io::BinaryStreamAdapter creator
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(tTJSBinaryStream *, krkr::io::CreateBinaryStreamAdapter,
                 (IStream * refstream));
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPPluginHolder
//---------------------------------------------------------------------------
/*
        tTVPPluginHolder holds plug-in. if the plug-in is not a local
   storage, plug-in is to be extracted to temporary directory and be
   held until the program done using it.
*/
class tTVPPluginHolder {
private:
    ttstr LocalName;
    tTVPLocalTempStorageHolder *LocalTempStorageHolder;

public:
    tTVPPluginHolder(const ttstr &aname);

    ~tTVPPluginHolder();

    [[nodiscard]] const ttstr &GetLocalName() const;
};
//---------------------------------------------------------------------------

void krkr::io::ListDir(const std::string &folder,
                std::function<void(const std::string &, int)> cb);

bool krkr::io::SaveStreamToFile(tTJSBinaryStream *st, tjs_uint64 offset,
                         tjs_uint64 size, const ttstr &outpath);

// 复位 Storage 层缓存，使 engine_destroy 后可干净二次启动（runtime-restart）。
void krkr::io::ResetStorageImplForRestart();

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::tTVPLocalFileInfo;
using krkr::io::GetLocalFileListAt;
using krkr::io::LocalFileStream;
using krkr::io::CheckExistentLocalFolder;
using krkr::io::CheckExistentLocalFile;
using krkr::io::TVPCreateFolders;
using krkr::io::CreateIStream;
using krkr::io::BinaryStreamAdapter;
using krkr::io::CreateBinaryStreamAdapter;
using krkr::io::tTVPPluginHolder;
using krkr::io::ListDir;
using krkr::io::SaveStreamToFile;
using krkr::io::ResetStorageImplForRestart;

#endif
