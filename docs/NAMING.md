# 命名契约（NAMING）

> 状态：**已定稿，逐步执行**。规则依据不是"我觉得好看"，而是**仓库里新代码已经在
> 这么写**（`SevenZipArchive` / `ActiveStoragePolicy` / `EngineLoop` / `JniHelper` /
> `AetherKiriCompanionRegistrar`）。本契约做的是把遗留名搬过来，不是发明新风格。
>
> 适用范围：`cpp/core/**`、`cpp/plugins/**`、`bridge/**`、`tools/**`。
> `cpp/core/tjs2/` 是第三方，**不适用**（见 §4）。
> 关联：`cpp/core/README.md`（目录约定）、`docs/STRUCTURE_DESIGN.md`（分层）、
> `cpp/DECISIONS.md`（每轮的决策记录）。

---

## 1. 六个名字域，六条规则

| # | 名字域 | 规则 | 反例 → 正例 |
|---|---|---|---|
| N1 | namespace | `krkr::<模块>`，模块名 = `cpp/core/` 下的目录名 | ~~`namespace krkr2`~~ → `krkr::visual` |
| N2 | 文件名 | UpperCamelCase，与其中的主类型/主职责同名；**不带下划线后缀** | `7zArchive.cpp` → `SevenZipArchive.cpp`<br>`combase.h` → `ComBase.h`<br>`MathAlgorithms_Default.cpp` → `MathAlgorithmsDefault.cpp` |
| N3 | 函数 | UpperCamelCase，**动词开头**，**不带 `TVP` 前缀**；归属由 namespace 表达 | `TVPOpenArchive()` → `krkr::io::OpenArchive()` |
| N4 | 类型 | 类/结构体 UpperCamelCase，**不带 `t`/`tTVP` 前缀**；接口 `I` + UpperCamelCase | `tTVPArchive` → `Archive`<br>`iTVPLayerManager` → `ILayerManager` |
| N5 | 宏 | `KRKR_` + UPPER_SNAKE | `TVP_MSG_DEFINE` → `KRKR_MSG_DEFINE`<br>`TVP_WL_MAX_FLAGS` → `KRKR_WL_MAX_FLAGS` |
| N6 | 成员/局部变量 | lowerCamelCase（只要是**动的**代码） | `Flags` → `flags`（只在改到该函数时做，不单独一轮） |
| N7 | namespace 作用域变量 | lowerCamelCase，**不加 `TVP` 前缀**；常量用 UpperCamelCase | `TVPAutoPathList` → `autoPathList`<br>`TVPCurrentMedia` → `currentMedia`<br>`TVPArchiveDelimiter` → `ArchiveDelimiter`（`const`） |

### N3 的动词表

用已有的语义前缀，不发明新的：

| 前缀 | 用于 | 例子 |
|---|---|---|
| `Get` / `Set` | 取/设一个值（可失败用 `TryGet`） | `GetTickCount` `SetVolume` |
| `Is` / `Has` / `Can` | 返回 bool | `IsExistentStorage` `HasOverride` |
| `Add` / `Remove` / `Clear` | 容器/注册表 | `AddAutoPath` `ClearAll` |
| `Create` / `Open` / `Close` / `Free` | 生命周期 | `CreateStream` `OpenArchive` |
| `Load` / `Save` / `Read` / `Write` | I/O | `LoadConfig` `WriteDataToFile` |
| `Convert` / `Parse` / `Format` | 数据变换 | `ConvertToUTF8` `ParseCommandLine` |
| `Throw` / `Report` / `Log` | 错误路径 | `ThrowExceptionMessage` |

**不用的**：`Do` / `Handle` / `Process` / `Perform` 这类空动词；`TVP`/`TVP2` 前缀；
拼音与日式英语（`LoadFileToRead` 之类）。

### N4 的 `t` / `i` 前缀什么时候保留

| 保留 | 原因 |
|---|---|
| `ttstr` `tTJSVariant` `tTJSBinaryStream` `tTJSString` … | tjs2 契约（第三方，见 §4） |
| `tTJSNI_*` / `tTJSNC_*` | tjs2 原生类命名（`NI` = native instance，`NC` = native class），`NCB_*` 宏依赖类名形态 |
| `I` + PascalCase 的接口 | `ILayerManager`：`i` 只是小写化，本契约规定接口用大写 `I` |

