//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------

#ifndef ThreadIntfH
#define ThreadIntfH

#include "tjsNative.h"
#include <functional>

namespace krkr::utils {
    enum tTVPThreadPriority {
        ttpIdle,
        ttpLowest,
        ttpLower,
        ttpNormal,
        ttpHigher,
        ttpHighest,
        ttpTimeCritical
    };
} // namespace krkr::utils

#include "ThreadImpl.h"

namespace krkr::utils {

    const tjs_int TVPMaxThreadNum = 8;
    typedef const std::function<void(int)> &TVP_THREAD_TASK_FUNC;


    TJS_EXP_FUNC_DEF(tjs_int, TVPGetProcessorNum, ());

    TJS_EXP_FUNC_DEF(tjs_int, TVPGetThreadNum, ());

    TJS_EXP_FUNC_DEF(void, TVPExecThreadTask,
                     (int numThreads, TVP_THREAD_TASK_FUNC func));
} // namespace krkr::utils

// TODO: 过渡兼容，迁移完成后删除
using krkr::utils::tTVPThreadPriority;
using krkr::utils::ttpHigher;
using krkr::utils::ttpHighest;
using krkr::utils::ttpIdle;
using krkr::utils::ttpLower;
using krkr::utils::ttpLowest;
using krkr::utils::ttpNormal;
using krkr::utils::ttpTimeCritical;

using krkr::utils::TVP_THREAD_TASK_FUNC;
using krkr::utils::TVPExecThreadTask;
using krkr::utils::TVPGetProcessorNum;
using krkr::utils::TVPGetThreadNum;
using krkr::utils::TVPMaxThreadNum;

#endif
