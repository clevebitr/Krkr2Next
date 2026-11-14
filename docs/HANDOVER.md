# 交接记录 — namespace 迁移 + 依赖拆环

> 时间：本次会话
> 基线：`cae8c9a`（上一轮 docs 提交）→ 当前 HEAD
> 共 **29 个提交**，全部单 cluster、可单独回滚
> 说明：本阶段所有改动**都还没有经过编译验证**（本机无 NDK/cmake/clang）。
> 验收清单见 §5。

---

## 1. 一句话现状

- **namespace 迁移**：6 个模块完全收口（utils / io / sound / plugin / compat / extension），
  base 与 environ 的代码层收口，visual / movie / plugins 未做。
- **依赖拆环**：模块间双向依赖 **14 组 → 5 组**。
- **仍未动的**：`visual`（157 文件 / 10 万行）、`plugins/`（302 文件 / 10 万行）、`movie` 主体。

---

## 2. namespace 迁移完成度

`已迁` = 该文件里出现 `namespace krkr*`。声明全在全局的"纯声明头"
（如 `Platform.h`）算作已处理。

| 模块 | 已迁/总 | 状态 |
|---|---:|---|
| `utils` | 29/40 | ✅ 叶子模块，只依赖 tjs2 |
| `io` | 19/20 | ✅ 余 `tar.h`（TAR 格式结构体） |
| `sound` | 32/41 | ✅ 余 9 个：MIDL COM ABI / 第三方 SDK / 不参与构建 |
| `plugin` | 7/7 | ✅ 含 11 个 NCB 宏族的限定名改造 |
| `compat` / `extension` | 6/6 · 2/2 | ✅ |
| `base` | 47/60 | ✅ 余 13 个见 §6 |
| `environ` | 18/22 | ✅ 余 4 个平台契约/类型头 |
| `visual` | 22/157 | 🔴 未系统推进，`simd/` 仍是 `namespace krkr2` |
| `movie` | 2/85 | 🔴 主体未动 |
| `tjs2` | 0/73 | ⛔ 设计上排除 |

---

## 3. 拆环：8 个提交做了什么

| commit | 破掉的环 | 手段 |
|---|---|---|
| `c60623c` | — | `io → visual` 归零（`WindowImpl.h` 是死 include） |
| `f09d90f` | **`base ↔ io`** | XP3 段缓存 override 状态下移到 base；顺带清 `io → environ` 死 include |
| `76f161f` | **`plugin ↔ visual`** | 6 处跨模块死 include |
| `612ff56` | **`utils ↔ base`**、**`utils ↔ environ`** | 7 个"不是工具的实现"搬到 `base/impl`；`tTVPAsyncTriggerMode` 下移 |
| `a176393` | **`io ↔ environ`** | `Platform.h` / `combase.h` / `typedefine.h` / `cpu_types.h` / `vkdefine.h` / `DetectCPU` / `ApplicationSpecialPath` / `ConfigManager` 从 environ 下移到 base |
| `aef126b` | **`movie ↔ visual`**、**`movie ↔ sound`** | `DeferredDestroyer` 下移到 utils；`TVPInitLibAVCodec` 就地声明 |
| `bd51eb8` | **`environ ↔ plugin`**、**`environ ↔ sound`** | 3 处死 include |

### 3.1 当前依赖图

```
utils     -> tjs2                                       叶子
io        -> base, tjs2, utils                          干净三层
movie     -> visual, sound, base, utils, tjs2, environ   干净顶层（无人反向依赖）
extension -> tjs2
compat    -> io, tjs2, base
```

### 3.2 剩余 5 组环

