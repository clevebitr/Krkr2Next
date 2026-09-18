/**
 * @file engine_options.h
 * @brief Well-known engine option key/value constants.
 *
 * These constants unify the option strings used across the C++ codebase
 * (engine_api, EngineBootstrap) to avoid typos and ensure consistency.
 */
#ifndef KRKR2_ENGINE_OPTIONS_H_
#define KRKR2_ENGINE_OPTIONS_H_

/* ── Option Keys ────────────────────────────────────────────────── */

/**
 * 已废弃：ANGLE 已从本项目移除，渲染走平台原生 EGL/GLES。
 * 该键仍被接受但取值被忽略（返回 OK），以免旧设置文件导致初始化失败。
 */
#define ENGINE_OPTION_ANGLE_BACKEND "angle_backend"

/** Frame rate limit (0 = unlimited / follow vsync). */
#define ENGINE_OPTION_FPS_LIMIT "fps_limit"

/**
 * 字体回退策略（`auto` / `legacy` / `chain`）。见
 * cpp/core/visual/FreeTypeFontRasterizer.h 的 FontFallbackMode。
 * `legacy` = 原版派系（单一回退字面）；
 * `chain` = AetherKiri 派系（逐字回退链）。
 * 引擎只在字体初始化时读一次，所以"下次开游戏生效"。
 */
#define ENGINE_OPTION_FONT_FALLBACK_MODE "font_fallback_mode"

/**
 * krkrz（吉里吉里Z）的 OGLDrawDevice 兼容层，见 cpp/plugins/krkrgles.cpp。
 *
 * krkrgles 系游戏的 Initialize.tjs 会先看 `Window.OGLDrawDevice` 存不存在，
 * 再决定要不要加载 GPU 层脚本（`GPULayer.tjs` / `GPUAffineLayer.tjs`）。缺了它，
 * 游戏不报错、只是静静降级，表现出来就是"有声音、画面黑"。
 *
 * 取值：
 *  - `off`  ：不提供（默认，保持既有行为）
 *  - `ogl`  ：只挂 `Window.OGLDrawDevice`
 *  - `alias`：挂 `Window.OGLDrawDevice` + `Window.GLESAdaptor`
 *  - `kag`  ：在 `alias` 之上再接管 `KAGWindow_createDrawDevice`
 *
 * 各档作用不同，**必须逐游戏试**：
 *  - `Window.OGLDrawDevice` 是闸门：游戏 Initialize.tjs 探测到它才会加载
 *    `GPULayer.tjs` / `GPUAffineLayer.tjs`（实测会话 11:22 首次出现）。
 *  - `Window.GLESAdaptor` 会改变部分游戏行为：千恋万花在 `alias` 档下被切进
 *    motionplayer 的 `captureCanvas` 路径，而 `Player::draw` 从此让路、UI 图全靠
 *    那条交付——实测不完整、UI 出问题。`ogl` 就是给这种游戏的。
 *  - `kag` 解决窗口绘制设备工厂：**千恋万花**在此档下立绘与背景动态正常；
 *    但对 G2（nainiuniu5krkr）会把主机 FBO 弄成 `INCOMPLETE 0x8CD6`（53 次），
 *    画面直接采不到（连回想页都黑），所以不要默认开。
 *
 * 由 krkrgles 插件在注册完成时（post-regist）读一次，所以"下次开游戏生效"。
 */
#define ENGINE_OPTION_OGLDRAWDEVICE_COMPAT "ogldrawdevice_compat"

#define ENGINE_OGLDRAWDEVICE_COMPAT_OFF "off"
#define ENGINE_OGLDRAWDEVICE_COMPAT_OGL "ogl"
#define ENGINE_OGLDRAWDEVICE_COMPAT_ALIAS "alias"
#define ENGINE_OGLDRAWDEVICE_COMPAT_KAG "kag"

