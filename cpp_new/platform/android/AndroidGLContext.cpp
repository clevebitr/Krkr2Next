#include "platform/android/AndroidGLContext.h"

#include <dlfcn.h>

#include <cstdio>

#include <EGL/egl.h>
#include <android/native_window.h>

namespace krkr::platform {

namespace {
constexpr int kOpenGLES3Bit = 0x40; // EGL_OPENGL_ES3_BIT
} // namespace

class AndroidGLContext::Impl {
  public:
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    ANativeWindow *window = nullptr;
    bool valid = false;
};

AndroidGLContext::AndroidGLContext(void *nativeWindow, int width, int height)
    : impl_(new Impl()), width_(width), height_(height) {
  impl_->window = static_cast<ANativeWindow *>(nativeWindow);
  if (impl_->window == nullptr) {
    lastError_ = "native window is null";
    return;
  }

  impl_->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (impl_->display == EGL_NO_DISPLAY || eglInitialize(impl_->display, nullptr, nullptr) == EGL_FALSE) {
    lastError_ = "eglInitialize failed";
    return;
  }
  if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    lastError_ = "eglBindAPI(EGL_OPENGL_ES_API) failed";
    return;
  }

  const EGLint configAttributes[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, kOpenGLES3Bit,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 0,
    EGL_STENCIL_SIZE, 0,
    EGL_NONE,
  };
  EGLint configCount = 0;
  if (eglChooseConfig(impl_->display, configAttributes, &impl_->config, 1, &configCount) == EGL_FALSE ||
      configCount == 0) {
    lastError_ = "eglChooseConfig found no ES3 config";
    return;
  }

  ANativeWindow_setBuffersGeometry(impl_->window, width_, height_, WINDOW_FORMAT_RGBA_8888);

  const EGLint surfaceAttributes[] = {EGL_NONE};
  impl_->surface = eglCreateWindowSurface(impl_->display, impl_->config, impl_->window,
                                          surfaceAttributes);
  if (impl_->surface == EGL_NO_SURFACE) {
    lastError_ = "eglCreateWindowSurface failed";
    return;
  }

  const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  impl_->context = eglCreateContext(impl_->display, impl_->config, EGL_NO_CONTEXT,
                                    contextAttributes);
  if (impl_->context == EGL_NO_CONTEXT) {
    lastError_ = "eglCreateContext failed";
    return;
  }

  if (!MakeCurrent()) {
    lastError_ = "eglMakeCurrent failed";
    return;
  }

  impl_->valid = true;
}

AndroidGLContext::~AndroidGLContext() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (impl_->context != EGL_NO_CONTEXT) {
      eglDestroyContext(impl_->display, impl_->context);
    }
    if (impl_->surface != EGL_NO_SURFACE) {
      eglDestroySurface(impl_->display, impl_->surface);
    }
    eglTerminate(impl_->display);
  }
  delete impl_;
  impl_ = nullptr;
}

bool AndroidGLContext::IsValid() const { return impl_ != nullptr && impl_->valid; }

bool AndroidGLContext::MakeCurrent() {
  if (impl_ == nullptr || impl_->display == EGL_NO_DISPLAY) {
    return false;
  }
  return eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) == EGL_TRUE;
}

bool AndroidGLContext::SwapBuffers() {
  if (!IsValid()) {
    return false;
  }
  return eglSwapBuffers(impl_->display, impl_->surface) == EGL_TRUE;
}

bool AndroidGLContext::Resize(int width, int height) {
  if (impl_ == nullptr || impl_->window == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  width_ = width;
  height_ = height;
  ANativeWindow_setBuffersGeometry(impl_->window, width_, height_, WINDOW_FORMAT_RGBA_8888);

  if (impl_->surface != EGL_NO_SURFACE) {
    eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(impl_->display, impl_->surface);
  }
  const EGLint surfaceAttributes[] = {EGL_NONE};
  impl_->surface = eglCreateWindowSurface(impl_->display, impl_->config, impl_->window,
                                          surfaceAttributes);
  if (impl_->surface == EGL_NO_SURFACE) {
    lastError_ = "eglCreateWindowSurface failed after resize";
    impl_->valid = false;
    return false;
  }
  return MakeCurrent();
}

bool AndroidGLContext::LoadApi(krkr::render::GLApi &api) {
  if (!IsValid()) {
    lastError_ = "context is not valid";
    return false;
  }

  const auto load = [](const char *name) -> void * {
    void *address = reinterpret_cast<void *>(eglGetProcAddress(name));
    if (address == nullptr) {
      address = dlsym(RTLD_DEFAULT, name);
    }
    return address;
  };

#define KRKR_GL_POINT(field, name) \
  api.field = reinterpret_cast<decltype(api.field)>(load(name));
  KRKR_GL_FUNCTION_LIST(KRKR_GL_POINT)
#undef KRKR_GL_POINT

  if (!api.IsComplete()) {
    lastError_ = "required GL entry points are missing";
    return false;
  }
  return true;
}

} // namespace krkr::platform