**不保留**：`tTVP*`（→ 去掉 `tTVP`，PascalCase）、`iTVP*`（→ `I` + 去掉 `TVP`）。

---

## 2. 归属：一个符号搬进哪个 namespace

判据只有一条：**看它在哪个模块的目录里定义**，不看它名字里带什么。

| 现状 | 目标 |
|---|---|
| `cpp/core/io/` 里定义的 `TVPOpenArchive` | `krkr::io::OpenArchive` |
| `cpp/core/base/core/MsgIntf.h` 里定义的 `TVPThrowExceptionMessage` | `krkr::base::ThrowExceptionMessage` |
| `cpp/core/utils/intf/DebugIntf.h` 里定义的 `TVPAddLog` | `krkr::utils::AddLog` |
| `cpp/core/visual/` 里定义的 `tTVPLayerManager` | `krkr::visual::LayerManager`（visual 未重组，**暂不动**） |

跨模块共用、但没有明显归属的工具（如 `TVPCreateNativeClass_*` 一族）**不猜**：
留在 `base`，等 TJS 类注册表反转（`DECISIONS.md` #9）之后一次性归位。

---

## 3. 桥接（`using`）的去向

命名空间迁移时留下了 **426 条 `using krkr::…;`** 过渡桥接，它们的作用是"调用点不改"。
命名统一之后这些桥接是**矛盾**的：它把旧名字继续留在全局，等于统一没发生。

规则：

1. 某符号改名时，**同一个 commit** 内删掉它对应的 `using` 行；
2. 改成全限定调用（codemod 自动做：模块外用 `krkr::<模块>::NewName`）；
3. 桥接只允许留在这两种情形：
   - 该符号的调用点在 `cpp/plugins/**` 里且本轮不改插件（登记为待清）；
   - 该符号属于 §4 的例外（tjs2 / 生成文件 / SDK）。

`scripts/check_naming.py` 会统计桥接行数与登记去向，只能降不能升。

---

## 4. 例外登记（永不改，或不由本契约管）

| 例外 | 范围 | 原因 |
|---|---|---|
| `cpp/core/tjs2/**` | 全部 | 第三方脚本引擎，`TJS` 命名空间是上游契约 |
| `MsgIntfInc.h` / `MsgImpl.h` 的 343 个消息符号 | `TVPxxx` 全局函数/常量 | `gentext.pl Messages.xlsx` 生成，改了下次生成就回退 |
| `tTJS*` / `iTJSDispatch2` / `ttstr` / `tTVP*` 中**属于 tjs2 的部分** | — | 同上 |
| MIDL / SDK 头 | `kmp_pi.h`、`oldwaveunpacker.h`、`tvpsnd.h`（COM ABI）、`iconv.h`、`utf8.h`、`md5.{c,h}`、`encoding/*.c` | 第三方或生成物，重命名会破坏 ABI/生成链路 |
| `ncbind.hpp` / `ncb_invoke.hpp` / `ncb_foreach.inc` | NCB 宏层 | `NCB_*` 宏族改造是独立一轮（`plugin/README.md`），文件名跟着宏一起动 |
| `tar.h` | TAR 格式结构体 | 与 POSIX `tar.h` 同名，改名要同步所有 `#include`（低优先，可改） |
| **插件可见宿主 API** | `TVP_malloc` `TVP_free` `ZLIB_*` `TVP_md5_*` `TVPDoTryBlock` `TVPRegisterGlobalObject` `TVPRemoveGlobalObject` `TVPRegisteredPlugins` `tvpLoadPlugins` 及 `tTVP{Try,Catch,Finally}BlockFunction` | 这些名字写在 **NCB 宏体**里（`ncbi` / `NCB_*` / `TVP_TRY` 展开成它们），只有和 NCB 宏改造同一批才能改（见 `cpp/core/plugin/README.md` §没做的） |
| **TJS 注册表工厂族** | `TVPCreateNativeClass_*` | 归属要等 `DECISIONS.md` #9 的注册表反转；现在改名会在反转时再改一次 |
| **environ 的全局实现族** | `TVPCreateFolders`（名字归 `krkr::io`）、`TVPConsoleLog`（`stubs` 全局定义）、`TVPCPUType` / `TVPCPUFeatures`（`extern "C"`）、`TVPMainThreadID`、`TVPSetRenderTarget`（定义在 visual） | 都是 AGENTS.md 已登记的"仍定义在全局/别处"债务；改名要先决定它们归哪个 namespace |
| `tTVPApplication` | — | 目标是 `Application`，但全局已经有**指针变量** `Application`（AGENTS.md 例外）——两者同名会让 namespace 内的 `Application->…` 全部失效 |
| `TVPMainForm` | 6 处，**只出现在注释里** | 指向已被删除的 Win32 主窗口，符号本身已不存在（`DebugIntf.cpp` 的 `/* */` 死代码块 + `DebugImpl.cpp` 的 `//` 注释）。没有"真实名字"可改，留着当历史叙述 |
| `7zArchive.cpp` | — | **不是例外**：它属于 N2，目标 `SevenZipArchive.cpp` |
| `*.tjs` 资源 | `resources/` | 脚本资源，不是 C++ |

