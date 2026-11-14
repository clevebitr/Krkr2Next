# core/sound —— 四层，目录名 = target 名

`sound` 曾经是 27 个文件的扁平目录 + 一个 `win32/` 子目录 + 一个 STATIC target。
其中 `win32/` **在 Android 上也全部参与编译**，目录名和事实相反；而"哪些头是
对外契约、哪些是实现"只能靠读 27 个文件判断。现在按依赖层次切成四个目录：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `common/` | `core_sound_common` | 纯数据与纯算法：`RingBuffer.h`、`WaveSegmentQueue.*`、`WaveLoopManager.h`、`xmmlib.*`、`MathAlgorithms.*`、`WaveFormatConverter*.cpp` | 只依赖 `tjs2` |
| `intf/` | `core_sound_intf` | TJS 接口类：`WaveIntf` / `SoundBufferBaseIntf` / `CDDAIntf` / `MIDIIntf`（+ `WaveLoopManager.cpp`） | `common` + base |
| `codec/` | `core_sound_codec` | 解码器与 DSP：`FFWaveDecoder` / `VorbisWaveDecoder` / `PhaseVocoder{DSP,Filter}` | `intf` |
| `impl/` | `core_sound_impl` | 平台实现（**原 `win32/`**）：`WaveImpl` / `WaveMixer` / `SoundBufferBaseImpl` / `CDDAImpl` / `MIDIImpl` / `tvpsnd` | `intf` + `codec` + base + environ |

`core_sound_module` 是一个 **INTERFACE 聚合层**，把四个簇串起来。`base` / `visual` /
`movie` / `environ` 里写的 `core_sound_module` 一个字都没改。

## 为什么 `win32/` 改名成 `impl/`

TODO.md 的"命名误导目录"一条点名的就是它：`sound/win32/` 里的 `WaveImpl.cpp`、
`WaveMixer.cpp`、`SoundBufferBaseImpl.cpp` 在 Android 上照常编译，`WaveMixer.cpp`
甚至就是 Android 的低延迟音频后端（oboe）。真正接近 Windows-only 的只有
`tvpsnd.cpp`（MIDL 生成的 COM ABI），而它也在构建里。

`impl/` 表达的是"这些是接口背后的实现"，和平台无关。`core/utils/win32/`、
`core/environ/win32/` 的同名问题**没有一起改**：它们的文件里确实大部分是
Windows 语义，要逐个确认才能动（见 `docs/STRUCTURE_DESIGN.md` §7）。

## 一条规矩：头在下层，实现在上层

`WaveLoopManager.h` 在 `common/`，`WaveLoopManager.cpp` 在 `intf/` ——
因为它的实现要 `tTVPWaveFormat`（`WaveIntf.h` 提供），而头文件只用到
`WaveSegmentQueue`。这不是失误，和 base 的 `EventIntf.h`(core/) +
`EventIntf.cpp`(runtime/) 是同一套分工：**判断标准只有一条 —— 这个文件 include 谁。**

## 已知的环：intf ↔ impl（本轮没有解决，也不该在没有编译器时解决）

四个接口头在**文件中段**include 了它的实现头：

| 位置 | 语句 | 原因 |
|---|---|---|
| `intf/WaveIntf.h:222` | `#include "WaveImpl.h"` | 紧随其后声明的 `tTJSNC_WaveSoundBuffer` 以 `tTJSNI_WaveSoundBuffer` 为基类 |
| `intf/SoundBufferBaseIntf.h:101` | `#include "SoundBufferBaseImpl.h"` | 同上 |
| `intf/CDDAIntf.h:37` | `#include "CDDAImpl.h"` | 同上 |
| `intf/MIDIIntf.h:43` | `#include "MIDIImpl.h"` | 同上 |

反向的边是 `impl/*.h` 里各自 include 回自己的接口头
（`WaveImpl.h:22`、`SoundBufferBaseImpl.h:17`、`CDDAImpl.h:15`、`MIDIImpl.h:16`）。
结果就是**接口头不能脱离实现头单独存在**：任何 include `WaveIntf.h` 的文件
（`base/script/ScriptMgnIntf.cpp`、`plugins/wutcwf.cpp`）都顺带把 `impl/` 拖进来，
`intf → impl` 的依赖方向被锁死。

拆法（需要编译器，单独一轮）：

1. 把 `tTJSNI_*` 的**声明**留在 `intf/`（纯虚基类 + 工厂声明），
   把 `tTJSNC_*` 的构造/成员表移到 `intf/*.cpp`；
2. `tTJSNC_*` 的 `CreateNew` 通过一个 `tTVPSoundObjectFactory` 注入点拿实现对象，
   注入在 `impl/` 里做（这套"注入口"模式与 `DECISIONS.md` #9 的 TJS 类注册表
   反转是同一个模板）；
3. 做完之后 `intf/` 不再 include `impl/`，`codec` 也不再有环。

第二条环：`intf/WaveIntf.cpp` 要 `FFWaveDecoder.h` / `VorbisWaveDecoder.h`（codec），
而 `codec/*.h` 要 `WaveIntf.h`（intf）。同样是"契约拿不到具体解码器"的问题，
和解码器注册表一起拆。

## 加一个新文件放哪

1. 只是数据结构 / 纯算法（环形缓冲、循环标记、格式转换、DSP 内核）→ `common/`
2. 是 TJS 可见的类，或外部模块要 include 的契约头 → `intf/`
3. 解一个新格式（opus / ADX / …）或新 DSP → `codec/`，并让 `intf` 通过注册表拿它
4. 要 `Application` / `SystemControl` / `NativeEventQueue` / 音频设备 → `impl/`
5. **不要**再往 `sound/` 根目录加文件：根目录现在只剩 `CMakeLists.txt` 和本文件

## 技术债（本轮没动）

- **`MathAlgorithms` 有两份声明**：`common/MathAlgorithms.h` 与
  `utils/MathAlgorithms.h` 内容重复（`AGENTS.md` §2.2 已登记为有意保留的例外）；
  `common/MathAlgorithms.cpp` 因此**不在构建里**（定义由
  `core_utils_module/MathAlgorithms_Default.cpp` 提供）。
- **`xmmlib.cpp` / `WaveFormatConverter_SSE.cpp` 不在构建里**：历史上被注释掉，
  没有编译验证前不打开（CMakeLists 注释里保留原样）。
- **公共头仍是扁平面**：`include` 写的是裸文件名（`"WaveIntf.h"`），靠五条
  `-I`（含模块根）解析。显式路径（`sound/intf/WaveIntf.h`）是下一步的调用点迁移。
- **`sound → movie` 的半解**：头依赖已清零（`TVPInitLibAVCodec` 就地声明），
  **链接期仍在调 movie 的实现**，所以聚合层保留了 `core_movie_module`。
- **`tvpsnd.cpp` 只有一句 `#include "combase.h"`**：真正的 COM 实现不在这个仓库里，
  它是 ABI 占位。哪天确认 Android 不需要它，可以整簇移出构建。

## 验收（有编译器之后）

```bash
scripts/check_tu.sh --module sound
bash scripts/check_static.sh
llvm-nm -C --defined-only libengine_api.so | grep 'krkr::sound::'
```

行为对齐点：`WaveSoundBuffer` / `SoundBuffer` / `MIDISoundBuffer` / `CDDASoundBuffer`
四个 TJS 类是否齐全（`TJS_MEMBERENSURE` 顺序），以及 `plugins/wutcwf.cpp` 的
wave filter 注册。搬迁不改变任何一个成员表，出现差异说明是 include 路径问题。
