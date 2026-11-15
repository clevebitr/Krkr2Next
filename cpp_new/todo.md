# cpp_new — 引擎入口重写（交接 / TODO）

> 目标：在 `cpp_new/` 里从零写一个干净的引擎核心，不 include 老树（`cpp/core/**`）的任何头，
> 长到能跑之后一次性替换 `bridge/engine_api/` 里的实现。
> 老树是参考物和需求书，**不是**依赖。

---

## 0. 一句话现状

入口容器已立起来：**能创建、能开路径、能跑帧、能销毁**，错误路径也通。
还没有文件系统校验、日志、线程、存储、脚本、渲染 —— 全部是空壳。

---

## 1. 现在能跑什么

```bash
cd /storage/emulated/0/Download/AppProjects/Krkr2Next

clang++ -std=c++20 -Wall -Wextra -Wpedantic -I cpp_new \
  cpp_new/host/main.cpp cpp_new/core/env/app/EngineRuntime.cpp \
  -o $TMPDIR/krkr_next

$TMPDIR/krkr_next <game_dir>
```

只用 Termux 里的 `clang++`，**不需要 NDK / cmake / Gradle / 手机**。反馈时间 < 10 秒。

已验证用例（2024-11 本轮）：

| # | 输入 | 结果 | 判定 |
|---|---|---|---|
| 1 | `games/demo`（目录） | `root="games/demo" kind=0 frames=180` rc=0 | ✅ 符合预期 |
| 2 | `games/demo/` | 原样回显 | ✅ 符合预期 |
| 3 | `games/demo/../demo` | 原样回显 | ✅ 符合预期 |
| 4 | `games/nope`（不存在） | **被当成合法路径接受** | ⚠️ 第 3 步要挡 |
| 5 | `GAME.XP3` | **kind=0** | ⚠️ 第 3 步要识别 |
| 6 | `readme.txt` | **被接受** | ⚠️ 第 3 步要挡 |
| 7 | `""` | `-1 game root is empty` rc=1 | ✅ |
| 8 | 无参数 | usage rc=2 | ✅ |
| 9 | 180 帧后 | `frames=180` | ✅ |

第 4/5/6 条不是 bug，是**第 3 步还没做**。这就是下一步的全部内容。

---

## 2. 文件约定

```
cpp_new/
├── host/main.cpp                     本地测试台 = 未来的 Kotlin 宿主（只用来启动，不进引擎）
└── core/env/app/
    ├── EngineRuntime.h               入口契约（不透明句柄 + 状态机）
    └── EngineRuntime.cpp             实现
```

- `host/main.cpp` **不属于引擎**，它扮演的是 `NativeEngine.kt` 的角色。引擎永远是库，不是可执行文件。
- 引擎内部的所有辅助函数写进 **匿名 namespace**（文件私有），不要新增全局符号。
- ⚠️ 目录名 `env` 与老树 `environ` 不一致，namespace 却叫 `krkr::environ`（`docs/NAMING.md` N1：
  namespace = 目录名）。要么把目录改成 `environ`，要么把 namespace 改成 `krkr::env`。**第 3 步顺手定下来。**

---

## 3. 总计划