---

## 5. 执行顺序（一个模块一个 commit）

从"符号少、调用面小"往"符号多、调用面广"推，每步都能单独回滚：

| 轮次 | 模块 | 自有遗留符号 | 状态 |
|---|---|---:|---|
| 1 | `extension` | 4 | ✅ **已完成**（`TVPAddClassHandler`→`AddClassHandler`、`TVPCauseAtInstallExtensionClass`→`InstallRegisteredClasses`、`TVPResetExtensionClassInstallStateForRestart`→`ResetInstallStateForRestart`、`tTVPAtInstallClass`→`AtInstallClass`；4 条过渡 `using` 同 commit 删除） |
| 2 | `plugin` | 23（已应用） | ✅ **插件管理函数 / 类型 / 宏完成**（68 处 / 7 文件，49→29）；余下 29 个全是登记例外（插件 ABI、NCB 宏类型、注册表工厂族）与别的模块的符号 |
| 3 | `utils` | 37 + 19（已应用） | ✅ **收口**：`utils.tsv` 32 符号 / 303 处 / 53 文件 + `utils-residual.tsv` 19 符号 / 383 处 / 62 文件 + 手工 `TVPTimer`→`Timer` 22 处；遗留 58→**8**（4 个 `TVPCreateNativeClass_*` 例外 + `TVPMainForm` 注释残留 + 3 个别的模块的符号） |
| 4 | `io` | 123 + 27 + 17（已应用） | ✅ **io 收口**：第 2 轮 1023 处 / 83 文件（`7zArchive.cpp` → `SevenZipArchive.cpp`）、第 4 轮 189 处 / 11 文件（`io-globals.tsv`，27 个 N7 变量）、第 4b 轮 45 处 / 3 文件（`io-residual.tsv`，9 个 `--draft` 漏收符号 + 6 处注释 + 5 处日志串）；遗留 176→**26**，余下**全部是别的模块的符号**（base 13 / utils 6 / environ 5）+ `TVPCreateNativeClass_Storages`（§4 例外） |
| 5 | `compat` | 0 | ✅ 自有符号已是新风格（`--draft` 扫出 0 个），无需改名 |
| 6 | `sound` | 127 + 64（已应用） | ✅ **收口**：`sound.tsv` 127 符号 / 893 处 / 36 文件 + `sound-residual.tsv` 64 符号 / 434 处 / 9 文件（N7 变量、匿名 struct/enum 实例、多行定义）+ 2 处注释；遗留 211→**20**（全部是 base/environ/movie 的符号 + 4 个例外）。同时修正了消息符号口径：`MsgImpl.h` 里独有的 ~40 个 `tTJSMessageHolder` 以前被当成遗留符号 |
| 7 | `environ` | 133 + 残项（已应用） | ✅ **收口**：三张 map（按**声明所在的 namespace** 切：`krkr::environ` / `krkr::base` / 全局）+ 残项；遗留 247→**98**（余下全是 base/visual 的符号与 §4 例外）。新增工具能力 `kind=global`：environ 多数符号名字就活在全局作用域，加 `krkr::environ::` 前缀会变成另一个符号 |
| 8 | `base` | 65 + 240（已应用） | ✅ **收口**：按**声明所在 namespace** 切两张 map（`base.tsv` `krkr::base` 65 个 / `base-globals.tsv` `kind=global` 240 个）；遗留 465→**30**（余下全是 `TVPCreateNativeClass_*`、插件 ABI、visual/environ/movie 的符号与 §4 例外）。消息符号 153 个永久例外 |
| 9 | `visual` / `movie` | — | 这两块结构未动；它们的**调用点**会在 1–8 里被 codemod 顺带更新 |

