//---------------------------------------------------------------------------
// kirikiroid2.dll —— Kirikiroid2 兼容面
//---------------------------------------------------------------------------
// Kirikiroid2（zeas2 血脉）时代的部分游戏脚本会直接用这两个成员：
//
//   _str_ord(text)              取字符串首字符的码点；非字符串原样返回
//   Storages.setTextEncoding(e) 设默认读取编码
//
// 它们是纯 TJS 层可见的兼容面，与图形/平台无关，所以按原样移植。
// 移植自 AetherKiri cpp/plugins/kirikiroid2.cpp（同源实现，逐行对照）。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "TextStream.h"
#include "ncbind.hpp"

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

namespace {

    tjs_error TJS_INTF_METHOD krkrStrOrd(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        // 非字符串参数原样返回：老脚本会拿它当"万能取值"用
        if(param[0]->Type() != tvtString) {
            if(result)
                *result = *param[0];
            return TJS_S_OK;
        }

        const tjs_char *text = param[0]->GetString();
        if(result)
            *result = (text && *text) ? static_cast<tjs_int>(*text) : 0;
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD krkrSetTextEncoding(tTJSVariant *,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(param[0]->Type() == tvtString)
            TVPSetDefaultReadEncoding(param[0]->AsStringNoAddRef());
        return TJS_S_OK;
    }

} // namespace

NCB_REGISTER_FUNCTION(_str_ord, krkrStrOrd);
NCB_ATTACH_FUNCTION(setTextEncoding, Storages, krkrSetTextEncoding);
