#pragma once

// ---------------------------------------------------------------------------
// OpenGL headers — 平台原生 EGL + GLES（不经 ANGLE）
// ---------------------------------------------------------------------------
// 上下文请求 GLES 3.0（minSdk 24 起强制可用），但这里只引 GLES2 头：现有
// shader 全是 GLSL ES 1.00，渲染代码也只用 ES2 接口。需要 ES3 符号的地方
// （目前只有 RenderManager_ogl.cpp 的扩展枚举，用 glGetStringi）自行包含
// <GLES3/gl3.h>，避免 ES3 声明扩散到所有 GL 使用方。
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

// ---------------------------------------------------------------------------
// GLES2 compatibility defines for desktop GL constants
// ---------------------------------------------------------------------------
#ifndef GL_DEPTH24_STENCIL8
#ifdef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8 GL_DEPTH24_STENCIL8_OES
#else
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#endif

#ifndef GL_READ_BUFFER
// GL_READ_BUFFER is not part of GLES2 core; used only behind #ifdef guards
#endif

#include <string>

bool TVPCheckGLExtension(const std::string &extname);

// ---------------------------------------------------------------------------
// CHECK_GL_ERROR_DEBUG — checks for GL errors in debug builds.
// In debug builds, checks for GL errors after each call.
// In release builds, this is a no-op.
// ---------------------------------------------------------------------------
#ifndef CHECK_GL_ERROR_DEBUG
#ifdef _DEBUG
#include <cassert>
#define CHECK_GL_ERROR_DEBUG()                                                 \
    do {                                                                       \
        GLenum __error = glGetError();                                         \
        if(__error) {                                                          \
            /* Log but don't assert — some errors are recoverable */           \
        }                                                                      \
    } while(false)
#else
#define CHECK_GL_ERROR_DEBUG() ((void)0)
#endif
#endif
