//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2 Script Managing
//---------------------------------------------------------------------------
#ifndef ScriptMgnImtfH
#define ScriptMgnImtfH

#include "tjs.h"

#include "tjsNative.h"
#include "tjsError.h"

//---------------------------------------------------------------------------
// implementation in this unit
//---------------------------------------------------------------------------
namespace krkr::base {

extern ttstr StartupScriptName;

extern void InitScriptEngine();

extern void UninitScriptEngine();

extern void RestartScriptEngine();

extern void ResetScriptEngineForRestart();

extern tTJS *GetScriptEngine();

TJS_EXP_FUNC_DEF(iTJSDispatch2 *, GetScriptDispatch, ());

TJS_EXP_FUNC_DEF(void, ExecuteScript,
                 (const ttstr &content, tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteScript,
                 (const ttstr &content, iTJSDispatch2 *context,
                  tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteExpression,
                 (const ttstr &content, tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteExpression,
                 (const ttstr &content, iTJSDispatch2 *context,
                  tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteScript,
                 (const ttstr &content, const ttstr &name, tjs_int lineofs,
                  tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteScript,
                 (const ttstr &content, const ttstr &name, tjs_int lineofs,
                  iTJSDispatch2 *context, tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteExpression,
                 (const ttstr &content, const ttstr &name, tjs_int lineofs,
                  tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteExpression,
                 (const ttstr &content, const ttstr &name, tjs_int lineofs,
                  iTJSDispatch2 *context, tTJSVariant *result = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteStorage,
                 (const ttstr &name, tTJSVariant *result = nullptr,
                  bool isexpression = false,
                  const tjs_char *modestr = nullptr));

TJS_EXP_FUNC_DEF(void, ExecuteStorage,
                 (const ttstr &name, iTJSDispatch2 *context,
                  tTJSVariant *result = nullptr, bool isexpression = false,
                  const tjs_char *modestr = nullptr));

TJS_EXP_FUNC_DEF(void, DumpScriptEngine, ());

TJS_EXP_FUNC_DEF(void, ExecuteBytecode,
                 (const tjs_uint8 *content, size_t len, iTJSDispatch2 *context,
                  tTJSVariant *result = nullptr,
                  const tjs_char *name = nullptr));

extern void ExecuteStartupScript();

TJS_EXP_FUNC_DEF(void, CreateMessageMapFile, (const ttstr &filename));

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// implementation in each platform to show script exception message
//---------------------------------------------------------------------------
extern void ShowScriptException(eTJS &e);

extern void ShowScriptException(eTJSScriptError &e);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// BeforeProcessUnhandledException (implementation in each
// platform)
//---------------------------------------------------------------------------
extern void BeforeProcessUnhandledException();
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// unhandled exception handler related macros
//---------------------------------------------------------------------------
extern bool ProcessUnhandledException(eTJSScriptException &e);

extern bool ProcessUnhandledException(eTJSScriptError &e);

extern bool ProcessUnhandledException(eTJS &e);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// InitializeStartupScript
//---------------------------------------------------------------------------
extern void InitializeStartupScript();

extern bool CheckProcessLog();

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// unhandled exception handler related macros
//---------------------------------------------------------------------------
#define TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(origin)                            \
    catch(eTJSScriptException & e) {                                           \
        BeforeProcessUnhandledException();                                  \
        e.AddTrace(ttstr(origin));                                             \
        if(!ProcessUnhandledException(e))                                   \
            ShowScriptException(e);                                         \
    }                                                                          \
    catch(eTJSScriptError & e) {                                               \
        BeforeProcessUnhandledException();                                  \
        e.AddTrace(ttstr(origin));                                             \
        if(!ProcessUnhandledException(e))                                   \
            ShowScriptException(e);                                         \
    }                                                                          \
    catch(eTJS & e) {                                                          \
        BeforeProcessUnhandledException();                                  \
        if(!ProcessUnhandledException(e))                                   \
            ShowScriptException(e);                                         \
    }                                                                          \
    catch(...) {                                                               \
        BeforeProcessUnhandledException();                                  \
        throw;                                                                 \
    }
#define TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION_FORCE_SHOW_EXCEPTION(origin)       \
    catch(eTJSScriptError & e) {                                               \
        BeforeProcessUnhandledException();                                  \
        e.AddTrace(ttstr(origin));                                             \
        ShowScriptException(e);                                             \
    }                                                                          \
    catch(eTJS & e) {                                                          \
        BeforeProcessUnhandledException();                                  \
        ShowScriptException(e);                                             \
    }                                                                          \
    catch(...) {                                                               \
        BeforeProcessUnhandledException();                                  \
        throw;                                                                 \
    }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Scripts : TJS Scripts class
//---------------------------------------------------------------------------
class tTJSNC_Scripts : public tTJSNativeClass {
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_Scripts();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *CreateNativeClass_Scripts();
//---------------------------------------------------------------------------

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::tTJSNC_Scripts;

#endif
