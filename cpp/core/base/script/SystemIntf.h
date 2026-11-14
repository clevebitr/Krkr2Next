//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "System" class interface
//---------------------------------------------------------------------------
#ifndef SystemIntfH
#define SystemIntfH

#include "tjsNative.h"

namespace krkr::base {

//---------------------------------------------------------------------------
// tTJSNC_System : TJS System class
//---------------------------------------------------------------------------
class tTJSNC_System : public tTJSNativeClass {
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_System();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *TVPCreateNativeClass_System();

} // namespace krkr::base

//---------------------------------------------------------------------------
// 以下两个由各平台实现（environ/android、environ/stubs），暂留全局。
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(ttstr, TVPGetPlatformName, ());
// retrieve platform name (eg. "Win32")
// implement in each platform.
TJS_EXP_FUNC_DEF(ttstr, TVPGetOSName, ());

// retrieve OS name
// implement in each platform.
//---------------------------------------------------------------------------
namespace krkr::base {

extern void TVPFireOnApplicationActivateEvent(bool activate_or_deactivate);

extern tjs_int TVPGetOSBits();

} // namespace krkr::base
//---------------------------------------------------------------------------

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::tTJSNC_System;
using krkr::base::TVPCreateNativeClass_System;
using krkr::base::TVPFireOnApplicationActivateEvent;
using krkr::base::TVPGetOSBits;
#endif