| 环 | 权重 | 卡在哪 |
|---|---|---|
| `base ↔ visual` | 26 / 147 | **同一根因**：`base/ScriptMgnIntf.cpp` 的 TJS 类注册表 |
| `base ↔ sound` | 4 / 29 | （21 个 `TVPCreateNativeClass_*` + 3 处 `new`） |
| `base ↔ plugin` | 2 / 9 | |
| `base ↔ environ` | 12 / 56 | `Application.h`(7) + `SystemControl.h`(5)：需要接口抽取 |
| `environ ↔ visual` | 28 / 11 | visual 侧 11 处用了 environ 的 11 个不同头 |

---

## 4. 方法沉淀（踩过的坑，下一个人不用再踩）

### 4.1 死 include 的两类误判

自动扫描只能**生成候选**，每条必须人工验证：

1. **变量名不在符号表里** —— visual/sound 里对 `Application.h` 的引用，
   实际用的是 `Application->X`，而 `Application` 这个变量名不在头文件的
   类名/`TVP*` 符号表里 → 会被误判成"没用"。
2. **符号长在被 include 的文件里** —— `MsgIntf.h` 的消息符号实际在它 include 的
   `MsgIntfInc.h`；`Application.h` 顺带提供 `<vector>`/`<functional>`。
   直接删会连带丢掉传递依赖。

### 4.2 判断"属于哪一层"要看依赖谁，不看名字

`CharacterSet`（UTF-8 转换）名字像工具，但实现要用 base 的
`TVPThrowExceptionMessage` → 留在 base。挪下去会凭空造一条新反向边。

### 4.3 同名 include guard 的重复头必须一起处理

`environ/win32/DetectCPU.h` 与 `environ/DetectCPU.h` 是同一个 guard（`DetectCPUH`）
的两份副本。实现搬到 base 后若留下副本，谁先命中就会声明出
`krkr::environ::TVPDetectCPU` 而实现已不在那里 → 链接失败。已删除副本。

### 4.4 块作用域 `extern` 会被绑到 namespace

`TVPResetRuntimeForRestart()` 函数体内的 `extern void TVPStopAllWaveSoundsForRestart();`
在包了 `krkr::base` 之后会绑定到 `krkr::base::TVPStopAllWaveSoundsForRestart`
（[dcl.stc]/5）→ undefined reference。必须提到文件作用域。

### 4.5 `declaration` vs `using-declaration` 冲突

`ScriptMgnImpl.h` 里全局声明 `TVPInitializeStartupScript`，而 `ScriptMgnIntf.h`
新增了 `using krkr::base::TVPInitializeStartupScript;` → 同一作用域共存是错误。
只要有一个 TU include 到就炸。

### 4.6 `using` 救不了显式特化

NCB 的 `NCB_*` 宏会在插件作用域展开 `template <>` 显式特化。
模板进 namespace 后，**宏体必须写成限定名**（`krkr::plugin::ncbClassInfo<cls>`），
`using` 只解决普通名字引用。共改了 11 个宏族。

### 4.7 枚举值不随类型导入

`tTVPAsyncTriggerMode { atmNormal, ... }` → `using` 类型不够，
三个枚举值要单独 `using`（AGENTS.md §9.3）。

### 4.8 头依赖清零 ≠ 链接依赖清零

`sound → movie` 的 `TVPInitLibAVCodec` 改成就地声明后，头依赖没了，
但链接期 sound 仍调用 movie 的实现。这类"半解"必须在 commit message 里写清楚。

### 4.9 行尾

`EventIntf.cpp` 是纯 CRLF（1214/1214），`EventIntf.h` 是纯 LF。
多行编辑前先确认行尾，否则精确匹配会失败。

---

## 5. 验收清单（有编译器后按序执行）

### 5.1 必跑

```bash
bash scripts/check_static.sh                    # 不需要 NDK，先跑
./build.sh debug --configure-only               # vcpkg configure（最慢，先落缓存）
./build.sh debug --engine-only                  # 出 libengine_api.so
```

```bash
# 想快就用 Linux 宿主（CI 的 engine_verify 走这条）
cmake --preset "Linux Debug Config" && cmake --build --preset "Linux Debug Build"
```

### 5.2 按风险顺序排查

