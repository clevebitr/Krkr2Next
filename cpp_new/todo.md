# cpp_new 任务清单

> 文档分层（见 `AGENTS.md` §3）：项目级计划在仓库根 `PLAN.md`；问题与决策记录在 `DECISIONS.md`；
> 设计与规格在 `cpp_new/docs/`；**本文件只记录当前阶段的可执行步骤与验收标准**。
> 历史实施记录已归档至 `cpp_new/docs/archive/todo-2025-11.md`，不再维护。

---

## 1. 当前状态

新引擎位于 `cpp_new/`，四层结构（`tjs` / `shim` / `core` / `engine`）已落地，并新增
`vendor/`（krkrz 上游原样代码）与 `pixel/`（混合操作抽象层）。脚本层可执行真实游戏的启动脚本，
渲染处于**第 8 步的重构阶段 R1**：像素语义已交由 krkrz 权威实现，合成结构仍为过渡实现。

当前步骤：**第 8 步 / R1-2a（seam 原语）**。断点与验收见 §4。

## 2. 阶段划分

| 大步 | 内容 | 状态 |
|---|---|---|
| 8 | 渲染：像素语义交由 krkrz（R1），合成结构改为 legacy 图层模型 | 进行中（R1-2a） |
| 9 | 输入：指针、按键、文本 → 脚本事件 | 未开始 |
| 10 | 音频：WAVE 与 OGG 解码、混音、回调 | 未开始 |
| 11 | 切换：以 `cpp_new` 替换 `bridge/engine_api` 的实现（签名不变） | 未开始 |

第 8 步的分片（R1 路线，规格见 `docs/visual-vendor.md`）：

| 片 | 内容 | 状态 | 验收 |
|---|---|---|---|
| R1-0 | vendor 清单、sha256、补丁登记 | 完成 | `tools/verify_vendor.py` 零不一致 |
| R1-1 | `tvpgl` 接入、`pixel/` 抽象层、算子矩阵断言 | 完成 | `blend_matrix` 300 项通过 |
| R1-2a | seam 原语：线程、路径、CPU 特性、存储媒体、位图内存 | **进行中** | 见 §4 |
| R1-2b | 位图、字体、图片解码、Windows oracle | 未开始 | `loadImages → saveLayerImage` 与 Windows 逐字节一致 |
| R1-3 | 图层模型与转场；删除自建 `Scene` | 未开始 | 29 种类型矩阵与转场逐像素一致 |
| R1-4 | Android 呈现（`VisualDrawDevice`） | 未开始 | 真机一帧，与软件落盘图偏差 ≤1 |
| R1-5 | 收口：删除过渡件、更新文档与静态检查 | 未开始 | `ctest` 全绿，`check_layer_deps` 零违规 |

## 3. 构建与断言

```bash
bash cpp_new/tools/run_probes.sh                 # 配置 + 构建 + CTest（当前 23 项）
python3 cpp_new/tools/check_layer_deps.py cpp_new   # 层间依赖方向（要求 0 违规）
python3 cpp_new/tools/verify_vendor.py              # 上游完整性与补丁面
```

实际构建目录位于 `../build/cmake/<preset>`；预设见 `cpp_new/CMakePresets.json`
（`desktop-debug` / `desktop-release` / `android-debug`）。

## 4. 当前步骤：R1-2a（seam 原语）

编译驱动盘点的结论（见 `docs/visual-vendor.md` §7.1）：上游 visual 的源码除三个 porting 头与
两个第三方库外均可直接编译。本步骤完成前五项，第三方库留待 R1-2b。

| # | 任务 | 产出 | 验收 |
|---|---|---|---|
| 1 | 线程原语 | `vendor/krkrz/visual/porting/thread_impl.h`（`std::thread` / `std::mutex`） | `LayerBitmapIntf.cpp` 编译通过 |
| 2 | 系统路径与本地名 | `porting/sys_init.h/.cpp`（替代 `SysInitImpl.h`） | `LayerBitmapImpl.cpp` 编译通过 |
| 3 | CPU 特性查询 | `porting/detect_cpu.h`（替代 `DetectCPU.h`，返回通用实现） | `GraphicsLoaderIntf.cpp`、`LoadTLG.cpp`、`SaveTLG5.cpp` 编译通过 |
| 4 | 存储媒体适配 | `porting/storage_media.h/.cpp`（`iTVPStorageMedia` 与 `tTJSBinaryStream` 接 `krkr::io::Storage`） | 新增 `storage_media_probe`：夹具文件经该通路读出字节与 `Storage` 一致 |
| 5 | 位图内存分配 | `porting/bitmap_memory.h/.cpp`（替代 `win32/BitmapBitsAlloc`，含对齐与哨兵） | 分配/释放往返与越界哨兵断言通过 |
| 6 | 文档与检查同步 | `vendor/krkrz/VENDOR.md`、`docs/visual-vendor.md`、本文件 | `check_layer_deps`、`verify_vendor` 通过 |

新文件均属本仓库实现，命名遵循 `AGENTS.md` §5，不使用上游前缀（`TVP`/`tTVP`/`iTVP`）。
上游源码不得修改；确需修改时登记至 `vendor/krkrz/VENDOR.md`。

## 5. 文档索引

| 主题 | 文件 |
|---|---|
| 分层与依赖约束 | `docs/ARCHITECTURE.md` |
| 渲染规格（29 种算子、采样、GL） | `docs/render-spec.md` |
| 渲染架构现状与目标 | `docs/rendering-architecture.md` |
| R1 分片计划与实测盘点 | `docs/visual-vendor.md` |
| 老树代码去向（零复用） | `docs/legacy-gap.md` |
| 老树渲染问题复盘（立绘、CG、动画、字体） | `docs/legacy-render-failures.md` |
| 技术债与已知问题（编号稳定） | `docs/tech-debt.md` |
| 历史实施记录（冻结） | `docs/archive/todo-2025-11.md` |
| 上游来源与补丁 | `vendor/krkrz/VENDOR.md` |

## 6. 纪律摘要

完整规范见仓库根 `AGENTS.md`。要点：

1. 不引用老树头文件；例外仅限 `vendor/`（上游原样代码）。
2. 新增功能先在 `core/contracts` 定义契约，再在 `platform` 与 `shim` 实现。
3. 每次提交前运行 `run_probes.sh`、`check_layer_deps.py`、`verify_vendor.py`。
4. 同一时刻只允许一个组件决定像素结果；禁止保留第二条合成路径。
5. 文档与代码注释使用书面语，不使用口语、语气词与第一人称叙述。
