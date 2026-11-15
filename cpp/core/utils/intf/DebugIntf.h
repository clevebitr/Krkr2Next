//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Utilities for Debugging
//---------------------------------------------------------------------------
#ifndef DebugIntfH
#define DebugIntfH

#include "tjsNative.h"
#include "tjs.h"

namespace krkr::utils {

//---------------------------------------------------------------------------
// global definitions
//---------------------------------------------------------------------------
extern bool autoLogToFileOnError;
extern bool autoClearLogOnError;
extern bool loggingToFile;

extern void SetOnLog(void (*func)(const ttstr &line));

// 释放脚本注册的全部日志闭包（tTJSVariantClosure）。
// 必须在脚本引擎销毁（krkr::base::UninitScriptEngine）之前调用，否则闭包 finalizer
// 会命中已失效的 TJS 全局态导致退出卡死（runtime-restart）。由
// Application::OnExit 在销毁引擎前调用；DestroyLoggingHandlerVector
// at-exit handler 仍保留作兜底（届时引擎已死，向量应为空/无闭包）。
extern void ClearLoggingHandlers();

TJS_EXP_FUNC_DEF(void, AddLog, (const ttstr &line));

TJS_EXP_FUNC_DEF(void, AddImportantLog, (const ttstr &line));

extern ttstr GetLastLog(tjs_uint n);

extern iTJSConsoleOutput *GetTJS2ConsoleOutputGateway();

extern iTJSConsoleOutput *GetTJS2DumpOutputGateway();

extern void StartTJS2Dump();

extern void EndTJS2Dump();

extern void OnError();

extern ttstr GetImportantLog();

extern void SetLogLocation(const ttstr &loc);

extern ttstr nativeLogLocation;

extern void StartLogToFile(bool clear);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// implement in each platform
//---------------------------------------------------------------------------
// extern void OnErrorHook();
// called from OnError, on system error.
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Debug : TJS Debug Class
//---------------------------------------------------------------------------
class tTJSNC_Debug : public tTJSNativeClass {
public:
    tTJSNC_Debug();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *TVPCreateNativeClass_Debug();
//---------------------------------------------------------------------------

} // namespace krkr::utils

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::utils::tTJSNC_Debug;
using krkr::utils::TVPCreateNativeClass_Debug;

#endif
