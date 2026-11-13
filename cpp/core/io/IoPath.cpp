/**
 * IO 组件：路径解析（存储名的扩展名 / 文件名 / 路径拆分，以及档案分隔符）。
 */
#include "tjsCommHead.h"
#include "StorageIntf.h"

namespace krkr::io {

    tjs_char TVPArchiveDelimiter = '>';

    ttstr TVPExtractStorageExt(const ttstr &name) {

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

} // namespace krkr::io

// TODO: 过渡兼容,完成后删除
using krkr::io::TVPExtractStorageExt;
using krkr::io::TVPExtractStorageName;
using krkr::io::TVPExtractStoragePath;
using krkr::io::TVPChopStorageExt;