如果编译失败，这些提交按风险从高到低：

| 顺序 | commit | 可能症状 |
|---|---|---|
| 1 | `42845ab` NCB 模板层 | `explicit specialization of 'krkr::plugin::ncbClassInfo<X>' outside its namespace`；`specialization of 'krkr::plugin::ncbRegistClass<...>::Regist' in different namespace` |
| 2 | `f3c64ac` EngineLoop+MainScene | `TVPDrawSceneOnce` / `TVPGetJoyPadAsyncState` "not declared" 或链接期 undefined reference |
| 3 | `79c60f9` MsgIntf | 消息符号 `TVPxxx` "was not declared"（说明 `MsgIntf.h` 的 namespace 边界放错，343 个消息定义被包进 namespace 了） |
| 4 | `a176393` 契约下移 | `Platform.h` / `combase.h` / `typedefine.h` / `virdefine.h` "No such file"（include 路径问题） |
| 5 | `612ff56` utils 变叶子 | `core/utils` 里残留文件引用 base 符号 → undefined reference |
| 6 | 其余 | 基本都是 "not declared" |

### 5.3 符号检查

```bash
llvm-nm -C --defined-only out/android/debug/bridge/engine_api/libengine_api.so \
  | grep -E 'krkr::(io|base|utils|sound|plugin|environ)::' | head -40
```

### 5.4 行为对比

启动日志与迁移前对比，重点看：

- XP3 挂载顺序、`patch.xp3` 优先级
- `TVPIsExistentStorage` 的模块查询
- TJS 类是否齐全（`Storages` / `Layer` / `Window` / `WaveSoundBuffer` / `AsyncTrigger` …）
  —— 这是 `base ↔ visual/sound/plugin` 注册表那一族的验收点
- 字体 / 插件加载
- 建议用千恋万花 + NEKOPARA 各跑一次

### 5.5 已知的"半解"项（不影响编译，但要记着）

- `sound → movie`：头依赖已清零，**链接期仍调用** `TVPInitLibAVCodec`
- `utils` 仍 PRIVATE 链接 `core_base_module`（`TVPTimer.cpp` 用
  `TVPGetRoughTickCount32`，实现现在在 `base/impl/TickCount.cpp`）
- `--allow-multiple-definition` 仍在，已知掩盖的重复定义：
  `TVPWriteDataToFile`、`TVPCheckAndSendDumps`、`TVPShowFileSelector`

---

## 6. 例外登记（有意不迁）

| 文件/符号 | 原因 |
|---|---|
| `tjs2/` 全部 | 保持 `TJS` 命名空间 |
| `base/KAGParser.h/.cpp` | `.cpp` 整体在 `namespace TJS`、header 有 `using namespace TJS;`，属 TJS 边界 |
| `base/MsgIntfInc.h`、`base/impl/MsgImpl.h` | `gentext.pl Messages.xlsx` 的**生成文件**，343 个消息符号永久留全局 |
| `base/UserEvent.h`、`base/impl/EventImpl.h` | 只有宏 / 只有一句 include |
| `base/impl/ScriptMgnImpl.h/.cpp` | 唯一声明已删 / 236 行全是注释 |
| `sound/win32/tvpsnd.*`、`kmp_pi.h`、`oldwaveunpacker.h` | MIDL 生成的 COM ABI / 第三方 SDK 契约 |
| `utils/md5.h`、`iconv/`、`MathAlgorithms*`、`Debugger.h` | C API / 生成表 / 与 sound 重复声明 |
| `io/tar.h`、`common/Defer.h` | 格式结构体 / `namespace clover` 的通用小工具 |
| `environ/combase.h`… 等类型头 | 已下移 base，本身无 namespace（纯声明） |

---

## 7. 下一步的三条路

### 路 A：接口抽取（需要设计，不需要注册顺序知识）

