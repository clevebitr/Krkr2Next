/**
 * @file krkr_egl_context.cpp
 * @brief Headless EGL context manager using native EGL/GLES.
 */

#include "krkr_egl_context.h"
#include "krkr_gl.h"

#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <spdlog/spdlog.h>

#if defined(__ANDROID__)
#include <EGL/eglext.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#define EGL_LOGI(...)                                                          \
    __android_log_print(ANDROID_LOG_INFO, "krkr2-egl", __VA_ARGS__)
#define EGL_LOGE(...)                                                          \
    __android_log_print(ANDROID_LOG_ERROR, "krkr2-egl", __VA_ARGS__)
#else
#define EGL_LOGI(...) ((void)0)
#define EGL_LOGE(...) ((void)0)
#endif // __ANDROID__

namespace krkr {

    // ---------------------------------------------------------------------------
    // EGLContextManager
    // ---------------------------------------------------------------------------

    EGLContextManager::~EGLContextManager() { Destroy(); }

    // ---------------------------------------------------------------------------
    // AcquireDisplay — 原生 EGL display 获取
    //
    // 直接使用平台自带的 EGL/GLES 驱动，不再经过 ANGLE 翻译层。
    // eglGetDisplay(EGL_DEFAULT_DISPLAY) 是 EGL 1.0
    // 起就有的接口，无需任何扩展， 在 Android 7.0+ (API 24) 全平台可用。
    // ---------------------------------------------------------------------------

    EGLDisplay EGLContextManager::AcquireDisplay() {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGL_LOGI(
            "AcquireDisplay: eglGetDisplay(EGL_DEFAULT_DISPLAY) returned %p",
            display);
        return display;
    }

    // ---------------------------------------------------------------------------
    // Initialize (Pbuffer mode)
    // ---------------------------------------------------------------------------

