//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000-2007 W.Dee <dee@kikyou.info> and
   contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Text Editor
//---------------------------------------------------------------------------
#ifndef PadIntfH
#define PadIntfH

#include "tjsNative.h"

namespace krkr::utils {

//---------------------------------------------------------------------------
// tTJSNI_BasePad
//---------------------------------------------------------------------------
class tTJSNI_BasePad : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *dsp) override;

    void Invalidate() override;
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// font ralated constants
// core/visual/tvpfontstruc.h Ç∆ìØÇ∂Ç‡ÇÃÇ≈Ç∑ÅB
//---------------------------------------------------------------------------
// #define TVP_TF_ITALIC    0x01
// #define TVP_TF_BOLD      0x02
// #define TVP_TF_UNDERLINE 0x04
// #define TVP_TF_STRIKEOUT 0x08
//---------------------------------------------------------------------------

} // namespace krkr::utils

#include "PadImpl.h"

namespace krkr::utils {

//---------------------------------------------------------------------------
// tTJSNC_Pad : TJS Pad Class
//---------------------------------------------------------------------------
class tTJSNC_Pad : public tTJSNativeClass {
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_Pad();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Pad();
//---------------------------------------------------------------------------

} // namespace krkr::utils

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::utils::tTJSNI_BasePad;
// tTJSNI_Pad 由 PadImpl.h 导出（PadIntf.h 会 include 它）
using krkr::utils::tTJSNC_Pad;
using krkr::utils::TVPCreateNativeClass_Pad;

#endif
