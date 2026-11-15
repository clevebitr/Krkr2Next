//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// safe 64bit System Tick Count
//---------------------------------------------------------------------------
#ifndef TickCountH
#define TickCountH

#include "tjsTypes.h"

namespace krkr::utils {
    TJS_EXP_FUNC_DEF(tjs_uint64, GetTickCount, ());

    extern tjs_uint32 GetRoughTickCount32();

    extern void StartTickCount();
    // this must be called before GetTickCount(), in main thread.
    // this function can be called more than one time.

} // namespace krkr::utils

// TODO: 过渡兼容，迁移完成后删除

#endif
