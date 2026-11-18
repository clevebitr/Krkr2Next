#pragma once
// 旧渲染层（legacy visual）的消息接口。
//
// 这不是上游原样拷贝，理由写在 vendor/krkrz/VENDOR.md §4：
//   - 上游 `msg/MsgIntfInc.h` 里 152 条消息全是 `TVP_MSG_DECL_NULL`（空串）；
//   - 真正的文案在上游 `msg/text/Messages.xlsx`，构建期由 Perl/Excel 生成 Win32
//     字符串资源，运行时靠 `LoadString` 读入 —— Android 上没有这条路径。
// 所以这里保留**同一套符号与签名**，文案改为由 `tools/gen_legacy_messages.py` 生成，
// 生成物在 `generated/messages.inc`。
//
// 只声明上游消息接口里实际被引用到的部分；新增符号跟着编译/链接错误补（链接驱动）。

#include "tjs.h"
#include "tjsMessage.h"

// 与上游 MsgIntf.h 相同的宏三态，保证上游源码里的 `TVP_MSG_DECL*` 语义不变。
#ifndef TVP_MSG_DECL
#define TVP_MSG_DECL(name, msg) extern tTJSMessageHolder name;
#define TVP_MSG_DECL_CONST(name, msg) extern tTJSMessageHolder name;
#define TVP_MSG_DECL_NULL(name) extern tTJSMessageHolder name;
#endif

#define LEGACY_MESSAGE(name, text) extern tTJSMessageHolder name;
#include "generated/messages.inc"
#undef LEGACY_MESSAGE

//---------------------------------------------------------------------------
// 工具函数（签名与上游一致）
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(ttstr, TVPFormatMessage, (const tjs_char *msg, const ttstr & p1));
TJS_EXP_FUNC_DEF(ttstr, TVPFormatMessage, (const tjs_char *msg, const ttstr & p1,
                                           const ttstr & p2));
TJS_EXP_FUNC_DEF(void, TVPThrowExceptionMessage, (const tjs_char *msg));
TJS_EXP_FUNC_DEF(void, TVPThrowExceptionMessage, (const tjs_char *msg, const ttstr &p1));
TJS_EXP_FUNC_DEF(void, TVPThrowExceptionMessage, (const tjs_char *msg, const ttstr &p1,
                                                  const ttstr &p2));
TJS_EXP_FUNC_DEF(void, TVPThrowExceptionMessage, (const tjs_char *msg, const ttstr &p1,
                                                  tjs_int num));

#define TVPThrowInternalError TVPThrowExceptionMessage(TVPInternalError, __FILE__, __LINE__)
