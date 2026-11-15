//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#ifndef SysInitIntfH
#define SysInitIntfH

//---------------------------------------------------------------------------
// System initialization and uninitialization
//---------------------------------------------------------------------------

//-- global data
namespace krkr::base {

extern ttstr projectDir; // project directory
extern ttstr dataPath; // data directory

//-- implementation in this unit
extern void SystemInit();

extern void SystemUninit();

//-- implement in each platform
extern void BeforeSystemInit(); // this must set projectDir
extern void AfterSystemInit();

extern void BeforeSystemUninit();

extern void AfterSystemUninit();

extern void
TerminateAsync(int code = 0); // do acynchronous teminating of application
extern void TerminateSync(
    int code = 0); // do synchronous teminating of application(never return)
extern void MainWindowClosed(); // called from WindowIntf.cpp,
                                   // caused by closing main window.
// this function must shutdown the application, unless the controller
// window is visible.
//---------------------------------------------------------------------------

extern bool systemUninitCalled;
// whether SystemUninit is called or not

extern void ResetRuntimeForRestart();
// reset internal state to allow re-initialization after engine_destroy

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// AtExit related
//---------------------------------------------------------------------------
void AddAtExitHandler(tjs_int pri, void (*handler)());

struct AtExit {
    AtExit(tjs_int pri, void (*handler)()) {
        AddAtExitHandler(pri, handler);
    }
};

#define KRKR_ATEXIT_PRI_PREPARE 10
#define KRKR_ATEXIT_PRI_SHUTDOWN 100
#define KRKR_ATEXIT_PRI_RELEASE 1000
#define KRKR_ATEXIT_PRI_CLEANUP 10000
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Command line parameter operations (implement in each platform)
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(bool, GetCommandLine,
                 (const tjs_char *name, tTJSVariant *value = nullptr));
// retrieves command line parameter named "name".
// command line parameter format must be "-name=value"
// returns false if the the parameter is not exist, otherwise
// sets the value to "value" and returns true.
TJS_EXP_FUNC_DEF(tjs_int, GetCommandLineArgumentGeneration, ());
// retrieves command line argument generation count. you can check
// whether the command line options has changed, by comparing this
// value to your value which is remembered when of previous call of
// this.
TJS_EXP_FUNC_DEF(void, SetCommandLine,
                 (const tjs_char *name, const ttstr &value));
// sets command line to the specified value.
// note that this function does not check any consistency or
// correctness of the value.

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::TerminateAsync;
using krkr::base::AddAtExitHandler;
using krkr::base::SetCommandLine;

//---------------------------------------------------------------------------

#endif
