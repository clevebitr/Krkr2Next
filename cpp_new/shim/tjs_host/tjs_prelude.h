#pragma once
// 垫层：用 -include 强制包含在所有 tjs2 源之前。
// 只做两件事：声明上游在非 MSVC 上缺失的函数 / 清理 Win32 宏，不改任何实现。

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef GetMessage
#undef GetObject
#undef PlaySound
#undef min
#undef max
#endif

#include <cstddef>
// tjs2 假设某些标准头已经被宿主包含（MSVC 的 <windows.h> 链会带进来）。
// 这里统一补齐，避免去改权威实现。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstdarg>

// tjsConfig.h 在 __GNUC__ 分支把 TJS_snprintf 映射到 wsnprintf（Borland 的扩展），
// GCC/MinGW 没有这个函数。补一个等价实现（宽字符版 snprintf）。
#include <cstdarg>
#include <cwchar>
inline int wsnprintf(wchar_t *buf, size_t count, const wchar_t *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int result = vswprintf(buf, count, fmt, args);
    va_end(args);
    return result;
}

// tjs.cpp 在 TJS_NO_REGEXP 下仍然调用 TJSReleaseRegex()（上游的开关是半成品）。
// 这里补声明，实现在 core/script/TjsRegExpShim.cpp（垫层，不改权威实现）。
namespace TJS {
    class iTJSDispatch2;
    extern iTJSDispatch2 *TJSCreateRegExpClass();
    extern void TJSReleaseRegex();
}

// tjs2 的若干头（如 tjsError.h）假设 tjs.h 已经被包含。强制在这里定好顺序，
// 免得为了"改 include 顺序"去改权威实现。
#include "tjs/tjs.h"

// GCC/libstdc++ 在 -std=c++20 下不定义 M_E 等 POSIX 数学常量（MSVC 需要 _USE_MATH_DEFINES）。
// tjsMath.cpp 直接用它们，这里补齐，避免改权威实现。
#ifndef M_E
#define M_E        2.7182818284590452354
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440
#endif
