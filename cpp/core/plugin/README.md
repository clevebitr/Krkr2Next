# core/plugin —— 三层：宿主 API / NCB 框架 / 加载器

`plugin` 只有 8 个文件，但它是全仓库**最不能靠"文件不多"判断风险**的模块：
`ncbind.hpp` 被 50 个插件源文件 include，`NCB_*` 宏会在插件作用域展开
`template <>` 显式特化。所以这一轮不做宏改造，只把三个职责分开：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `host/` | `core_plugin_host` | 宿主 API 契约：`PluginIntf.{h,cpp}`（`TVP_malloc` / `TVP_free` / `ZLIB_*` / `TVP_md5_*` / `TVPRegisterGlobalObject` / `TVPDoTryBlock` …） | base 的 `MsgIntf` |
| `ncb/` | `core_plugin_ncb` | NCB 框架：`ncbind.{hpp,cpp}` `ncb_invoke.hpp` `ncb_foreach.inc` | base（Storage / ScriptMgn / Debug / Msg / CharacterSet）+ `compat/ModuleGate.h` |
| `loader/` | `core_plugin_loader` | 插件加载器与注册表：`PluginImpl.{h,cpp}`（`tvpLoadPlugins` / `TVPRegisteredPlugins` / `TVPResetPluginSystemForRestart` / NCB 自动注册） | `host` + `ncb` + io + environ |

`core_plugin_module` 是一个 **INTERFACE 聚合层**，所以 `base` / `environ` / `visual` /
`plugins` 里的调用点零改动。

## 三个目录各自的"外部面"

| 头 | 外部 include 次数 | 意思 |
|---|---:|---|
| `ncb/ncbind.hpp` | 50 | 每个插件都 include 它；**改它等于全仓重编** |
| `loader/PluginImpl.h` | 5 | 宿主侧（`Application.cpp`、`bridge`）要注册表 |
| `host/PluginIntf.h` | 2 | 插件侧，很小 |

这个分布说明为什么 `ncb/` 值得单独一层：它的风险（宏 + 模板特化）和
`host/`（一张 C 函数表）完全不是一回事，混在一个 target 里时链接行和 include 面
也只能一起动。

## NCB 的约束（改之前必读）

- `NCB_*` 宏会在**插件作用域**展开 `template <>` 显式特化，所以宏体里必须是
  **限定名**（`krkr::plugin::ncbClassInfo<cls>`）。`using` 只解决普通名字引用，
  救不了显式特化 —— 这条是 `DECISIONS.md`「using 救不了显式特化」踩出来的。
- 因此把 NCB 收成 `krkr::plugin::ncb` 是一次**宏族同步改造**（11 个宏族），
  必须一次做完且必须有编译器。本轮只搬文件，宏一个字没动。

## 已知的边 / 环（不在本轮解决）

- **`plugin → sound`（`loader/PluginImpl.h:19` → `kmp_pi.h`）**：为了 `TKMP_*` 的
  acquire/release 声明，直接 include 了 `sound/impl/kmp_pi.h`（MIDL 生成的 ABI 头）。
  它不属于任何一层的实现，是 ABI 契约；短期保留，长期应和 `tvpsnd.h` 一起
  归到"平台 ABI 头"的独立位置。
- **`plugin ↔ environ`**：`Application.cpp` 要 `PluginImpl.h`（加载插件），
  `loader/PluginImpl.cpp` 要 `environ/intf/SysInitImpl.h`（配置项 `TVPPluginState`）。
  拆法 = 插件注册表反转 + `iTVPApplication` 接口抽取，与 `DECISIONS.md` #9 同批。
- **`plugin ↔ base`**：`ncb/ncbind.hpp` 直接 include `StorageImpl.h`（base 门面）与
  `ScriptMgnIntf.h`（base script）。这是"插件可以 include 任何核心头"的实证；
  真正要解决的是**插件契约**（能 include 什么、能调哪些符号），不是这一处 include。
- **`ncb/ncbind.cpp → compat/ModuleGate.h`**：NCB 自动注册受兼容层门控，
  依赖方向 compat → plugin 与 plugin → compat 各有一半，先记着。

## 没做的（下一步）

1. **收敛 NCB 到 `krkr::plugin::ncb`**：11 个宏族 + 50 个 include 点，需要编译器。
2. **定插件契约**：目前 `plugins/` 里 119 个源文件可以 include 任何核心头、调任意
   全局（`TVP*` ≈980 个）。没有契约的话，每次核心重构都会波及这 10 万行
   （`docs/STRUCTURE_DESIGN.md` §7 已登记为"最后做"）。
3. **`TVPRegisteredPlugins` 的生命周期**：与 `--allow-multiple-definition`、
   二次启动类 bug 都在同一片区域（全局状态 + 隐式初始化顺序）。

## 加一个新文件放哪

1. 插件能调用的宿主函数（malloc / md5 / zlib / 注册全局对象）→ `host/`
2. `ncbClassInfo` / `ncbRegistClass` 这类模板与宏 → `ncb/`
3. 加载 `.so`/`.dll`、维护注册表、NCB 自动注册 → `loader/`
4. 具体插件（`wutcwf`、`layerExImage`、`krkrgles` …）→ **不属于本模块**，
   在 `cpp/plugins/`

## 验收（有编译器之后）

```bash
scripts/check_tu.sh --module plugin
scripts/check_cmake_graph.sh plugin
llvm-nm -C --defined-only libengine_api.so | grep 'krkr::plugin::'
```

行为对齐点：插件加载日志（`tvpLoadPlugins` 的输出顺序）、NCB 自动注册的注册数、
`TVPRegisteredPlugins` 的内容。搬文件不改变任何一个，出现差异说明是 include
路径问题或某个插件少了 `ncbind.hpp`。
