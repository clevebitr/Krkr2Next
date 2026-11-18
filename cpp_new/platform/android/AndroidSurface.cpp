#include "platform/android/AndroidSurface.h"

#include <memory>
#include <mutex>

#include "core/log/Logger.h"
#include "platform/android/AndroidGLContext.h"

namespace krkr::platform {

namespace {

std::mutex &SurfaceMutex() {
  static std::mutex instance;
  return instance;
}

std::unique_ptr<AndroidGLContext> &CurrentContext() {
  static std::unique_ptr<AndroidGLContext> instance;
  return instance;
}

} // namespace

krkr::render::IGLContext *GetAndroidGLContext() {
  std::lock_guard<std::mutex> lock(SurfaceMutex());
  return CurrentContext().get();
}

} // namespace krkr::platform

extern "C" {

bool krkr_android_surface_attach(void *nativeWindow, int width, int height) {
  std::lock_guard<std::mutex> lock(krkr::platform::SurfaceMutex());
  auto &context = krkr::platform::CurrentContext();
  context.reset();
  auto created = std::make_unique<krkr::platform::AndroidGLContext>(nativeWindow, width, height);
  if (!created->IsValid()) {
    krkr::log::Error("android: surface attach failed: ", created->GetLastErrorText());
    return false;
  }
  context = std::move(created);
  krkr::log::Info("android: surface attached (", width, "x", height, ")");
  return true;
}

bool krkr_android_surface_resize(int width, int height) {
  std::lock_guard<std::mutex> lock(krkr::platform::SurfaceMutex());
  auto &context = krkr::platform::CurrentContext();
  if (context == nullptr) {
    krkr::log::Warn("android: resize before attach is ignored");
    return false;
  }
  if (!context->Resize(width, height)) {
    krkr::log::Error("android: surface resize failed: ", context->GetLastErrorText());
    return false;
  }
  krkr::log::Info("android: surface resized to ", width, "x", height);
  return true;
}

void krkr_android_surface_detach(void) {
  std::lock_guard<std::mutex> lock(krkr::platform::SurfaceMutex());
  krkr::platform::CurrentContext().reset();
  krkr::log::Info("android: surface detached");
}

bool krkr_android_surface_valid(void) {
  std::lock_guard<std::mutex> lock(krkr::platform::SurfaceMutex());
  const auto &context = krkr::platform::CurrentContext();
  return context != nullptr && context->IsValid();
}

} // extern "C"
