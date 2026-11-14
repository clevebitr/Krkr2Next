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
| `7zArchive.cpp` | — | **不是例外**：它属于 N2，目标 `SevenZipArchive.cpp` |
| `*.tjs` 资源 | `resources/` | 脚本资源，不是 C++ |

---

## 5. 执行顺序（一个模块一个 commit）

从"符号少、调用面小"往"符号多、调用面广"推，每步都能单独回滚：

| 轮次 | 模块 | 自有遗留符号 | 状态 |
|---|---|---:|---|
| 1 | `extension` | 4 | ✅ **已完成**（`TVPAddClassHandler`→`AddClassHandler`、`TVPCauseAtInstallExtensionClass`→`InstallRegisteredClasses`、`TVPResetExtensionClassInstallStateForRestart`→`ResetInstallStateForRestart`、`tTVPAtInstallClass`→`AtInstallClass`；4 条过渡 `using` 同 commit 删除） |
| 2 | `plugin` | 23（已应用） | ✅ **插件管理函数 / 类型 / 宏完成**（68 处 / 7 文件，49→29）；余下 29 个全是登记例外（插件 ABI、NCB 宏类型、注册表工厂族）与别的模块的符号 |
| 3 | `utils` | 14（draft） | 🟡 map 已起草；4 个新名冲突待人工处理（`TVPTimer`→`Timer` 撞名） |
| 4 | `io` | 123（已应用） | ✅ **函数/类型/宏已完成**（1023 处替换 / 83 文件，`7zArchive.cpp` → `SevenZipArchive.cpp`）；遗留 176→70，余下是**跨模块符号**（base/utils 的，各轮处理）与 **N7 全局变量**（`io-globals.tsv` 待做） |
| 5 | `compat` | 0 | ✅ 自有符号已是新风格（`--draft` 扫出 0 个），无需改名 |
| 6 | `sound` | 112（draft） | 🟡 map 已起草 |
| 7 | `environ` | 101（draft） | 🟡 map 已起草 |
| 8 | `base` | 465（基线） | ⬜ 未起草；先做非消息部分，消息符号永久例外 |
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

## 9. 工作量估算（基于前 3 轮实测）

| 轮次 | 模块 | 符号 | 替换处 | 文件 | 耗时 |
|---|---|---:|---:|---:|---|
| 1 | `extension` | 7 | 21 | 4 | ~15 min（含工具摸索） |
| 2 | `io` | 123 | 1023 | 83 | ~50 min（含 4 处工具修正） |
| 3 | `plugin` | 23 | 68 | 7 | ~15 min |

**吞吐 ≈ 100~150 符号/小时**（工具成熟后，不含编译回归）。

| 剩余模块 | 自有符号（draft 口径） | 纯改名 | 含编译回归 |
|---|---:|---|---|
| `io` 剩余（N7 全局变量） | 28 | ~0.5 h | ~0.5 天 |
| `utils` | 37 | ~0.5 h | 0.5~1 天 |
| `sound` | 132 | ~1.5 h | 1~2 天 |
| `environ` | 134 | ~1.5 h | 1~2 天 |
| `base` | ~250（不含 153 个消息例外） | ~3 h | 2~4 天（注册表族要等反转） |

结论：**纯改名还剩 6~8 小时**（再 5 轮，与我这两轮同量级）；
**含编译验证 1.5~2 周**；加上仓库里积压的未编译提交的首次全量验证（3~5 天），
**总计 2~3 周**（不含 `visual` / `movie`，它们再 +1.5~2 周）。

两个不确定性会影响工期：
1. **宏的 token pasting**（`TVP_GL_FUNC_DECL` 一族 282 处）—— 若有 `TVP##name` 拼接，
   改名只能在编译期暴露；
2. **插件 ABI 是否解除例外** —— 解除就要和 NCB 宏改造同批做，工期 +1~2 天。