`base/impl/ApplicationIntf.h` 定义 `iTVPApplication`（7 个虚函数：
`Terminate` / `ProcessMessages` / `PostUserMessage` / `FilterUserMessage` /
`GetActivating` / `GetTitle` / `SetTitle`），`tTVPApplication` 继承它，
`Application` 指针类型改成接口。

- 解掉 `base → environ`(7) 与 `sound → environ`(1)
- 风险：`Application->Terminate()`、`ProcessMessages()` 的调用时序不能变

### 路 B：TJS 类注册表反转（**必须有编译器**）

把 `base/ScriptMgnIntf.cpp` 的注册表搬到 environ，base 留一个注入口。
- 一次解掉 `base ↔ visual`(26) / `base ↔ sound`(4) / `base ↔ plugin`(2)
- 风险：注册顺序影响 `TJS_MEMBERENSURE`；注入口设晚一步 = 所有 TJS 类消失

### 路 C：文件结构重设计（**建议等编译验证之后**）

见 §8。

---

## 8. 关于文件结构（结论：现在先别动目录）

### 8.1 为什么等

1. 当前 HEAD 有 29 个**未编译验证**的提交。再叠一层目录搬迁，
   编译失败时无法区分"是 namespace 改动"还是"是路径改动"——bisect 失效。
2. 剩下 5 组环是**结构性**的。环没断就搬目录，只是把这团网原样挪到新目录里。
3. 目录一改，`git log --follow`、`git blame` 全断，历史追溯成本立刻上升。

### 8.2 但可以先做的（零风险）

把目标结构写成设计文档，含：目标树、每步的 include 路径变化、
CMake target 拆分方案、每步的验收命令。等编译绿了照着执行。

### 8.3 推荐的目标形态（详见 `DECISIONS.md` #8）

```
core/<module>/
  include/     ← 只在 PUBLIC include path（门面头 + 子模块头）
  src/         ← 只在 PRIVATE include path
  platform/{android,linux,windows}/
```

四条规则：

1. **不做"单一总头"，做"每模块一个门面头 + 显式子模块头"**
   - 反例就在仓库里：`tjsCommHead.h` 被 164 个文件包含，作用只是"让所有人依赖 tjs2"
2. **子模块 = CMake target，不是目录**
   - `utils/string/` 如果还是同一个 STATIC 库，只有心理收益
3. **内部头不在公共 include path 上** → 谁 include 内部头就是编译错误
   （这是唯一能落地的约束，文档约束在这个仓库已经失效过一次）
4. **CI 加 include-graph ratchet**
   - 层间反向 include 直接 fail；门面头的 include 数单调不增
   - 去掉 `--allow-multiple-definition`（先变 warning 清单）

### 8.4 第一个动手的模块建议：`io` 或 `utils`

它们现在已经是干净的层（io → base/tjs2/utils；utils → tjs2），
拿它们做 `include/src/platform` 三层的样板，风险最低，也最容易验证效果。

---

## 9. 决策索引

本阶段的决策记录在 `DECISIONS.md`（未跟踪，因为被 hook 保护只能追加）：

| # | 主题 |
|---|---|
| 1 | environ 硬骨头：EngineLoop + MainScene 必须同一个 cluster |
| 2 | NCB 模板层必须在有编译器时做 |
| 3 | 硬骨头已拆 + 更正"重复定义"的误判 |
| 4 | environ 停在 28/37：剩下 9 个文件的分类和处理理由 |
| 5 | KAGParser 登记为例外 |
| 6 | 阶段计划：base 收口 → 文件结构重设计 → visual/movie/plugins |
| 7 | MsgIntf 扫描结论：消息目录是生成文件，永久留全局 |
| 8 | 文件结构重设计：5 条建议 + 实测依赖图 |
| 9 | 拆环：io→visual 已切；base 注册表需真反转且需编译器 |
| 10 | 拆环结果：utils 变真叶子，双向依赖 14 → 10 |
| 11 | 拆环收尾：双向依赖 14 → 5 |
