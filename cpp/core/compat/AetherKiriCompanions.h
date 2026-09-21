//
// AetherKiri 层的伴生脚本（虚拟文件提供者）。
//
// AetherKiri 让 GPU / E-mote 系游戏能跑，靠的是在存储层"虚拟提供"一批引擎侧脚本：
//   - GPU 伴生脚本（`gpulayer.tjs` / `gpuaffinelayer.tjs` / `d3d*.tjs` / `live2d.tjs` …）
//     → 内嵌的 KAGWindow 接管占位脚本（对齐上游 `impl/GpuCompatScript.h`）；
//   - `motion_<asset>.psb.tjs` / `.mtn.tjs` → `%["storage" => "<asset>.psb"]`，
//     让 `.PSB` 路由到 `MotionAffineSourceLayer` → `Motion.EmotePlayer`
//     （这是 E-mote 动作解析的关键；缺它时 KAG 的 `checkAnimImageData()` 会把
//     文件名抹掉，动作整段解析不出来）；
//   - `[dx_]<name>emo.{psb,mtn,mt}` → 空流（split-emote 虚拟存储）。
//
// 本仓库没有这套机制时，游戏脚本 `Scripts.evalStorage("live2d.tjs")` 会直接抛
// "找不到存储"，GPU/E-mote 初始化整段失败。
//
// **只在 AetherKiri 层生效**：provider 内部检查 `ActiveLayer()`；缺省（classic）层
// 行为完全不变。
//
#pragma once

namespace krkr::compat {

    // 注册/注销 AetherKiri 伴生脚本 provider（幂等，可在静态初始化期调用）。
    void RegisterAetherKiriCompanions();
    void UnregisterAetherKiriCompanions();

} // namespace krkr::compat
