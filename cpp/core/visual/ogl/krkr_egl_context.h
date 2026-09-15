/**
 * @file krkr_egl_context.h
 * @brief Headless EGL context manager using native EGL/GLES.
 *
 * 用离屏 EGL Pbuffer 或 Android ANativeWindow Surface 承载 GLES 上下文，
 * 不依赖窗口系统。Android 走平台自带的 EGL/GLES 驱动（不经 ANGLE 翻译层）：
 *  - 有窗口（SurfaceTexture 零拷贝）时用 WindowSurface，eglSwapBuffers 直出；
 *  - 无窗口（Linux 宿主验证构建）时用 Pbuffer。
 * 上下文请求 GLES 3.0（minSdk 24 起强制可用），GLSL ES 1.00 shader
 * 仍可直接运行。
 */
#pragma once

#include <cstdint>

#define KRKR_HAS_EGL 1
#include <EGL/egl.h>

namespace krkr {

    class EGLContextManager {
    public:
        EGLContextManager() = default;
        ~EGLContextManager();

        // Non-copyable
        EGLContextManager(const EGLContextManager &) = delete;
        EGLContextManager &operator=(const EGLContextManager &) = delete;

        /**
         * Initialize the EGL display, create a Pbuffer surface and
         * a GLES 3.0 context.  Makes the context current.
         *
         * @param width   Initial surface width in pixels
         * @param height  Initial surface height in pixels
         * @return true on success
         */
        bool Initialize(uint32_t width, uint32_t height);

        /**
         * Destroy the EGL context, surface, and display.
         */
        void Destroy();

        /**
         * Make this context current on the calling thread.
         */
        bool MakeCurrent();

        /**
         * Release the context from the calling thread.
         */
        bool ReleaseCurrent();

        /**
         * Resize the Pbuffer surface. This destroys the old surface
         * and creates a new one with the requested dimensions.
         * The context is re-made current after resize.
         *
         * @param width   New width in pixels
         * @param height  New height in pixels
         * @return true on success
         */
        bool Resize(uint32_t width, uint32_t height);

        /**
         * IOSurface 渲染目标查询——本项目仅面向 Android，恒为未附着。
         * 保留这些访问器是因为 DrawDevice、textrender 与宿主 blit 层都有
         * `if (egl.HasIOSurface())` 形式的运行时分支；恒 false 使其走 Android
         * 路径。
         */
        bool HasIOSurface() const { return false; }
        uint32_t GetIOSurfaceWidth() const { return 0; }
        uint32_t GetIOSurfaceHeight() const { return 0; }

        /**
         * Bind the default framebuffer (0) and set the viewport to the
         * current render target size. Call this before rendering a frame.
         */
        void BindRenderTarget();

        /**
         * Initialize EGL directly with an ANativeWindow (Android only).
         * Unlike Initialize() which creates a Pbuffer, this creates
         * a WindowSurface directly, avoiding Pbuffer-related failures
         * on some Android devices/drivers.
         *
         * After this call, IsValid()==true and HasNativeWindow()==true.
         *
         * @param window  ANativeWindow* from ANativeWindow_fromSurface()
         * @param width   Surface width in pixels
         * @param height  Surface height in pixels
         * @return true on success
         */
        bool InitializeWithWindow(void *window, uint32_t width,
                                  uint32_t height);

        /**
         * Attach an Android ANativeWindow as the render target.
         * Creates an EGL WindowSurface and binds it as the primary surface.
         * eglSwapBuffers() delivers frames directly to the SurfaceTexture.
         *
         * @param window  ANativeWindow* from ANativeWindow_fromSurface()
         * @param width   Surface width in pixels
         * @param height  Surface height in pixels
         * @return true on success
         */
        bool AttachNativeWindow(void *window, uint32_t width, uint32_t height);

        /**
         * Detach the ANativeWindow, reverting to Pbuffer mode.
         */
        void DetachNativeWindow();

        /**
         * @return true if rendering to an ANativeWindow (Android
         * SurfaceTexture).
         */
        bool HasNativeWindow() const { return native_window_ != nullptr; }

        /**
         * @return the EGL WindowSurface for eglSwapBuffers (Android).
         */
        EGLSurface GetWindowSurface() const { return window_surface_; }

        /**
         * @return the ANativeWindow width (0 if not attached).
         */
        uint32_t GetNativeWindowWidth() const { return window_width_; }

        /**
         * @return the ANativeWindow height (0 if not attached).
         */
        uint32_t GetNativeWindowHeight() const { return window_height_; }

        /**
         * Update stored native window dimensions after SurfaceTexture resize.
         * setDefaultBufferSize() changes the buffer dimensions; the EGL surface
         * auto-adapts on next eglSwapBuffers. We track the new size here so
         * UpdateDrawBuffer() calculates the correct letterbox viewport.
         */
        void UpdateNativeWindowSize(uint32_t w, uint32_t h) {
            window_width_ = w;
            window_height_ = h;
        }

        /**
         * Mark the current frame as dirty (new content rendered).
         * Must be called after UpdateDrawBuffer() completes rendering.
         */
        void MarkFrameDirty() { frame_dirty_ = true; }

        /**
         * Check and consume the dirty flag.
         * @return true if the frame was dirty (and flag is now cleared).
         */
        bool ConsumeFrameDirty() {
            if(!frame_dirty_)
                return false;
            frame_dirty_ = false;
            return true;
        }

        /**
         * Peek at the dirty flag without consuming it.
         */
        bool IsFrameDirty() const { return frame_dirty_; }

        // Accessors
        uint32_t GetWidth() const { return width_; }
        uint32_t GetHeight() const { return height_; }
        bool IsValid() const { return context_ != EGL_NO_CONTEXT; }

        EGLDisplay GetDisplay() const { return display_; }
        EGLSurface GetSurface() const { return surface_; }
        EGLContext GetContext() const { return context_; }

    private:
        bool CreateSurface(uint32_t width, uint32_t height);
        void DestroySurface();
        void DestroyNativeWindowResources();

        /**
         * 获取原生 EGL display（eglGetDisplay(EGL_DEFAULT_DISPLAY)）。
         * 不再经过 ANGLE 的 EGL_PLATFORM_ANGLE_* 路径。
         */
        EGLDisplay AcquireDisplay();

        EGLDisplay display_ = EGL_NO_DISPLAY;
        EGLSurface surface_ = EGL_NO_SURFACE;
        EGLContext context_ = EGL_NO_CONTEXT;
        EGLConfig config_ = nullptr;
        uint32_t width_ = 0;
        uint32_t height_ = 0;

        // Android WindowSurface resources (SurfaceTexture zero-copy rendering)
        void *native_window_ = nullptr; // ANativeWindow*
        EGLSurface window_surface_ = EGL_NO_SURFACE;
        uint32_t window_width_ = 0;
        uint32_t window_height_ = 0;

        // Frame dirty flag — prevents eglSwapBuffers on frames where
        // UpdateDrawBuffer() was not called, avoiding double-buffer
        // flicker (alternating between current and stale back-buffer).
        bool frame_dirty_ = false;
    };

    /**
     * Get the global engine EGL context singleton.
     * Initialized once during engine bootstrap.
     */
    EGLContextManager &GetEngineEGLContext();

} // namespace krkr
