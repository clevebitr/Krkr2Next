#pragma once
// 上游在几处用 Win32 的 `OutputDebugString` 输出调试信息（例如 ComplexRect 的断言）。
// Android 没有这条通道，这里把它映射到 core 日志；签名保持 Win32 的窄字符版本。
// 只在非 Windows 构建里使用（见 vendor/krkrz/visual/porting/toolchain_compat.h）。

#if !defined(_WIN32)
#include "tjsTypes.h"

// Win32 有两个版本（窄/宽）；上游 ComplexRect 用的是宽字符版。
void OutputDebugString(const char *text);
void OutputDebugString(const tjs_char *text);
#endif
