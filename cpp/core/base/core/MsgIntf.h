//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Definition of Messages and Message Related Utilities
//---------------------------------------------------------------------------
#ifndef MsgIntfH
#define MsgIntfH

#include "tjs.h"
#include "tjsMessage.h"

#ifndef TVP_MSG_DECL
#define TVP_MSG_DECL(name, msg) extern tTJSMessageHolder name;
#define TVP_MSG_DECL_CONST(name, msg) extern tTJSMessageHolder name;
#define TVP_MSG_DECL_NULL(name) extern tTJSMessageHolder name;
#endif

#include "MsgImpl.h"

#define WIDEN2(x) TJS_W(x)
#define WIDEN(x) WIDEN2(x)

//---------------------------------------------------------------------------
// Message Strings
//---------------------------------------------------------------------------
#include "MsgIntfInc.h"

//---------------------------------------------------------------------------
// Utility Functions
//---------------------------------------------------------------------------
namespace krkr::base {

TJS_EXP_FUNC_DEF(ttstr, FormatMessage,
                 (const tjs_char *msg, const ttstr &p1));

TJS_EXP_FUNC_DEF(ttstr, FormatMessage,
                 (const tjs_char *msg, const ttstr &p1, const ttstr &p2));

TJS_EXP_FUNC_DEF(void, ThrowExceptionMessage, (const tjs_char *msg));

TJS_EXP_FUNC_DEF(void, ThrowExceptionMessage,
                 (const tjs_char *msg, const ttstr &p1, tjs_int num));

TJS_EXP_FUNC_DEF(void, ThrowExceptionMessage,
                 (const tjs_char *msg, const ttstr &p1));

TJS_EXP_FUNC_DEF(void, ThrowExceptionMessage,
                 (const tjs_char *msg, const ttstr &p1, const ttstr &p2));

TJS_EXP_FUNC_DEF(ttstr, GetAboutString, ());

TJS_EXP_FUNC_DEF(ttstr, GetVersionInformation, ());

TJS_EXP_FUNC_DEF(ttstr, GetVersionString, ());

} // namespace krkr::base

#define KRKR_THROW_INTERNAL_ERROR                                                  \
    ThrowExceptionMessage(TVPInternalError, __FILE__, __LINE__)
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// version retrieving
//---------------------------------------------------------------------------
namespace krkr::base {

extern tjs_int VersionMajor;
extern tjs_int VersionMinor;
extern tjs_int VersionRelease;
extern tjs_int VersionBuild;

//---------------------------------------------------------------------------
extern void GetVersion();
/*
        implement in each platforms;
        fill these four version field.
*/
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(void, GetSystemVersion,
                 (tjs_int & major, tjs_int &minor, tjs_int &release,
                  tjs_int &build));

TJS_EXP_FUNC_DEF(void, GetTJSVersion,
                 (tjs_int & major, tjs_int &minor, tjs_int &release));
//---------------------------------------------------------------------------

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除

// 消息目录（MsgIntfInc.h 的 151 条 + impl/MsgImpl.h 的 192 条 tTJSMessageHolder）
// 刻意留在全局：两个文件都是 `gentext.pl Messages.xlsx` 的生成文件，
// 手改会被覆盖；且共 343 个符号，全部搬进 namespace 要写 343 条 using。

#endif