每轮的完成判据（三条都要）：

1. `scripts/check_naming.py` 的该模块遗留计数 → 0（或只剩登记例外）；
2. 该模块不再有 `using krkr::…::OldName;` 桥接；
3. `grep -rn "\bOldName\b"` 在整个 `cpp/` 与 `bridge/` 里为 0（调用点已全限定）。

---

## 6. 工具

```bash
# 存量 ratchet：有任何模块的遗留计数比基线高就失败（不需要编译器）
python3 scripts/check_naming.py
python3 scripts/check_naming.py --module io --verbose      # 看该模块还剩哪些符号
python3 scripts/check_naming.py --update-baseline          # 只能往下调

# 机械改名：map 驱动，默认 dry-run
python3 scripts/rename_symbols.py --map scripts/rename/maps/extension.tsv
python3 scripts/rename_symbols.py --map scripts/rename/maps/extension.tsv --apply
```

map 文件是**可审阅的产物**（`scripts/rename/maps/<模块>.tsv`）：

```
# module: extension
# qualify: krkr::extension
# owner: cpp/core/extension
TVPAddClassHandler	AddClassHandler	fn
tTVPAtInstallClass	AtInstallClass	type
```

- `owner/` 里的文件替换成短名（`AddClassHandler`），其它地方替换成全限定名
  （`krkr::extension::AddClassHandler`）—— 全限定在 C++ 里总是合法，
  所以 codemod **不需要理解调用点所在的 namespace**。
- `macro` 类条目不做限定（宏没有 namespace）。
- `global` 类条目同样不做限定，用于**名字本身活在全局作用域**的符号：`environ` 的
  多数函数/变量都是这种（声明在 base/visual 的全局头里、实现在 environ 全局作用域）。
  这类符号如果被写成 `krkr::environ::X`，在调用点就是"另一个符号"，链接期直接找不到。
- `--apply` 之前会做四件事：新名重复检测、新名已存在检测、旧名零命中检测、
  注释/字符串命中统计。

## 7. 不做什么

| 不做 | 原因 |
|---|---|
| 改 `tjs2/` 的任何名字 | 第三方契约 |
| 改 343 个消息符号 | 生成文件 |
| 同一轮里既改名又改结构 | 失败时无法区分是"名字错"还是"结构错"（这条已经踩过一次） |
| 改成员/局部变量的命名风格 | 收益小、diff 大；只在真正改到该函数时顺手做（N6） |
| 为了"整齐"把 `d3d`/`gl` 这类公认缩写展开成 `Direct3D`/`OpenGL` | 缩写表见 §8 |

## 8. 缩写表（保持原样，不展开）

`GL` `EGL` `D3D` `JNI` `TJS` `XP3` `KAG` `CDDA` `MIDI` `FFT` `DSP` `SSE` `AVX`
`ZIP` `TAR` `7Z`（类型名里写 `SevenZip`） `UTF8` `UTF16` `MD5` `CRC32`
`PSB` `MTN` `LZ4` `ZSTD` `OBOE` `SDL` `GPU` `CPU` `IO` `UI` `NCB` `AGF`

> `IO` 例外：`namespace krkr::io` 用小写（namespace 保持全小写）；
> 类型名里用 `Io`（`IoPath` `IoVirtualFile`），与现有代码一致。

---

## 8.5 编译验证状态（2026-11-15）

