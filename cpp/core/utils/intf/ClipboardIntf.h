//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Clipboard Class interface
//---------------------------------------------------------------------------
#ifndef ClipboardIntfH
#define ClipboardIntfH

#include "tjsNative.h"

namespace krkr::utils {

/*[*/
//---------------------------------------------------------------------------
// ClipboardFormat
//---------------------------------------------------------------------------
enum ClipboardFormat { cbfText = 1 };
/*]*/

//---------------------------------------------------------------------------
// implement these in each platform
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(bool, HasClipboardFormat, (ClipboardFormat format));

TJS_EXP_FUNC_DEF(void, SetClipboardText, (const ttstr &text));

TJS_EXP_FUNC_DEF(bool, GetClipboardText, (ttstr & text));

//---------------------------------------------------------------------------
// tTJSNI_BaseClipboard
//---------------------------------------------------------------------------
class tTJSNI_BaseClipboard : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *dsp) override;

    void Invalidate() override;
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Clipboard : TJS Clipboard Class
//---------------------------------------------------------------------------
class tTJSNC_Clipboard : public tTJSNativeClass {
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_Clipboard();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *TVPCreateNativeClass_Clipboard();
//---------------------------------------------------------------------------

} // namespace krkr::utils

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::utils::cbfText;
using krkr::utils::tTJSNI_BaseClipboard;
using krkr::utils::tTJSNC_Clipboard;
using krkr::utils::TVPCreateNativeClass_Clipboard;

#endif