| # | 步骤 | 内容 | 状态 |
|---|---|---|---|
| 1 | 入口容器 | `Create / OpenGame / Tick / Destroy` + 不透明句柄 + 返回码 | ✅ 完成 |
| 2 | 本地测试台 | `host/main.cpp`，clang++ 直编，9 条用例 | ✅ 完成 |
| 3 | **文件系统校验** | 存在性 / 目录 vs 包 / 路径规范化 / `GameKind` | ⬅ **下一步** |
| 4 | 日志 | 级别 + 帧号 + 落盘，替掉散在 `engine_api_lifecycle.cpp` 的 `spdlog` 调用 | 待做 |
| 5 | 异步启动 | worker 线程 + `StartupState` 状态机（`RUNNING/SUCCEEDED/FAILED`） | 待做 |
| 6 | 存储层 | 目录读取 + XP3 索引解析（`OpenArchive` / `FindFile`） | 待做（大） |
| 7 | 脚本虚拟机 | 接 TJS2，跑 `startup.tjs` | 待做（大） |
| 8 | 渲染 | EGL + GLES3 surface，一帧提交给宿主（零拷贝 SurfaceTexture） | 待做（大） |
| 9 | 输入 | 指针 / 按键 / 文本 → 脚本事件 | 待做 |
| 10 | 音频 | 至少 WAVE + OGG | 待做 |
| 11 | 换回去 | 用 `cpp_new` 替换 `bridge/engine_api/` 的实现，签名不变，Kotlin 不动 | 待做 |

**节奏约定**：每次只做一行，做完能编、能跑、能贴出输出，再进下一行。不许并行铺开。

---

## 4. 下一步（第 3 步）详细清单

**目标**：让第 1 节的第 4/5/6 条从 ⚠️ 变成 ✅。

### 4.1 头文件改 3 处

1. `enum class Result` → 加 `IoError = -4;`（与真 ABI 的 `ENGINE_RESULT_IO_ERROR = -4` 对齐）
2. 新增 `enum class GameKind : std::int32_t { None=0, Directory=1, Archive=2 };`
3. `EngineRuntime` 里加 `GameKind gameKind_ = GameKind::None;`，
   加 getter `GetGameKind()`、`GetGameRoot()`，加 `#include <cstdint>`（现在靠 `<string>` 传递包含，不可靠）

### 4.2 实现改 `OpenGame`

```cpp
#include <cctype>
#include <filesystem>
#include <system_error>
```

1. `std::error_code ec`；**所有 `fs::` 调用都传 `ec`，绝不使用抛异常的重载**
2. `fs::exists(requested, ec)` 失败 → `IoError`（消息带原路径）
3. `fs::weakly_canonical(requested, ec)` 规范化；失败降级 `fs::absolute`；再失败用原路径
4. `fs::is_directory(resolved, ec)` 为真 → `GameKind::Directory`，路径末尾补 `/`
5. 否则看扩展名（`.xp3/.zip/.7z/.tar`，**大小写不敏感**）→ `GameKind::Archive`
6. 都不是 → `IoError`，消息写清「既不是目录也不是已知包格式」
7. `resolved.generic_string()`（不是 `string()`）——永远产出 `/`，跨平台拼路径才安全

### 4.3 三个必须遵守的细节

| 细节 | 为什么 |
|---|---|
| `std::tolower(static_cast<unsigned char>(c))` | `char` 在 ARM 上有符号，UTF-8 中文字节是负数，传给 `<cctype>` 是**未定义行为** |
| `weakly_canonical` 而不是 `canonical` | 前者不要求路径存在；后者要求，且对包文件路径更苛刻 |
| 不要手写 `ends_with(".xp3") \|\| ends_with(".XP3")` | 老代码就是这么写的，只认两种大小写，`game.Xp3` 会漏。写成 `EqualsIgnoreCase(ext, ".xp3")` |

### 4.4 验收标准

```bash
cd /storage/emulated/0/Download/AppProjects/Krkr2Next
clang++ -std=c++20 -Wall -Wextra -Wpedantic -I cpp_new \
  cpp_new/host/main.cpp cpp_new/core/env/app/EngineRuntime.cpp -o $TMPDIR/krkr_next || exit 1

mkdir -p $TMPDIR/games/demo && : > $TMPDIR/games/demo/GAME.XP3 && : > $TMPDIR/games/demo/readme.txt

$TMPDIR/krkr_next $TMPDIR/games/demo            # 绝对路径，kind=1，末尾有 /
$TMPDIR/krkr_next $TMPDIR/games/demo/           # 与上一条输出完全一致
$TMPDIR/krkr_next $TMPDIR/games/demo/../demo    # 与上一条输出完全一致（.. 被吃掉）
$TMPDIR/krkr_next $TMPDIR/games/nope            # -4 does not exist
$TMPDIR/krkr_next $TMPDIR/games/demo/GAME.XP3   # kind=2（大写扩展名也要认）
$TMPDIR/krkr_next $TMPDIR/games/demo/readme.txt # -4 既不是目录也不是已知包
$TMPDIR/krkr_next ""                            # -1 空串
```