/**
 * 游戏兼容档（compat profile）—— 两条血脉的差异收敛点。
 *
 * 本项目要同时支持**老 KiriKiri2 系**（Kirikiroid2 血脉：旧 API 面、kr2ext 那套
 * 容器/图像/音频）与 **krkrz / AetherKiri 系**（GPU 层、OGLDrawDevice、
 * companion script、宽插件集）。差异**不允许**写成"某个游戏就怎样"的分支，而是
 * 收敛成这里的**具名 + 带版本号**的档：
 *
 *   kirikiri2-classic  v1  → ogldrawdevice_compat=off
 *   krkrz-gpu          v1  → ogldrawdevice_compat=alias
 *   krkrz-kag          v1  → ogldrawdevice_compat=kag
 *   krkrz-ogl          v1  → ogldrawdevice_compat=ogl
 *   aetherkiri         v1  → ogldrawdevice_compat=kag，**并激活 AetherKiri 兼容层**
 *
 * `aetherkiri` 是唯一会**切换兼容层**的档（其余档只影响渲染侧选项，兼容层仍是缺省的
 * 旧版 krkr2 层）。层的口径与逐条差异见 compat/README.md，注册表实现在
 * `cpp/core/compat/CompatLayer.cpp`。缺省层 = `krkr2-classic`：AetherKiri 层是新增
 * 代码路径，必须按游戏显式开启（`auto` 判档暂不切换层）。
 *
 * 档只映射到既有选项，插件与核心照旧只认选项。档的口径变化必须同时 +1 版本号，
 * 这样真机日志（`compat profile: <name> v<n> -> ...`）能区分"哪一版口径"。
 *
 * 取值：
 *   - `auto`：按**血脉标记**自动判定（不看游戏名字），需要
 *     [ENGINE_OPTION_GAME_COMPAT_GAME_ROOT]：
 *       plugin/krkrgles.dll | krkrlive2d.dll → krkrz-gpu
 *       plugin/motionplayer*.dll             → krkrz-kag
 *       其它                                  → kirikiri2-classic
 *   - 具名档：直接指定上面的名字。
 *
 * ⚠️ 时机：krkrgles 插件是在**注册完成（post-regist，engine_create 期）**读
 * `ogldrawdevice_compat` 的，所以壳必须在 engine_create 之前把本选项与
 * [ENGINE_OPTION_GAME_COMPAT_GAME_ROOT] 传进来；两个选项到齐的那一刻就完成解析。
 * 若壳另外显式传了 `ogldrawdevice_compat`，以**显式值**为准。
 */
#define ENGINE_OPTION_GAME_COMPAT_PROFILE "game_compat_profile"

/** 自动判档用的游戏根目录（裸文件系统路径，与 engine_open_game 同一个）。 */
#define ENGINE_OPTION_GAME_COMPAT_GAME_ROOT "game_compat_game_root"

#define ENGINE_GAME_COMPAT_PROFILE_AUTO "auto"
#define ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2 "kirikiri2-classic"
#define ENGINE_GAME_COMPAT_PROFILE_KRKRZ_GPU "krkrz-gpu"
#define ENGINE_GAME_COMPAT_PROFILE_KRKRZ_KAG "krkrz-kag"
#define ENGINE_GAME_COMPAT_PROFILE_KRKRZ_OGL "krkrz-ogl"

/**
 * AetherKiri 兼容层档（必须显式开启；见上面的兼容档说明）。
 *
 * 与其它档的区别：它会调用 `krkr::compat::SetActiveLayerByName("aetherkiri")`，把整层
 * 行为（IO/加载策略、脚本前奏、插件注册集合）切到 AetherKiri 口径；其余档一律保持
 * 缺省的旧版 krkr2 层。层还没接通的部分不产生运行时影响（见 compat/README.md）。
 */
#define ENGINE_GAME_COMPAT_PROFILE_AETHERKIRI "aetherkiri"

/** Render pipeline selection ("opengl" or "software"). */
#define ENGINE_OPTION_RENDERER "renderer"

/** Memory profile ("balanced" / "aggressive").
 *  Consumed by the C++ memory governor via TVPGetCommandLine(). */
#define ENGINE_OPTION_MEMORY_PROFILE "memory_profile"

/** Runtime memory budget in MB (0 = auto).
 *  Consumed by the C++ memory governor via TVPGetCommandLine(). */
#define ENGINE_OPTION_MEMORY_BUDGET_MB "memory_budget_mb"

/** Memory governor log interval in milliseconds.
 *  Consumed by the C++ memory governor via TVPGetCommandLine(). */
#define ENGINE_OPTION_MEMORY_LOG_INTERVAL_MS "memory_log_interval_ms"

/** PSB resource cache budget in MB. */
#define ENGINE_OPTION_PSB_CACHE_MB "psb_cache_mb"

/** PSB resource cache max entry count. */
#define ENGINE_OPTION_PSB_CACHE_ENTRIES "psb_cache_entries"

/** Archive cache max entry count. */
#define ENGINE_OPTION_ARCHIVE_CACHE_COUNT "archive_cache_count"

/** Auto path cache max entry count. */
#define ENGINE_OPTION_AUTOPATH_CACHE_COUNT "autopath_cache_count"

/* ── 已废弃的 ANGLE Backend 取值（仅为兼容旧设置文件保留） ───────── */

#define ENGINE_ANGLE_BACKEND_GLES "gles"
#define ENGINE_ANGLE_BACKEND_VULKAN "vulkan"

/* ── Renderer Values ────────────────────────────────────────────── */

#define ENGINE_RENDERER_OPENGL "opengl"
#define ENGINE_RENDERER_SOFTWARE "software"

#define ENGINE_MEMORY_PROFILE_BALANCED "balanced"
#define ENGINE_MEMORY_PROFILE_AGGRESSIVE "aggressive"

#endif /* KRKR2_ENGINE_OPTIONS_H_ */
