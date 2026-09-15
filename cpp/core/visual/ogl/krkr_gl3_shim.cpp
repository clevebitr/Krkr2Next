/**
 * @file krkr_gl3_shim.cpp
 * @brief 运行期解析 Android 上无法链接的 GLES3 入口点。
 *
 * Android NDK 的 libGLESv2.so 存根只导出 **ES 2.0** 入口点。已用 llvm-nm
 * 直接核对 NDK 27 的 sysroot/usr/lib/aarch64-linux-android/24/libGLESv2.so：
 *   glGetString            导出
 *   glGetStringi           **未导出**
 *   glBlitFramebuffer      **未导出**
 *   glMapBufferRange       **未导出**
 *   glUnmapBuffer          **未导出**
 * 后四个都是 ES 3.0 core，而代码里在直接调用——它们在链接期表现为 undefined
 * symbol，ld.lld 只能给出毫无用处的 "did you mean: glGetString"。
 *
 * 去 ANGLE 之前不成问题——ANGLE 那份 libGLESv2 把 ES3 入口也导出了，一直替我们
 * 兜着底；换回平台原生 EGL/GLES 后这层兜底消失，链接就断了。这也解释了为什么
 * 问题是在去 ANGLE 之后才出现的。
 *
 * 官方对"存根不保证的入口点"给的做法就是运行期取：设备上真正的 libGLESv2 从
 * API 18 起就导出了这些函数，eglGetProcAddress 一律能取到。这里按同名符号给出
 * 转发实现，调用点无需改动。
 *
 * 若将来某个 NDK 版本开始在存根里导出这些符号，本文件也不冲突：同名的定义在
 * 我们自己的 .so 里优先绑定，行为与直接调用一致（仍是转发到同一份实现）。
 * 对应地，这个文件只在 Android 下编译；桌面构建的 libGLESv2 本来就导出它们。
 */

#if defined(__ANDROID__)

#include <EGL/egl.h>
// 用 gl3.h 而非 gl2.h：下面要**定义**同名函数，必须让声明与定义一致
// （GL_APICALL/GL_APIENTRY 的修饰由头文件给出）。
#include <GLES3/gl3.h>

#include <spdlog/spdlog.h>

namespace {

    /** 取一次入口点并缓存（函数内 static，C++11 起初始化线程安全）。 */
    template <typename Fn>
    Fn ResolveEntry(const char *name) {
        auto fn = reinterpret_cast<Fn>(eglGetProcAddress(name));
        if(fn == nullptr) {
            // 走到这里说明设备连 ES3 入口都没有，依赖它的功能会静默失效，
            // 必须留下痕迹——否则表现成"扩展列表为空/图传不上去"这类无头案。
            spdlog::error("krkr_gl3_shim: eglGetProcAddress(\"{}\") 返回空，"
                          "该设备缺少这个 GLES3 入口点",
                          name);
        }
        return fn;
    }

} // namespace

extern "C" {

const GLubyte *glGetStringi(GLenum name, GLuint index) {
    using Fn = const GLubyte *(*)(GLenum, GLuint);
    static Fn fn = ResolveEntry<Fn>("glGetStringi");
    return fn != nullptr ? fn(name, index) : nullptr;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    using Fn = void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                        GLbitfield, GLenum);
    static Fn fn = ResolveEntry<Fn>("glBlitFramebuffer");
    if(fn != nullptr)
        fn(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask,
           filter);
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                       GLbitfield access) {
    using Fn = void *(*)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
    static Fn fn = ResolveEntry<Fn>("glMapBufferRange");
    return fn != nullptr ? fn(target, offset, length, access) : nullptr;
}

GLboolean glUnmapBuffer(GLenum target) {
    using Fn = GLboolean (*)(GLenum);
    static Fn fn = ResolveEntry<Fn>("glUnmapBuffer");
    return fn != nullptr ? fn(target) : GL_FALSE;
}

} // extern "C"

#else

// 桌面构建的 libGLESv2（GLVND/Mesa）直接导出这些入口点，不需要垫片。
// 这里放一个 typedef 而不是空文件：空翻译单元在部分工具链下会告警。
using krkr_gl3_shim_not_needed_on_this_platform = void;

#endif // __ANDROID__