**关键判据**：第 1/2/3 条输出必须**逐字相同**（都变成绝对路径 + 尾斜杠）。不相同就是规范化没接对。

---

## 5. 已知问题 / 技术债

| # | 问题 | 处理 |
|---|---|---|
| 1 | `main.cpp:25` 少一个右括号，编译失败 | 已修（本轮） |
| 2 | 头文件里 `#include <cstdio>` 是无用的（头文件里没有打印） | 第 3 步删掉，挪到 `main.cpp` |
| 3 | 函数定义后多余的 `;`（`{ return new EngineRuntime(); };`） | 合法但误导，第 3 步顺手清 |
| 4 | `EngineRuntime.h` 缺 `#include <cstdint>`，靠 `<string>` 传递包含 | 第 3 步补 |
| 5 | 目录 `core/env/app/` 与 namespace `krkr::environ` 不一致 | 第 3 步定方向 |
| 6 | `core/base/core/src/krkrMsgImpl.cpp` 是老树消息系统的搬运草稿，include 了 `tjsCommHead.h` 等**老树头**，在 `cpp_new` 里编不过 | 保留，**不要接进任何构建**，第 7 步处理 |
| 7 | `tests/` 里没有 `cpp_new` 的 CI | 第 4 步有日志后加一个 smoke 脚本 |

---

## 6. 铁律（踩了要回滚）

1. **不 include 老树任何头**（`cpp/core/**`）。一 include `tjsCommHead.h` 就被 337 个 extern 全局绑架。
2. **不新增全局符号**。一切进 `namespace krkr::<模块>`；辅助函数进匿名 namespace。
3. **不使用 `using namespace std;`**。一律 `std::`。
4. **入口层不抛异常**。跨 JNI / `extern "C"` 抛异常是未定义行为。全部用返回码 + `GetLastError()`。
5. **返回码粗、消息细**。码跟 `engine_api.h` 对齐（`0/-1/-2/-3/-4/-5/-6/-7`），人话写进 `lastError_`。
6. **内部 `std::string`，边界 `const char*`**。UTF-8 进、UTF-8 出，中途不转宽字符。
7. **`new` 必须配 `Destroy`**。没有 GC。
8. **公开函数第一行检查参数**（`nullptr` / 空串），JNI 那边真的会传空指针进来。

---

## 7. 与老树的关系

替换时**只换实现，不换签名**。契约是这三个文件，改动会被 Kotlin 侧立刻感知：

- `bridge/engine_api/include/engine_api.h`（C ABI，是需求书，**不许改**）
- `bridge/engine_api/src/engine_api_android_jni.cpp`（JNI 薄壳，保持不动）
- `app/app/src/main/kotlin/org/dpdns/clevebitr/core/NativeEngine.kt`（宿主，保持不动）

老树里值得抄的参考点：

| 参考 | 位置 | 抄什么 |
|---|---|---|
| 宿主路径规范化 | `bridge/engine_api/src/engine_api_lifecycle.cpp` `OpenGameCore` | 尾斜杠约定、包类型判断（**但大小写处理有 bug，见 §4.3**） |
| 启动状态机 | `engine_api.h` 的 `engine_startup_state_t` / `ENGINE_RESULT_STARTUP_PENDING` | 第 5 步的验收形状 |
| 生命周期 | `cpp/core/environ/app/Application.cpp` | 启动顺序（**不要抄 1062 行的结构**） |
