#pragma once

#include <cstddef>
#include <memory>

namespace krkr::visual {
  class Scene;
}

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 Window 类。
  //
  // 窗口是图层树的根：脚本创建 Window 即建立一棵场景树（krkr::visual::Scene），
  // 之后 Layer 挂到它下面。合成与呈现不在本层，见 docs/render-spec.md §14。
  void RegisterWindowClass(const ScriptContext &context);

  // 主窗口（本会话第一个创建的窗口）的场景。宿主用它取一帧的图层树。
  // 尚未创建窗口时为空。
  std::shared_ptr<krkr::visual::Scene> GetPrimaryScene();

  // 运行时重启时清空登记表。脚本侧仍持有引用的场景不受影响，只是不再被当作主场景。
  void ResetWindowRegistry();

  std::size_t GetWindowCount();
} // namespace krkr::script
