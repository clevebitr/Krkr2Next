# `cpp/` —— 已冻结（2025-11）

本目录是 KrKr2-Next 的**旧引擎核心**（Kirikiroid2 血脉），**已被 `cpp_new/` 取代**。

## 状态

| 项 | 值 |
|---|---|
| 生命周期 | **冻结**：不再新增功能、不再修非阻塞缺陷、不再接受重构 |
| 仍被使用 | 是。根 `CMakeLists.txt` 仍从这里构建 `libengine_api.so`（`cpp/external` → `cpp/core` → `cpp/plugins` → `bridge/engine_api`），`app/` 依赖它 |
| 何时删除 | 第 11 步「用 `cpp_new` 替换 `bridge/engine_api` 的实现」完成并通过真机验收之后 |
| 允许的改动 | 只允许「让当前构建保持可编」的最小修复 |

## 为什么冻结而不是立刻删除

删掉本目录会让 Android APK 在第 11 步完成前**完全不能构建**（宿主 `app/` 与 `bridge/engine_api`
都挂在它上面）。所以采取「冻结 + 标记」，把删除作为第 11 步的收尾动作。

## 后继在哪

| 主题 | 现在看哪里 |
|---|---|
| 新引擎（分层、契约、断言） | `cpp_new/docs/ARCHITECTURE.md`、`cpp_new/todo.md` |
| 渲染转权威实现（R1 路线、进度、估计） | `cpp_new/docs/visual-vendor.md`、`cpp_new/docs/rendering-architecture.md` |
| 老树代码去向（**0 行复用**） | `cpp_new/docs/legacy-gap.md` |
| 老树立绘/特殊CG/Q版动画/字体问题复盘 | `cpp_new/docs/legacy-render-failures.md` |
| 上游来源与补丁登记 | `cpp_new/vendor/krkrz/VENDOR.md` |

## 本目录只作行为参考

老树仍然是**行为参考**（游戏实际怎么跑、ABI 语义、兼容补丁清单），但**不是**代码来源：
新引擎的铁律是「不 include 老树任何头」，唯一例外是 `cpp_new/vendor/`（上游 krkrz 原样拷贝）。

本目录内部的 `DECISIONS.md`、各模块 `README.md` 保留原样（历史记录）。其中提到的
`scripts/check_cmake_graph.sh`、`scripts/check_tu.sh` 等脚本已在仓库清理中删除。
