# PLAN

> 由 `plan` 工具维护，请勿直接编辑。步骤级进度以 `todo` 工具为准；决策与踩坑写入 DECISIONS.md。

<!-- plan-bridge:state -->
current: M1
auto-breakpoint: false
<!-- /plan-bridge:state -->

## Milestones

- [ ] M1 — 渲染：像素语义交由上游、合成改用 legacy 图层模型
- [ ] M2 — 输入：指针、按键、文本转脚本事件
- [ ] M3 — 音频：WAVE 与 OGG 解码、混音、回调
- [ ] M4 — 切换 ABI 实现并删除冻结的旧引擎

## Current: M1 — 渲染：像素语义交由上游、合成改用 legacy 图层模型

goal: 像素语义由上游实现、合成改用 legacy 图层模型
breakpoint: R1-2a：先实现 thread_impl.h（见 todo.md §4）