文本层面 8 个模块已收口（合计遗留 **132**），**但 Linux 宿主构建尚未通过**。

- 构建环境已搭好：WSL2 Ubuntu-22.04 + cmake 3.31 + ninja + g++，`cmake --preset
  "Linux Debug Config"` 跑通（127 个 vcpkg 包），`out/linux/debug/build.ninja` 已生成；
  复现命令与踩坑（`fribidi` 手工下载、`libgdiplus` 缺 X11、WSL 回收后台进程）见
  `cpp/DECISIONS.md` 的「编译验证 #1」。
- 首次 `ninja -k 0` 到 195/873 就有 **7305 行 error / 148 个 TU 失败**，其中 90% 是
  一个根因的级联：`base-globals.tsv` 里 62 条其实声明在 `namespace krkr::base` 的符号
  被按"全局裸名"改写（`ThrowExceptionMessage` 一个就 1615 行）。
- 已修两处独立事故（`6b513e4`）：`MemoryStream` 的跨 namespace 误改、
  `DrawFace`/`HitType` 与成员变量撞名。
- 下一步：回退 base 轮 → 修分类脚本（认 `TJS_EXP_FUNC_DEF`/`extern` 声明）→ 重新 apply
  → 重编。命令见 DECISIONS「编译验证 #1」的 A 方案。

> 结论：**"模块收口" ≠ "能编译"**。收口只是把遗留名字降下来了，编译验证是独立的一关，
> 而且这一关会反过来改 map（这次就是）。

## 9. 工作量估算（基于前 4 轮实测）

| 轮次 | 模块 | 符号 | 替换处 | 文件 | 耗时 |
|---|---|---:|---:|---:|---|
| 1 | `extension` | 7 | 21 | 4 | ~15 min（含工具摸索） |
| 2 | `io` | 123 | 1023 | 83 | ~50 min（含 4 处工具修正） |
| 3 | `plugin` | 23 | 68 | 7 | ~15 min |
| 4 | `io` 全局变量（N7） | 27 | 189 | 11 | ~30 min |
| 4b | `io` 残项（draft 漏收） | 9（+11 注释/日志） | 45 | 3 | ~15 min |
| 5 | `utils`（含 5b 残项） | 32 + 19 + 1 手工 | 686 + 70 去重限定 | 90 | ~70 min（含新检查脚本） |
| 6 | `sound`（含 6b 残项） | 127 + 64 + 2 注释 | 1327 | 39 | ~80 min |
| 7 | `environ`（三张 map + 残项） | 125 + 8 | 836 | 76 | ~90 min（含 `kind=global`） |
| 8 | `base`（两张 map） | 305 | 3274 | 256 | ~100 min |

**吞吐 ≈ 100~150 符号/小时**（工具成熟后，不含编译回归）。

| 剩余模块 | 自有符号（draft 口径） | 纯改名 | 含编译回归 |
|---|---:|---|---|
| ~~`io` 剩余（N7 全局变量）~~ | ~~28~~ | ✅ 第 4 轮完成 | — |
| ~~`utils`~~ | ~~37~~ | ✅ 第 5 轮完成 | — |
| ~~`sound`~~ | ~~132~~ | ✅ 第 6 轮完成 | — |
| ~~`environ`~~ | ~~134~~ | ✅ 第 7 轮完成 | — |
| ~~`base`~~ | ~~250~~ | ✅ 第 8 轮完成 | — |
| ~~`io` 残项~~ | ~~13~~ | ✅ 第 4b 轮完成（9 符号 + 11 处注释/日志） | — |

结论：**纯改名还剩 5~7 小时**（再 5 轮左右）；
**含编译验证 1.5~2 周**；加上仓库里积压的未编译提交的首次全量验证（3~5 天），
**总计 2~3 周**（不含 `visual` / `movie`，它们再 +1.5~2 周）。

两个不确定性会影响工期：
1. **宏的 token pasting**（`TVP_GL_FUNC_DECL` 一族 282 处）—— 若有 `TVP##name` 拼接，
   改名只能在编译期暴露；
2. **插件 ABI 是否解除例外** —— 解除就要和 NCB 宏改造同批做，工期 +1~2 天。
