#include "platform/android/AndroidTarget.h"

#include "core/log/Logger.h"

namespace krkr::platform {

// 8c 之前 Android 没有可用的窗口表面：显式告知"未接 Surface"，
// 而不是返回成功让上面以为画面已经出去。
bool AndroidTarget::Present(const std::uint32_t *pixels, int width, int height, int stride) {
  (void)pixels;
  (void)width;
  (void)height;
  (void)stride;
  static bool warned = false;
  if (!warned) {
    warned = true;
    krkr::log::Warn("android: ANativeWindow surface is not attached yet (step 8c)");
  }
  return false;
}

} // namespace krkr::platform
