//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "System" class implementation
//---------------------------------------------------------------------------
#ifndef SystemImplH
#define SystemImplH

#include "tjsTypes.h"
#include "tjsString.h"

namespace krkr::base {

//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(bool, TVPGetAsyncKeyState,
                 (tjs_uint keycode, bool getcurrent = true));

//---------------------------------------------------------------------------
extern void TVPPostApplicationActivateEvent();

extern void TVPPostApplicationDeactivateEvent();

extern bool TVPShellExecute(const TJS::ttstr &target, const TJS::ttstr &param);

extern void TVPDoSaveSystemVariables();

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::TVPGetAsyncKeyState;
using krkr::base::TVPPostApplicationActivateEvent;
using krkr::base::TVPPostApplicationDeactivateEvent;
using krkr::base::TVPShellExecute;
using krkr::base::TVPDoSaveSystemVariables;
//---------------------------------------------------------------------------
#endif
