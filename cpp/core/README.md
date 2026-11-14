# cpp/core —— 模块地图与重组约定

core 是引擎本体：11 个模块 + `common/`，全部编译进 `krkr2core`（INTERFACE）→
最终链进 `libengine_api.so`。`tjs2/` 是第三方脚本引擎，独立演进，**不参与**本目录的
重组约定（见 `docs/STRUCTURE_DESIGN.md` §7）。

## 三条约定

**1. 目录 = target。**
一个模块内部按依赖层次切目录，每个目录 = 一个 STATIC target，
名字统一为 `core_<模块>_<层>`。读目录名就知道谁依赖谁，不用打开文件。

**2. 接口头放在它服务的那一层，实现放在需要上层的那一层。**
`.h` 和它的 `.cpp` **可以、而且应该**落在不同目录：

| 例子 | 头 | 实现 |
|---|---|---|
| 事件接口 | `base/core/EventIntf.h` | `base/runtime/EventIntf.cpp`（要推事件到 TJS） |
| 存储门面 | `io/storage/IoVirtualFile.h` | 同目录（不改行为，只是例子） |
| 波形循环 | `sound/common/WaveLoopManager.h` | `sound/intf/WaveLoopManager.cpp`（要 `tTVPWaveFormat`） |

判断标准只有一条：**这个文件 include 谁**，不是它叫什么。

**3. 模块名保留为 INTERFACE 聚合层。**
`core_base_module` / `core_io_module` / `core_sound_module` 这些名字永远存在，
内部改成几个簇都行。这样重组一个模块时，**别的模块一个字都不用改**，
diff 只落在被重组模块的目录里。

## 现状

| 模块 | 聚合层 | 分层 | README |
|---|---|---|---|
| `base` | `core_base_module` | ✅ `core/` `runtime/` `script/`（`sysinit/` 已并入 environ） | [base/README.md](base/README.md) |
| `io` | `core_io_module` | ✅ `policy/` `path/` `archive/` `storage/` | [io/README.md](io/README.md) |
| `sound` | `core_sound_module` | ✅ `common/` `intf/` `codec/` `impl/`（原 `win32/`） | [sound/README.md](sound/README.md) |
| `environ` | `core_environ_module` | ✅ `intf/` `sysinit/` `platform/` `app/`（`win32/` 已解散） | [environ/README.md](environ/README.md) |
| `visual` | `core_visual_module` | ❌ 156 文件 / 10 万行，且有 `simd/` `gl/` `impl/` `ogl/` 三套历史落点 | — |
| `movie` | `core_movie_module` | ❌ 85 文件，主体未动 | — |
| `plugin` | `core_plugin_module` | ✅ `host/` `ncb/` `loader/` | [plugin/README.md](plugin/README.md) |
| `utils` | `core_utils_module` | ✅ `c_api/` `misc/` `intf/` `impl/`（原 `win32/`） | [utils/README.md](utils/README.md) |
| `compat` | `core_compat_module` | ✅ `layer/` `companions/` | [compat/README.md](compat/README.md) |
| `extension` | `core_extension_module` | ❌ **有意不拆**（只有 1 个实现文件） | [extension/README.md](extension/README.md) |
| `common` | （`include_directories` 直接暴露） | — 只有 `Defer.h` | — |

## 依赖方向（实测，2026-11）

```
utils     -> tjs2                                       叶子（仍 PRIVATE 链 base）
io        -> base, tjs2, utils                          干净三层
movie     -> visual, sound, base, utils, tjs2, environ   干净顶层（无人反向依赖）
extension -> tjs2
compat    -> io, tjs2, base
```

仍存在的 **5 组双向依赖**（`docs/STRUCTURE_DESIGN.md` §1.1）：
`base ↔ visual` / `base ↔ sound` / `base ↔ plugin`（同一张 TJS 类注册表）、
`base ↔ environ`、`environ ↔ visual`。拆法与顺序见 `cpp/DECISIONS.md` #9 与
`docs/HANDOVER.md` §7，**三条都需要编译器**，不要在拿不到编译验证时动。

## 重组一个模块的动作清单

以 `io` / `sound` 两轮为模板（照这个顺序做，一步一个 commit）：

1. 用 include 图（不是文件名）给每个文件定层，写出分层的理由；
2. `git mv` 进同名目录（**用 `git mv`**，否则 `git log --follow` 断）；
3. CMakeLists 改成 N 个 STATIC + 1 个 INTERFACE 聚合层，**链接行逐条对照搬迁前**；
4. 公共 include 面按"每层一个目录 + 历史遗留面"两条变量写，历史面先别收窄；
5. 写 `README.md`：层表、实测 include 依据、已知的环、新文件放哪、验收命令；
6. 更新显式路径调用点（`#include "io/storage/IoVirtualFile.h"` 这类）；
7. 更新指向旧路径的注释（`grep -rn "旧路径"`，注释最容易留下过期信息）；
8. `scripts/check_cmake_graph.sh <模块>` —— 不需要 NDK 就能验聚合层与源文件。

## 验证

```bash
scripts/check_cmake_graph.sh io sound   # target 图 + 源文件存在性（无需 NDK/vcpkg）
bash scripts/check_static.sh            # JNI 符号 / 语法 / 移植清单
scripts/check_tu.sh --module io         # 需要 NDK，逐 TU 编译
./build.sh debug --engine-only          # 真正判据
```

`check_cmake_graph.sh` 只证明"target 图对、文件在"，**不证明能编译**。
本仓库有大量未过编译器的改动，别把它的绿灯当成编译通过。
