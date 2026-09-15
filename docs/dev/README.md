# 开发文档

KrKr2-Next-Compose 的架构、构建、源码和兼容性资料。内容以当前代码状态为准，历史排查过程不在这里保存。

> ⚠️ **部分文档仍继承自 PocketKrKr**。本项目从 PocketKrKr fork 而来，改动了壳
> （Flutter → Kotlin）、渲染后端（ANGLE → 原生 EGL/GLES3）和平台范围（→ 仅 Android）。
> 下列文档已按 KrKr2-Next-Compose 现状更新；**其余（`architecture.md`、`source-map.md`、
> `conventions.md`、`tech-stack.md`、`getting-started.md` 等）仍描述 Flutter + ANGLE
> 时代**，其中的引擎核心部分（`cpp/core`、`cpp/plugins`）依然适用，但凡是提到
> Flutter 壳、Dart 桥、ANGLE、iOS/macOS 的内容都已过时。阅读时以本页标注为准。

## 已按 KrKr2-Next-Compose 现状更新

- **[input-contract.md](input-contract.md)** — 引擎输入接口的唯一权威说明
  （VK 码、坐标空间、返回键序列）。改动输入相关代码前必读。
- **[build.md](build.md)** — 构建系统、工具链、产物、CI。
- **[upstream-references.md](upstream-references.md)** — 已删除的 Flutter 壳代码
  如何取回（保留完整 git 历史，基线标签 `pocketkrkr-base`）。

## 继承自 PocketKrKr（按上条注意事项阅读）

按建议阅读顺序：

1. [入门指南](getting-started.md)：项目与构建的快速了解。
2. [架构](architecture.md)：引擎、桥接和渲染数据流。**壳与渲染后端已变**。
3. [源码地图](source-map.md)：按目录定位模块职责。引擎部分适用；`apps/` 已删除。
4. [关键引用](key-references.md)：主要文件和 C ABI 符号。
5. [约定](conventions.md)：修改平台、生命周期、链接、Live2D 和 SIMD 代码前必读。
   其中 SIMD、Live2D 与部分生命周期约定仍然有效。
6. [技术栈](tech-stack.md)：**ANGLE 部分已过时**。
7. [兼容性](compatibility.md)：游戏兼容性回归流程。
8. [探针清单](probes.md)：`KRKR_RENDER_PROBE` 统一开关、探针类型、日志前缀和源码位置。
9. [渲染诊断](rendering-diagnosis.md)：排查黑屏、停帧和显示链路问题。
10. [性能优化](perf-optimization.md) / [优化路线](optimization-roadmap.md)。
11. [待办](todo.md)：当前未完成事项（继承自上游，未按本项目重排）。
12. [兼容性与参考资料](krkrz-compat.md)：外部实现与格式资料的统一入口。

## 项目边界

```text
app/                              Kotlin/Compose 宿主壳、渲染线程、输入转发、生命周期
bridge/engine_api/                稳定 C ABI、JNI 绑定、Surface 交接
cpp/core/                         TJS2、存储、归档、渲染、字体、音频、视频和主循环
cpp/plugins/                      PSB、PSD、motionplayer、LayerEx、Cubism 等插件
build.sh、scripts/                构建入口与校验脚本
CMakePresets.json、vcpkg/         预设与依赖配置
.github/workflows/                CI
```

## 平台形态

| 平台 | 引擎产物 | 图形路径 |
|---|---|---|
| Android | 自包含 `libengine_api.so`，打包进 APK | 原生 EGL + GLES3、SurfaceTexture 零拷贝 |
| Linux | 宿主验证构建，不提供应用包 | 原生 EGL + GLES3 Pbuffer |

iOS/macOS 已从本项目移除。

## 参考资料

外部实现、格式资料和行为对照入口统一收录在
[krkrz-compat.md](krkrz-compat.md)。参考资料只用于理解协议和行为，不代表
KrKr2Next 已具备对应能力，也不直接复制外部代码。
