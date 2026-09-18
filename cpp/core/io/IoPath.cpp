//
// IO 组件：路径解析（存储名的扩展名 / 文件名 / 路径拆分，以及档案分隔符）。
//
// 这是 `cpp/core/io/` 的第一块实现：把**纯函数**的路径解析从 `StorageIntf.cpp` 搬过来。
// 公开 API（`StorageIntf.h` 里的 TVP* 声明）与行为完全不变，只是定义换了 TU。
//
// 为什么先搬这一块：它不依赖任何其他核心符号（只用 ttstr 与 TVPArchiveDelimiter），
// 因此模块依赖是单向的 —— core_base_module -> core_io_module，不会出现循环依赖。
//
// 后续（M1.2 余下 / M1.3–M1.5）陆续搬入：存储媒体注册表、auto-path 表与挂载排序、
// 归档工厂、TJS `Storages` 门面。搬完后 `StorageIntf.h` 只保留门面声明，实现全在本目录。
//
#include "tjsCommHead.h"
#include "StorageIntf.h"

//---------------------------------------------------------------------------
// 档案分隔符：`xxx.xp3>目录/文件` 里的那个 '>'。
// this changes '>' from '#' since 2.19 beta 14
//---------------------------------------------------------------------------
// archive delimiter
// this changes '>' from '#' since 2.19 beta 14
tjs_char TVPArchiveDelimiter = '>';

//---------------------------------------------------------------------------
// TVPExtractStorageExt
//---------------------------------------------------------------------------
ttstr TVPExtractStorageExt(const ttstr &name) {
    // extract an extension from name.
    // returned string will contain extension delimiter ( '.' ),
    // except for missing extension of the input string. ( returns
    // nullptr string when input string does not have an extension )

    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;
        if(*p == TJS_W('.')) {
            // found extension delimiter
            size_t extlen = (slen - (p - s));
            return { p, extlen };
        }

        p--;
    }

    // not found
    return {};
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExtractStorageName
//---------------------------------------------------------------------------
ttstr TVPExtractStorageName(const ttstr &name) {
    // extract "name"'s storage name ( excluding path ) and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;

        p--;
    }

    p++;
    if(p == s)
        return name;
    else
        return { p, (size_t)(slen - (p - s)) };
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExtractStoragePath
//---------------------------------------------------------------------------
ttstr TVPExtractStoragePath(const ttstr &name) {
    // extract "name"'s path ( including last delimiter ) and return
    // it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;

        p--;
    }

    p++;
    return { s, (size_t)(p - s) };
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPChopStorageExt
//---------------------------------------------------------------------------
extern ttstr TVPChopStorageExt(const ttstr &name) {
    // chop storage's extension and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;
        if(*p == TJS_W('.')) {
            // found extension delimiter
            return { s, (size_t)(p - s) };
        }

        p--;
    }

    // not found
    return name;
}
