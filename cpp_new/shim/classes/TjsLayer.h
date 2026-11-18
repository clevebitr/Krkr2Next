#pragma once

namespace krkr::script {
  struct ScriptContext;

  // 注册 TJS 侧 Layer 类。
  //
  // 图层只往场景树里写参数（几何、可见性、混合类型、位图）；合成与呈现由
  // krkr::visual::Scene 负责。未实现的成员（滤镜、转场、字体绘制）不在本类里
  // 声明，脚本访问时得到"成员不存在"，而不是假成功。
  void RegisterLayerClass(const ScriptContext &context);
} // namespace krkr::script
