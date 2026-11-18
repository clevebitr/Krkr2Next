#include "platform/desktop/gl/GLContext.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

// WGL 扩展入口点（不在 opengl32 的导出表里，需要 wglGetProcAddress）。
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

namespace krkr::platform {

namespace {

typedef HGLRC(WINAPI *PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int *);
typedef BOOL(WINAPI *PFNWGLSWAPINTERVALEXT)(int);

struct WglWindow {
  HWND window = nullptr;
  HDC deviceContext = nullptr;
  HGLRC context = nullptr;
};

LRESULT CALLBACK HiddenWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcA(window, message, wparam, lparam);
}

} // namespace

class GLContext::Impl {
  public:
    WglWindow wgl;
    void *openglLibrary = nullptr;
    bool valid = false;
    std::string error;
};

GLContext::GLContext(int width, int height) : impl_(new Impl()), width_(width), height_(height) {
  HINSTANCE instance = GetModuleHandleA(nullptr);
  const char *class_name = "KrkrNextHiddenGLWindow";

  WNDCLASSA window_class = {};
  window_class.style = CS_OWNDC;
  window_class.lpfnWndProc = &HiddenWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  if (RegisterClassA(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    impl_->error = "RegisterClass failed";
    return;
  }

  impl_->wgl.window = CreateWindowExA(0, class_name, "krkr_gl", WS_OVERLAPPEDWINDOW,
                                      CW_USEDEFAULT, CW_USEDEFAULT, width_, height_, nullptr,
                                      nullptr, instance, nullptr);
  if (impl_->wgl.window == nullptr) {
    impl_->error = "CreateWindow failed";
    return;
  }
  impl_->wgl.deviceContext = GetDC(impl_->wgl.window);

  PIXELFORMATDESCRIPTOR format = {};
  format.nSize = sizeof(format);
  format.nVersion = 1;
  format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  format.iPixelType = PFD_TYPE_RGBA;
  format.cColorBits = 32;
  format.cAlphaBits = 8;
  format.iLayerType = PFD_MAIN_PLANE;

  const int pixel_format = ChoosePixelFormat(impl_->wgl.deviceContext, &format);
  if (pixel_format == 0 ||
      !SetPixelFormat(impl_->wgl.deviceContext, pixel_format, &format)) {
    impl_->error = "SetPixelFormat failed";
    return;
  }

  // 先用兼容上下文把 wglCreateContextAttribsARB 取到，再建 3.3 core。
  HGLRC legacy = wglCreateContext(impl_->wgl.deviceContext);
  if (legacy == nullptr) {
    impl_->error = "wglCreateContext failed";
    return;
  }
  wglMakeCurrent(impl_->wgl.deviceContext, legacy);
  auto create_attribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARB>(
      wglGetProcAddress("wglCreateContextAttribsARB"));

  if (create_attribs != nullptr) {
    const int attributes[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                              WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                              WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                              0};
    impl_->wgl.context = create_attribs(impl_->wgl.deviceContext, nullptr, attributes);
  }
  if (impl_->wgl.context == nullptr) {
    impl_->wgl.context = legacy;
    impl_->error = "falling back to the legacy context (no 3.3 core)";
  } else {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    wglMakeCurrent(impl_->wgl.deviceContext, impl_->wgl.context);
  }

  impl_->openglLibrary = LoadLibraryA("opengl32.dll");
  if (impl_->openglLibrary == nullptr) {
    impl_->error = "cannot load opengl32.dll";
    return;
  }

  if (auto swap_interval = reinterpret_cast<PFNWGLSWAPINTERVALEXT>(
          wglGetProcAddress("wglSwapIntervalEXT"));
      swap_interval != nullptr) {
    swap_interval(0);
  }

  impl_->valid = impl_->wgl.context != nullptr;
}

GLContext::~GLContext() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->wgl.context != nullptr) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(impl_->wgl.context);
  }
  if (impl_->wgl.deviceContext != nullptr && impl_->wgl.window != nullptr) {
    ReleaseDC(impl_->wgl.window, impl_->wgl.deviceContext);
  }
  if (impl_->wgl.window != nullptr) {
    DestroyWindow(impl_->wgl.window);
  }
  if (impl_->openglLibrary != nullptr) {
    FreeLibrary(static_cast<HMODULE>(impl_->openglLibrary));
  }
  delete impl_;
  impl_ = nullptr;
}

bool GLContext::IsValid() const { return impl_ != nullptr && impl_->valid; }

bool GLContext::MakeCurrent() {
  return impl_ != nullptr && impl_->wgl.context != nullptr &&
         wglMakeCurrent(impl_->wgl.deviceContext, impl_->wgl.context) != FALSE;
}

bool GLContext::LoadApi(krkr::render::GLApi &api) {
  if (!IsValid()) {
    lastError_ = "context is not valid";
    return false;
  }

  const auto load = [this](const char *name) -> void * {
    void *address = reinterpret_cast<void *>(wglGetProcAddress(name));
    if (address == nullptr) {
      address = reinterpret_cast<void *>(
          GetProcAddress(static_cast<HMODULE>(impl_->openglLibrary), name));
    }
    return address;
  };

  // 入口点清单在 core/contracts/GLApi.h 的 KRKR_GL_FUNCTION_LIST 里，
  // 桌面与 Android 的 loader 展开同一份清单，避免两边漏项。
#define KRKR_GL_POINT(field, name)   api.field = reinterpret_cast<decltype(api.field)>(load(name));
  KRKR_GL_FUNCTION_LIST(KRKR_GL_POINT)
#undef KRKR_GL_POINT

  if (!api.IsComplete()) {
    lastError_ = "required GL entry points are missing";
    return false;
  }
  return true;
}

bool GLContext::SwapBuffers() {
  return impl_ != nullptr && ::SwapBuffers(impl_->wgl.deviceContext) != FALSE;
}

} // namespace krkr::platform
