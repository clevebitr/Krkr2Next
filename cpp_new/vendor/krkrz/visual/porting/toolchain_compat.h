#pragma once
// 让我们 vendored 的 krkrz 像素代码在非 Windows 工具链上编译。
//
// 上游 visual/tvpgl.h 与 visual/IA32/tvpgl_ia32_intf.h 把这组宏写在
// `#ifdef _WIN32` 里（展开成带 `__cdecl` 的函数指针），非 Windows 下宏不存在，
// 于是头文件里用宏写的声明全部断掉。这里在包含上游头之前补齐同名宏，
// 使上游文件保持零修改（补丁面最小，见 vendor/krkrz/VENDOR.md §3）。
//
// 只用 `-include` 的方式引入本文件，不要手写进其它头。

// 本头被 C（tvpgl.c）与 C++ 同时包含，且是 -include 的第一批头：
// 先把 tjs 的基础类型带进来（`tjs_char` 等），避免下面的声明依赖包含顺序。
#include "tjsTypes.h"

#if !defined(_WIN32)

// 函数指针表（tvpgl.h 的导出约定）。后缀含义见上游 tvpgl.h：
//   d = 目标带 alpha；a = 目标带 additive-alpha；o = 带 opacity；HDA = 保护目标 alpha。
#define TVP_GL_FUNC_DECL(rettype, funcname, arg)             rettype funcname arg
#define TVP_GL_FUNC_EXTERN_DECL(rettype, funcname, arg)      extern rettype funcname arg
#define TVP_GL_FUNC_PTR_DECL(rettype, funcname, arg)         rettype (*funcname) arg
#define TVP_GL_FUNC_PTR_EXTERN_DECL_(rettype, funcname, arg) extern rettype (*funcname) arg
#define TVP_GL_FUNC_PTR_EXTERN_DECL                          TVP_GL_FUNC_PTR_EXTERN_DECL_

// IA32 派发声明（tvpgl_ia32_intf.h）。本平台不实现这些实现，只保证头文件可解析。
#define TVP_GL_IA32_FUNC_DECL(rettype, funcname, arg)             rettype funcname arg
#define TVP_GL_IA32_FUNC_EXTERN_DECL(rettype, funcname, arg)      extern rettype funcname arg
#define TVP_GL_IA32_FUNC_PTR_DECL(rettype, funcname, arg)         rettype (*funcname) arg
#define TVP_GL_IA32_FUNC_PTR_EXTERN_DECL(rettype, funcname, arg)  extern rettype (*funcname) arg

#endif // !_WIN32

// ---------------------------------------------------------------------------
// 上游的非 Windows 分支缺一些 Win32 类型/常量。它们只是 POD 结构与句柄类型，
// 在头文件里被用到；实现（GDI 区域、位图 API）由 porting 层自己提供。
// ---------------------------------------------------------------------------
#if !defined(_WIN32)
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// 用 `typedef struct tag {...}` 的写法：本头同时被 C（tvpgl.c）与 C++ 包含。
typedef struct tagBITMAPINFOHEADER {
  unsigned long biSize;
  long biWidth;
  long biHeight;
  unsigned short biPlanes;
  unsigned short biBitCount;
  unsigned long biCompression;
  unsigned long biSizeImage;
  long biXPelsPerMeter;
  long biYPelsPerMeter;
  unsigned long biClrUsed;
  unsigned long biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagRGBQUAD {
  unsigned char rgbBlue;
  unsigned char rgbGreen;
  unsigned char rgbRed;
  unsigned char rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO;

typedef struct HRGN__ *HRGN;

// 上游在 Platform 缺失时用 Win32 的调试输出（窄/宽两个版本），
// 由 porting/debug_output.cpp 实现。
void OutputDebugString(const char *text);
#ifdef __cplusplus
// C 没有重载；宽字符版本只给 C++ 的上游源码用（ComplexRect 等）。
void OutputDebugString(const tjs_char *text);
#endif
#endif // !_WIN32
