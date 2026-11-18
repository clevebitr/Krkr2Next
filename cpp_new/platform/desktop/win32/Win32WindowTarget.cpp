#include "platform/desktop/win32/Win32WindowTarget.h"

#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace krkr::platform {

class Win32WindowTarget::Impl {
  public:
    HWND window = nullptr;
    HDC deviceContext = nullptr;
    int width = 0;
    int height = 0;
    bool quit = false;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
      switch (message) {
        case WM_CLOSE:
          DestroyWindow(window);
          return 0;
        case WM_DESTROY:
          PostQuitMessage(0);
          return 0;
        case WM_PAINT: {
          // 画面在 Present 里直接提交；这里只消化重绘请求，避免消息堆积。
          PAINTSTRUCT paint;
          BeginPaint(window, &paint);
          EndPaint(window, &paint);
          return 0;
        }
        case WM_ERASEBKGND:
          return 1;
        default:
          return DefWindowProcA(window, message, wparam, lparam);
      }
    }
};

Win32WindowTarget::Win32WindowTarget(const char *titleUtf8, int width, int height)
    : impl_(std::make_unique<Impl>()) {
  impl_->width = width > 0 ? width : 640;
  impl_->height = height > 0 ? height : 480;

  HINSTANCE instance = GetModuleHandleA(nullptr);
  const char *class_name = "KrkrNextRenderWindow";

  WNDCLASSA window_class = {};
  window_class.style = CS_OWNDC;
  window_class.lpfnWndProc = &Impl::WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  window_class.lpszClassName = class_name;
  if (RegisterClassA(&window_class) == 0) {
    const DWORD error = GetLastError();
    if (error != ERROR_CLASS_ALREADY_EXISTS) {
      std::fprintf(stderr, "RegisterClass failed: %lu\n",
                   static_cast<unsigned long>(error));
      return;
    }
  }

  RECT rect = {0, 0, impl_->width, impl_->height};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  impl_->window = CreateWindowExA(0, class_name, titleUtf8, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                  rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  if (impl_->window == nullptr) {
    std::fprintf(stderr, "CreateWindow failed: %lu\n",
                 static_cast<unsigned long>(GetLastError()));
    return;
  }
  impl_->deviceContext = GetDC(impl_->window);
  ShowWindow(impl_->window, SW_SHOW);
  UpdateWindow(impl_->window);
}

Win32WindowTarget::~Win32WindowTarget() {
  if (impl_ != nullptr && impl_->window != nullptr) {
    ReleaseDC(impl_->window, impl_->deviceContext);
    DestroyWindow(impl_->window);
  }
}

bool Win32WindowTarget::IsValid() const { return impl_ != nullptr && impl_->window != nullptr; }

int Win32WindowTarget::GetWidth() const { return impl_ != nullptr ? impl_->width : 0; }

int Win32WindowTarget::GetHeight() const { return impl_ != nullptr ? impl_->height : 0; }

bool Win32WindowTarget::Present(const std::uint32_t *pixels, int width, int height,
                               int stride) {
  if (!IsValid() || pixels == nullptr || width <= 0 || height <= 0 || stride < width) {
    return false;
  }

  // 32bpp BI_RGB + 负高度：像素按 0xAARRGGBB 的自上而下顺序，与 Surface 一致。
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  const int copied = StretchDIBits(impl_->deviceContext, 0, 0, width, height, 0, 0, width,
                                   height, pixels, &info, DIB_RGB_COLORS, SRCCOPY);
  return copied != GDI_ERROR;
}

bool Win32WindowTarget::PumpEvents() {
  if (!IsValid()) {
    return false;
  }
  MSG message = {};
  while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
    if (message.message == WM_QUIT) {
      impl_->quit = true;
    }
    TranslateMessage(&message);
    DispatchMessageA(&message);
  }
  return !impl_->quit;
}

} // namespace krkr::platform
