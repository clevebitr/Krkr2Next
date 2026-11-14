# core/compat —— 两层：层的策略 / 层的内容

> 本文只讲**代码结构**。兼容层的行为口径、两层各自的差异、迁移里程碑（M1.x）
> 在仓库根的 [`compat/README.md`](../../../compat/README.md)，那边是权威。

`compat` 是"引擎同时带着两套兼容行为、一个进程只激活一套"的实现。它只有 3 个
`.cpp`，但里面有两条性质完全不同的东西：**策略**（哪一层生效、哪个模块放行）
与**内容**（AetherKiri 层专属的虚拟文件/伴生脚本）。现在分开：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `layer/` | `core_compat_layer` | `CompatLayer.{h,cpp}`（层 id、激活、IO 策略注入）、`ModuleGate.{h,cpp}`（模块门控） | io 的策略头 + `tjs2` |
| `companions/` | `core_compat_companions` | `AetherKiriCompanions.{h,cpp}`（GPU 伴生脚本等虚拟文件 provider）、`resources/D3DEmote.tjs`（`configure_file` 生成字节数组） | `layer` + base 的 `StorageIntf` + io 的 `IoVirtualFile` 注册点 |

`core_compat_module` 是一个 **INTERFACE 聚合层**，`base` / `plugin` / `plugins` /
`bridge` 里的调用点（链接行）零改动。

## 为什么这么切

实测 include 图只有一条方向：

```
companions/AetherKiriCompanions.cpp → CompatLayer.h   （要问"现在是哪一层"）
layer/CompatLayer.cpp               → AetherKiriCompanions.h（激活时注册 provider）
```

看起来是双向的，但性质不同：`CompatLayer` 只 include `AetherKiriCompanions.h` 的
**声明**去调用注册函数，而 `AetherKiriCompanions.cpp` 需要的是策略**取值**。
拆开后 `layer/` 不再 include `base/core/StorageIntf.h` 与 `io/storage/IoVirtualFile.h`
—— **"策略不认识内容"** 这件事在 CMake 层面成立，而不是靠人记住。

这正是 `compat/README.md` §2 的分工：`cpp/core/io/` 不认识"层"，层只给 io 一份
策略取值；层专属内容通过 io 的泛型注册点贡献，不反向实现挂载/路径解析。

## 一条不能破的约束

`compat` **不允许**依赖 `krkr2plugin`、io 的实现细节、或任何具体游戏/发行版代码
（`compat/README.md` §2）。允许 include 的只有：

- `io/policy/StoragePolicy.h`（策略契约）
- `io/storage/IoVirtualFile.h`（虚拟文件注册点）
- `base/core/StorageIntf.h`（`ttstr` / `tTJSBinaryStream` 这类类型）

## 显式路径：7 处调用点（改目录必须一起改）

`compat` 是全仓被**显式路径**引用最多的模块（因为它横切所有模块）：

| 调用点 | include |
|---|---|
| `cpp/core/plugin/ncb/ncbind.cpp` | `compat/layer/ModuleGate.h` |
| `cpp/plugins/compat/aetherkiri/legacy_system_env.cpp` | `compat/layer/ModuleGate.h` |
| `cpp/plugins/compat/aetherkiri/legacy_system_misc.cpp` | `compat/layer/ModuleGate.h` |
| `cpp/plugins/compat/aetherkiri/legacy_zlib_version.cpp` | `compat/layer/ModuleGate.h` |
| `cpp/plugins/krkrgles.cpp` | `compat/layer/CompatLayer.h` |
| `cpp/plugins/krkrlive2d.cpp` | `compat/layer/CompatLayer.h` |
| `bridge/engine_api/src/engine_api_internal.h` | `compat/layer/CompatLayer.h` |

这 7 处本轮已同步（`grep -rn '#include "compat/'` 复核）。

## 已知的"半接线"（本轮没动，也不该在搬目录时动）

`layer/CompatLayer.h` 自己写着：策略"还没有接到 `StorageIntf`/`StorageImpl` 的实现上，
当前对运行时行为没有影响，只提供日志与后续接线的单一入口"。`ModuleGate` 默认放行。

这是 `TODO.md` P0 第 3 条点名的"半接线抽象"：**付了认知成本，没拿到行为收益**。
它需要的是把 `StoragePolicy` 逐条接到 io 的实现上（M1.2–M1.6），
不是目录重组能解决的问题。

## 加一个新文件放哪

1. 层 id、激活链路、某模块在本层是否放行 → `layer/`
2. 某层专属的虚拟文件 / 伴生脚本 / 内嵌资源 → `companions/`
3. 需要新的内嵌资源 → `companions/resources/` + `configure_file`（照 `D3DEmote.tjs`
   的做法：hex 字节数组，别手写字符串字面量）

## 验收（有编译器之后）

```bash
scripts/check_cmake_graph.sh compat
llvm-nm -C --defined-only libengine_api.so | grep 'krkr::compat::'
```

行为面：启动日志里的 `compat layer = …`、AetherKiri 层下 11 个 GPU 存储名是否可打开、
`motion_<asset>.psb` / `*emo` 虚拟存储是否注入。
