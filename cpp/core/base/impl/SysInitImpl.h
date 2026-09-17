//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#ifndef SysInitImplH
#define SysInitImplH

//---------------------------------------------------------------------------
extern void TVPDumpHWException();

extern void TVPInitializeBaseSystems();

extern ttstr TVPNativeProjectDir;
extern ttstr TVPNativeDataPath;

extern bool TVPProjectDirSelected;

extern void TVPEnsureDataPathDirectory();

extern bool TVPExecuteUserConfig();

extern bool TVPTerminated;
extern bool TVPTerminateWindowClosed;
extern bool TVPTerminateOnWindowClose;
extern bool TVPTerminateOnNoWindowStartup;
extern int TVPTerminateCode;
extern bool TVPHostSuppressProcessExit;

extern void TVPResetSysInitImplForRestart();

//---------------------------------------------------------------------------
// "游戏请求关窗"的宿主确认闸门（Android 壳用）。
//
// 原逻辑：KAG 的退出菜单（`kag.close()` → `MainWindow.close()` →
// `Window.close()` → `HostWindowLayer::Close()`）一进来就置
// TVPTerminateWindowClosed 并终止，宿主只能直接回库 —— 用户要求的"先问一句
// 要不要退出、选继续就接着玩"没有落点。这里把**终止之前的窗口关闭**挂起来：
// 窗口不拆、closing_ 不置、也不终止，engine_tick 用独立结果码告诉宿主
// （ENGINE_RESULT_WINDOW_CLOSE_REQUESTED），宿主弹确认框后再调
// engine_resolve_window_close() 二选一：
//   - 允许 → 回到真正的关窗终止流程（下一帧 engine_tick 报 WINDOW_CLOSED）；
//   - 不允许 → 只清请求，窗口与脚本都完好，游戏接着跑。
// 只有宿主显式开启（SetDeferWindowClose(true)）才生效，默认关闭保持原行为。
namespace krkr::host {
/** 宿主是否要求"关窗前先问用户"。 */
void SetDeferWindowClose(bool defer);
bool DeferWindowClose();
/** 游戏请求关窗：返回 true 表示已由宿主接管（调用方不得再自行终止）。 */
bool RequestWindowClose();
/** 是否有未决的关窗请求（超过兜底时限会自动转为确认关窗）。 */
bool WindowClosePending();
/** 宿主选"继续游戏"：扔掉未决请求。 */
void CancelWindowClose();
/** 宿主选"退出游戏"（或兜底超时）：执行真正的关窗终止。 */
void ConfirmWindowClose();
} // namespace krkr::host

//---------------------------------------------------------------------------

#include "SysInitIntf.h"

#endif
