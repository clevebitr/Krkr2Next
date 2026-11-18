#pragma once

#include <cstddef>
#include <cstdint>

// GL 函数指针表：垫层与平台都不 include 任何 GL 头，由平台把入口点注入。
//
// 好处：CI 与没有 GL 的环境照常编译；Android(GLES3) 与桌面(GL3.3) 共用同一份后端；
// 以后接 SDL 只是"又一个 loader"，渲染核心不动。
// 只声明用到的入口点与常量，新增需求时按同一形式追加。

#define KRKR_GL_NO_ERROR 0
#define KRKR_GL_TRIANGLE_STRIP 0x0005
#define KRKR_GL_FLOAT 0x1406
#define KRKR_GL_UNSIGNED_BYTE 0x1401
#define KRKR_GL_UNSIGNED_INT 0x1405
#define KRKR_GL_TEXTURE_2D 0x0DE1
#define KRKR_GL_TEXTURE0 0x84C0
#define KRKR_GL_TEXTURE_MIN_FILTER 0x2801
#define KRKR_GL_TEXTURE_MAG_FILTER 0x2800
#define KRKR_GL_TEXTURE_WRAP_S 0x2802
#define KRKR_GL_TEXTURE_WRAP_T 0x2803
#define KRKR_GL_NEAREST 0x2600
#define KRKR_GL_CLAMP_TO_EDGE 0x812F
#define KRKR_GL_RGBA 0x1908
#define KRKR_GL_ARRAY_BUFFER 0x8892
#define KRKR_GL_STREAM_DRAW 0x88E0
#define KRKR_GL_STATIC_DRAW 0x88E4
#define KRKR_GL_VERTEX_SHADER 0x8B31
#define KRKR_GL_FRAGMENT_SHADER 0x8B30
#define KRKR_GL_COMPILE_STATUS 0x8B81
#define KRKR_GL_LINK_STATUS 0x8B82
#define KRKR_GL_BLEND 0x0BE2
#define KRKR_GL_SCISSOR_TEST 0x0C11
#define KRKR_GL_SRC_ALPHA 0x0302
#define KRKR_GL_ONE_MINUS_SRC_ALPHA 0x0303
#define KRKR_GL_ONE 1
#define KRKR_GL_ZERO 0
#define KRKR_GL_DST_COLOR 0x0306
#define KRKR_GL_FUNC_REVERSE_SUBTRACT 0x800B
#define KRKR_GL_FUNC_ADD 0x8006
#define KRKR_GL_COLOR_BUFFER_BIT 0x00004000
#define KRKR_GL_FRAMEBUFFER 0x8D40
#define KRKR_GL_RENDERBUFFER 0x8D41
#define KRKR_GL_COLOR_ATTACHMENT0 0x8CE0
#define KRKR_GL_DEPTH_ATTACHMENT 0x8D00
#define KRKR_GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define KRKR_GL_UNPACK_ALIGNMENT 0x0CF5
#define KRKR_GL_PACK_ALIGNMENT 0x0D05
#define KRKR_GL_NO_ERROR_VALUE 0

namespace krkr::render {

  using GLenum = unsigned int;
  using GLuint = unsigned int;
  using GLint = int;
  using GLsizei = int;
  using GLfloat = float;
  using GLbitfield = unsigned int;
  using GLboolean = unsigned char;
  using GLchar = char;
  using GLsizeiptr = std::ptrdiff_t;

  struct GLApi {
    // 状态与缓冲
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Clear)(GLbitfield) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    void (*BlendEquation)(GLenum) = nullptr;
    GLenum (*GetError)() = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) = nullptr;

    // 纹理
    void (*GenTextures)(GLsizei, GLuint *) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint *) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                       const void *) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;

    // 缓冲与顶点属性
    void (*GenVertexArrays)(GLsizei, GLuint *) = nullptr;
    void (*BindVertexArray)(GLuint) = nullptr;
    void (*GenBuffers)(GLsizei, GLuint *) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum) = nullptr;
    void (*EnableVertexAttribArray)(GLuint) = nullptr;
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;

    // 着色器
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint *) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint *) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar *) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;

    // 离屏帧缓冲（桌面验证用；Android 8c 直接画到默认帧缓冲）
    void (*GenFramebuffers)(GLsizei, GLuint *) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
    void (*DeleteFramebuffers)(GLsizei, const GLuint *) = nullptr;


// 需要装载的入口点清单：平台各自的 loader 用这个宏展开，避免两边漏项。
#define KRKR_GL_FUNCTION_LIST(X) \
  X(ClearColor, "glClearColor") \
  X(Clear, "glClear") \
  X(Viewport, "glViewport") \
  X(Enable, "glEnable") \
  X(Disable, "glDisable") \
  X(Scissor, "glScissor") \
  X(BlendFuncSeparate, "glBlendFuncSeparate") \
  X(BlendEquation, "glBlendEquation") \
  X(GetError, "glGetError") \
  X(PixelStorei, "glPixelStorei") \
  X(ReadPixels, "glReadPixels") \
  X(GenTextures, "glGenTextures") \
  X(DeleteTextures, "glDeleteTextures") \
  X(BindTexture, "glBindTexture") \
  X(TexParameteri, "glTexParameteri") \
  X(TexImage2D, "glTexImage2D") \
  X(ActiveTexture, "glActiveTexture") \
  X(GenVertexArrays, "glGenVertexArrays") \
  X(BindVertexArray, "glBindVertexArray") \
  X(GenBuffers, "glGenBuffers") \
  X(BindBuffer, "glBindBuffer") \
  X(BufferData, "glBufferData") \
  X(EnableVertexAttribArray, "glEnableVertexAttribArray") \
  X(VertexAttribPointer, "glVertexAttribPointer") \
  X(DrawArrays, "glDrawArrays") \
  X(CreateShader, "glCreateShader") \
  X(ShaderSource, "glShaderSource") \
  X(CompileShader, "glCompileShader") \
  X(GetShaderiv, "glGetShaderiv") \
  X(GetShaderInfoLog, "glGetShaderInfoLog") \
  X(DeleteShader, "glDeleteShader") \
  X(CreateProgram, "glCreateProgram") \
  X(AttachShader, "glAttachShader") \
  X(LinkProgram, "glLinkProgram") \
  X(GetProgramiv, "glGetProgramiv") \
  X(GetProgramInfoLog, "glGetProgramInfoLog") \
  X(UseProgram, "glUseProgram") \
  X(GetUniformLocation, "glGetUniformLocation") \
  X(Uniform1i, "glUniform1i") \
  X(Uniform1f, "glUniform1f") \
  X(Uniform4f, "glUniform4f") \
  X(GenFramebuffers, "glGenFramebuffers") \
  X(BindFramebuffer, "glBindFramebuffer") \
  X(FramebufferTexture2D, "glFramebufferTexture2D") \
  X(CheckFramebufferStatus, "glCheckFramebufferStatus") \
  X(DeleteFramebuffers, "glDeleteFramebuffers")
    bool IsComplete() const {
      return Clear != nullptr && GenTextures != nullptr && CreateProgram != nullptr &&
             GenVertexArrays != nullptr && BindVertexArray != nullptr &&
             DrawArrays != nullptr && GetError != nullptr && GenFramebuffers != nullptr;
    }
  };
} // namespace krkr::render
