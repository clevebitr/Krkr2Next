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
