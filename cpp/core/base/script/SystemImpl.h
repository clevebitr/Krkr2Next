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
TJS_EXP_FUNC_DEF(bool, GetAsyncKeyState,
                 (tjs_uint keycode, bool getcurrent = true));

//---------------------------------------------------------------------------
extern void PostApplicationActivateEvent();

extern void PostApplicationDeactivateEvent();

extern bool ShellExecute(const TJS::ttstr &target, const TJS::ttstr &param);

extern void DoSaveSystemVariables();

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
//---------------------------------------------------------------------------
#endif
