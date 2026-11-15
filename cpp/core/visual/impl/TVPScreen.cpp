#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"
#include "krkr_egl_context.h"

int Screen::GetWidth() { return 2048; }
int Screen::GetHeight() {
    // Use EGL surface dimensions if available, otherwise fallback to width
    auto &egl = krkr::GetEngineEGLContext();
    if(egl.IsValid()) {
        uint32_t w = egl.GetWidth();
        uint32_t h = egl.GetHeight();
        if(w > 0 && h > 0) {
            int baseW = GetWidth();
            return baseW * static_cast<int>(h) / static_cast<int>(w);
        }
    }
    return GetWidth();
}

int Screen::GetDesktopLeft() { return 0; }
int Screen::GetDesktopTop() { return 0; }
int Screen::GetDesktopWidth() { return GetWidth(); }
int Screen::GetDesktopHeight() { return GetHeight(); }