    bool EGLContextManager::Initialize(uint32_t width, uint32_t height) {
        if(context_ != EGL_NO_CONTEXT) {
            spdlog::warn("EGLContextManager::Initialize called but context "
                         "already exists, destroying first");
            Destroy();
        }

        display_ = AcquireDisplay();
        if(display_ == EGL_NO_DISPLAY) {
            EGL_LOGE("eglGetDisplay failed: 0x%x", eglGetError());
            spdlog::error("eglGetDisplay failed: 0x{:x}", eglGetError());
            return false;
        }

        EGLint majorVersion, minorVersion;
        if(!eglInitialize(display_, &majorVersion, &minorVersion)) {
            EGL_LOGE("eglInitialize failed: 0x%x", eglGetError());
            spdlog::error("eglInitialize failed: 0x{:x}", eglGetError());
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("EGL initialized: version %d.%d", majorVersion, minorVersion);
        spdlog::info("EGL initialized: version {}.{}", majorVersion,
                     minorVersion);
        spdlog::info("EGL vendor: {}", eglQueryString(display_, EGL_VENDOR));
        spdlog::info("EGL version string: {}",
                     eglQueryString(display_, EGL_VERSION));

        // 选择支持 Pbuffer 的 config；Android 上还需要 EGL_WINDOW_BIT 以支持
        // SurfaceTexture 渲染。请求 ES3 可渲染类型：minSdk 24 起 GLES 3.0 是
        // 强制要求（Android 4.3/API 18+），因此这里不需要 ES2 回退分支。
        EGLint configAttribs[] = {
#if defined(__ANDROID__)
            EGL_SURFACE_TYPE,
            EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
#else
            EGL_SURFACE_TYPE,
            EGL_PBUFFER_BIT,
#endif
            EGL_RED_SIZE,
            8,
            EGL_GREEN_SIZE,
            8,
            EGL_BLUE_SIZE,
            8,
            EGL_ALPHA_SIZE,
            8,
            EGL_DEPTH_SIZE,
            0,
            EGL_STENCIL_SIZE,
            8,
            EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES3_BIT,
            EGL_NONE
        };

        EGLint numConfigs = 0;
        if(!eglChooseConfig(display_, configAttribs, &config_, 1,
                            &numConfigs) ||
           numConfigs == 0) {
            EGL_LOGE("eglChooseConfig failed: 0x%x, numConfigs=%d",
                     eglGetError(), numConfigs);
            spdlog::error("eglChooseConfig failed: 0x{:x}, numConfigs={}",
                          eglGetError(), numConfigs);
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("eglChooseConfig OK: numConfigs=%d", numConfigs);

        // Create the Pbuffer surface
        if(!CreateSurface(width, height)) {
            EGL_LOGE("CreateSurface(Pbuffer) failed for %ux%u", width, height);
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("Pbuffer surface created: %ux%u", width, height);

        // Create GLES3 context. GLES 3.0 上下文向后兼容 GLSL ES 1.00 shader，
        // 因此现有 shader 无需改写即可运行。
        EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        context_ =
            eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
        if(context_ == EGL_NO_CONTEXT) {
            EGL_LOGE("eglCreateContext failed: 0x%x", eglGetError());
            spdlog::error("eglCreateContext failed: 0x{:x}", eglGetError());
            DestroySurface();
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("eglCreateContext OK");

        // Make current
        if(!MakeCurrent()) {
            EGL_LOGE("Initial MakeCurrent failed");
            spdlog::error("Initial MakeCurrent failed");
            Destroy();
            return false;
        }
        EGL_LOGI("MakeCurrent OK");

        spdlog::info("EGL context created successfully: {}x{}", width, height);
        spdlog::default_logger()->flush();

        // glGetString may return nullptr if the context is not fully ready
        auto safeGlString = [](GLenum name) -> const char * {
            const char *s = reinterpret_cast<const char *>(glGetString(name));
            return s ? s : "(null)";
        };
        spdlog::info("GL_RENDERER: {}", safeGlString(GL_RENDERER));
        spdlog::info("GL_VERSION: {}", safeGlString(GL_VERSION));
        spdlog::default_logger()->flush();

        // Check for GL errors after context creation
        GLenum err = glGetError();
        if(err != GL_NO_ERROR) {
            spdlog::warn("GL error after context creation: 0x{:x}", err);
        }

        // Invalidate the GL state cache since we have a fresh context
        krkr::gl::InvalidateStateCache();

        return true;
    }

    void EGLContextManager::Destroy() {
        // Release render-target objects while the context is still valid, then
        // unbind and destroy the context itself. No GL handle survives this
        // point. 必须在 context 有效时释放渲染目标，再解除绑定并销毁
        // context；之后不得复用任何 GL 句柄。
        DestroyNativeWindowResources();
        if(display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);

            if(context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
                context_ = EGL_NO_CONTEXT;
            }

            DestroySurface();

            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
        }
        config_ = nullptr;
        width_ = 0;
        height_ = 0;
    }

    bool EGLContextManager::MakeCurrent() {
        if(display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT) {
            return false;
        }
        // Prefer WindowSurface if available, otherwise use Pbuffer
        EGLSurface target =
            (window_surface_ != EGL_NO_SURFACE) ? window_surface_ : surface_;
        if(target == EGL_NO_SURFACE) {
            return false;
        }
        if(!eglMakeCurrent(display_, target, target, context_)) {
            spdlog::error("eglMakeCurrent failed: 0x{:x}", eglGetError());
            return false;
        }
        return true;
    }

    bool EGLContextManager::ReleaseCurrent() {
        if(display_ == EGL_NO_DISPLAY)
            return false;
        return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                              EGL_NO_CONTEXT) == EGL_TRUE;
    }

    bool EGLContextManager::Resize(uint32_t width, uint32_t height) {
        if(width == width_ && height == height_) {
            return true; // No change needed
        }

        if(display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT) {
            spdlog::error("Cannot resize: EGL not initialized");
            return false;
        }

        // Release the current surface
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        DestroySurface();

        // Create new surface with updated size
        if(!CreateSurface(width, height)) {
            spdlog::error("Failed to create new surface during resize");
            return false;
        }

        // Re-make current
        if(!MakeCurrent()) {
            spdlog::error("MakeCurrent failed after resize");
            return false;
        }

        spdlog::info("EGL surface resized to {}x{}", width, height);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    bool EGLContextManager::CreateSurface(uint32_t width, uint32_t height) {
        EGLint pbufferAttribs[] = { EGL_WIDTH, static_cast<EGLint>(width),
                                    EGL_HEIGHT, static_cast<EGLint>(height),
                                    EGL_NONE };
        surface_ = eglCreatePbufferSurface(display_, config_, pbufferAttribs);
        if(surface_ == EGL_NO_SURFACE) {
            spdlog::error("eglCreatePbufferSurface failed: 0x{:x}",
                          eglGetError());
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    void EGLContextManager::DestroySurface() {
        if(display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
    }

    // ---------------------------------------------------------------------------
    // BindRenderTarget — 绑定当前渲染目标
    //
    // 本项目仅面向 Android：渲染目标要么是 EGL WindowSurface（SurfaceTexture
    // 零拷贝直出），要么是 Pbuffer（无窗口时的 Linux 宿主验证构建）。
    // 两者都用默认 FBO (0)，不存在独立的 FBO 渲染目标路径。
    // ---------------------------------------------------------------------------

    void EGLContextManager::BindRenderTarget() {
        if(native_window_ != nullptr && window_surface_ != EGL_NO_SURFACE) {
            // Android WindowSurface 模式：渲染到默认 FBO (0)
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, static_cast<GLsizei>(window_width_),
                       static_cast<GLsizei>(window_height_));
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, static_cast<GLsizei>(width_),
                       static_cast<GLsizei>(height_));
        }
    }

    // ---------------------------------------------------------------------------
    // Android: Initialize EGL directly with a WindowSurface (no Pbuffer)
    // ---------------------------------------------------------------------------

    bool EGLContextManager::InitializeWithWindow(void *window, uint32_t width,
                                                 uint32_t height) {
#if defined(__ANDROID__)
        if(!window || width == 0 || height == 0) {
            EGL_LOGE(
                "InitializeWithWindow: invalid parameters (window=%p, %ux%u)",
                window, width, height);
            return false;
        }

        if(context_ != EGL_NO_CONTEXT) {
            EGL_LOGI("InitializeWithWindow: context already exists, destroying "
                     "first");
            Destroy();
        }


        // 1. 获取原生 EGL display
        display_ = AcquireDisplay();
        if(display_ == EGL_NO_DISPLAY) {
            EGL_LOGE("InitializeWithWindow: AcquireDisplay failed: 0x%x",
                     eglGetError());
            return false;
        }

        // 2. Initialize EGL
        EGLint majorVersion, minorVersion;
        if(!eglInitialize(display_, &majorVersion, &minorVersion)) {
            EGL_LOGE("InitializeWithWindow: eglInitialize failed: 0x%x",
                     eglGetError());
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("InitializeWithWindow: EGL %d.%d vendor=%s", majorVersion,
                 minorVersion, eglQueryString(display_, EGL_VENDOR));

        // 3. Choose config — only need EGL_WINDOW_BIT (no Pbuffer)
        EGLint configAttribs[] = { EGL_SURFACE_TYPE,
                                   EGL_WINDOW_BIT,
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_BLUE_SIZE,
                                   8,
                                   EGL_ALPHA_SIZE,
                                   8,
                                   EGL_DEPTH_SIZE,
                                   0,
                                   EGL_STENCIL_SIZE,
                                   8,
                                   EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES3_BIT,
                                   EGL_NONE };

        EGLint numConfigs = 0;
        if(!eglChooseConfig(display_, configAttribs, &config_, 1,
                            &numConfigs) ||
           numConfigs == 0) {
            EGL_LOGE("InitializeWithWindow: eglChooseConfig failed: 0x%x "
                     "numConfigs=%d",
                     eglGetError(), numConfigs);
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("InitializeWithWindow: eglChooseConfig OK numConfigs=%d",
                 numConfigs);

        // 4. Create WindowSurface from ANativeWindow
        auto *nativeWindow = static_cast<ANativeWindow *>(window);
        ANativeWindow_acquire(nativeWindow);

        EGLint surfAttribs[] = { EGL_NONE };
        window_surface_ = eglCreateWindowSurface(display_, config_,
                                                 nativeWindow, surfAttribs);
        if(window_surface_ == EGL_NO_SURFACE) {
            EGL_LOGE(
                "InitializeWithWindow: eglCreateWindowSurface failed: 0x%x",
                eglGetError());
            ANativeWindow_release(nativeWindow);
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("InitializeWithWindow: WindowSurface created %ux%u", width,
                 height);

        // 5. Create GLES3 context
        EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        context_ =
            eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
        if(context_ == EGL_NO_CONTEXT) {
            EGL_LOGE("InitializeWithWindow: eglCreateContext failed: 0x%x",
                     eglGetError());
            eglDestroySurface(display_, window_surface_);
            window_surface_ = EGL_NO_SURFACE;
            ANativeWindow_release(nativeWindow);
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("InitializeWithWindow: context created");

        // 6. Make current with WindowSurface
        if(!eglMakeCurrent(display_, window_surface_, window_surface_,
                           context_)) {
            EGL_LOGE("InitializeWithWindow: eglMakeCurrent failed: 0x%x",
                     eglGetError());
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
            eglDestroySurface(display_, window_surface_);
            window_surface_ = EGL_NO_SURFACE;
            ANativeWindow_release(nativeWindow);
            eglTerminate(display_);
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        EGL_LOGI("InitializeWithWindow: MakeCurrent OK");

        // Disable VSync wait — the host shell already controls frame pacing via
        // its own Choreographer / VSync signal.  Without this, eglSwapBuffers
        // blocks for one VSync period which desynchronises from the host's tick
        // and causes visible flicker.
        eglSwapInterval(display_, 0);

        // Store state — no Pbuffer surface_ is created; window_surface_ is
        // primary
        native_window_ = nativeWindow;
        window_width_ = width;
        window_height_ = height;
        width_ = width;
        height_ = height;
        // surface_ remains EGL_NO_SURFACE (no Pbuffer fallback)

        // Log GL info
        const char *renderer =
            reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const char *version =
            reinterpret_cast<const char *>(glGetString(GL_VERSION));
        EGL_LOGI("InitializeWithWindow: GL_RENDERER=%s GL_VERSION=%s",
                 renderer ? renderer : "(null)", version ? version : "(null)");

        // Invalidate GL state cache
        krkr::gl::InvalidateStateCache();

        EGL_LOGI("InitializeWithWindow: success %ux%u", width, height);
        return true;
#else
        (void)window;
        (void)width;
        (void)height;
        spdlog::error("InitializeWithWindow: not supported on this platform");
        return false;
#endif
    }

    // ---------------------------------------------------------------------------
    // Android WindowSurface attachment (SurfaceTexture zero-copy rendering)
    // ---------------------------------------------------------------------------

    bool EGLContextManager::AttachNativeWindow(void *window, uint32_t width,
                                               uint32_t height) {
#if defined(__ANDROID__)
        if(!window || width == 0 || height == 0) {
            spdlog::error(
                "AttachNativeWindow: invalid parameters (window={}, {}x{})",
                window != nullptr, width, height);
            return false;
        }
        if(context_ == EGL_NO_CONTEXT) {
            spdlog::error("AttachNativeWindow: EGL context not initialized");
            return false;
        }

        // Replace the previous window target before acquiring the new
        // reference. The manager releases its retained ANativeWindow reference
        // during detach. 替换旧窗口目标后再获取新引用；detach
        // 时由管理器释放自己持有的 ANativeWindow 引用。
        DestroyNativeWindowResources();

        auto *nativeWindow = static_cast<ANativeWindow *>(window);
        ANativeWindow_acquire(nativeWindow);

        // Create EGL WindowSurface from ANativeWindow
        EGLint attribs[] = { EGL_NONE };
        window_surface_ =
            eglCreateWindowSurface(display_, config_, nativeWindow, attribs);
        if(window_surface_ == EGL_NO_SURFACE) {
            spdlog::error(
                "AttachNativeWindow: eglCreateWindowSurface failed: 0x{:x}",
                eglGetError());
            ANativeWindow_release(nativeWindow);
            return false;
        }

        // Switch context to WindowSurface
        if(!eglMakeCurrent(display_, window_surface_, window_surface_,
                           context_)) {
            spdlog::error("AttachNativeWindow: eglMakeCurrent(WindowSurface) "
                          "failed: 0x{:x}",
                          eglGetError());
            eglDestroySurface(display_, window_surface_);
            window_surface_ = EGL_NO_SURFACE;
            ANativeWindow_release(nativeWindow);
            return false;
        }

        // Disable VSync wait — the host shell controls frame pacing
        eglSwapInterval(display_, 0);

        native_window_ = nativeWindow;
        window_width_ = width;
        window_height_ = height;

        spdlog::info("AttachNativeWindow: success {}x{}", width, height);
        return true;
#else
        (void)window;
        (void)width;
        (void)height;
        spdlog::error("AttachNativeWindow: not supported on this platform");
        return false;
#endif
    }

    void EGLContextManager::DetachNativeWindow() {
        DestroyNativeWindowResources();
        spdlog::info("DetachNativeWindow: reverted to Pbuffer mode");
    }

    void EGLContextManager::DestroyNativeWindowResources() {
#if defined(__ANDROID__)
        if(native_window_) {
            // Revert to Pbuffer surface if available
            if(surface_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY &&
               context_ != EGL_NO_CONTEXT) {
                eglMakeCurrent(display_, surface_, surface_, context_);
            } else if(display_ != EGL_NO_DISPLAY) {
                // No Pbuffer — just unbind
                eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                               EGL_NO_CONTEXT);
            }
            if(window_surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(display_, window_surface_);
                window_surface_ = EGL_NO_SURFACE;
            }
            ANativeWindow_release(static_cast<ANativeWindow *>(native_window_));
            native_window_ = nullptr;
            window_width_ = 0;
            window_height_ = 0;
        }
#endif
    }

    // ---------------------------------------------------------------------------
    // Global singleton
    // ---------------------------------------------------------------------------

    EGLContextManager &GetEngineEGLContext() {
        static EGLContextManager instance;
        return instance;
    }

} // namespace krkr
