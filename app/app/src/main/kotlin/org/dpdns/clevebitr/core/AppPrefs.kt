package org.dpdns.clevebitr.core

import android.content.Context
import android.content.SharedPreferences

/**
 * 壳侧设置。用 `SharedPreferences` 存：这些值要在**两个进程**（主进程与 `:crash`）
 * 与 `Application.onCreate` 这么早的时机读到，文件式的配置需要额外处理并发与时机，
 * SharedPreferences 直接就有进程可见性与同步语义。
 *
 * 所有写操作统一 `apply()`：设置项都是"下次启动/下次开游戏生效"这一类，不需要立即
 * 落盘的同步语义，更不该在 UI 线程上等待写盘。
 */
object AppPrefs {

    private const val FILE_NAME = "krkr2next_prefs"

    /** logcat 采集（见 [LogcatCapture]）。默认开：只进 logcat 的那些日志是排障必需。 */
    private const val KEY_LOGCAT_CAPTURE = "debug.logcat_capture"

    /** 引擎帧率上限。0 = 不限速，跟随 vsync（默认）。 */
    private const val KEY_FPS_LIMIT = "debug.fps_limit"

    /** 性能叠加层档位（`off` / `summary` / `detail`）。默认关。 */
    private const val KEY_PERF_OVERLAY = "debug.perf_overlay"

    /**
     * 旧键：布尔型的"显示 FPS"。只用来**读旧值**——从上一版升上来的用户不该发现自己的
     * 设置被重置。与 AetherKiri 处理 `rendering/perf_overlay` 旧键的方式一致。
     */
    private const val KEY_SHOW_FPS = "debug.show_fps"

    /** 上次浏览到的目录，下次启动回到这里。 */
    private const val KEY_LAST_DIR = "ui.last_dir"

    /** 主题档位（`system` / `light` / `dark`）。默认跟随系统。 */
    private const val KEY_THEME = "ui.theme"

    /** 引擎字体回退策略（`auto` / `legacy` / `chain`）。见 [FONT_FALLBACK_MODES]。 */
    private const val KEY_FONT_FALLBACK = "engine.font_fallback"

    /**
     * krkrz 的 OGLDrawDevice 兼容档位（`off` / `alias`）。
     * 见 [OGLDRAWDEVICE_COMPAT_MODES]。
     */
    private const val KEY_OGLDRAWDEVICE_COMPAT = "engine.ogldrawdevice_compat"

    /** 游戏兼容档（`auto` / `kirikiri2-classic` / `krkrz-gpu` / `krkrz-kag` / `krkrz-ogl`）。 */
    private const val KEY_GAME_COMPAT_PROFILE = "engine.game_compat_profile"

    const val FPS_LIMIT_UNLIMITED = 0

    private fun prefs(context: Context): SharedPreferences =
        context.applicationContext.getSharedPreferences(FILE_NAME, Context.MODE_PRIVATE)

    // ── 调试 ──────────────────────────────────────────────────────────────

    fun logcatCapture(context: Context): Boolean =
        prefs(context).getBoolean(KEY_LOGCAT_CAPTURE, true)

    fun setLogcatCapture(context: Context, enabled: Boolean) =
        prefs(context).edit().putBoolean(KEY_LOGCAT_CAPTURE, enabled).apply()

    fun fpsLimit(context: Context): Int =
        prefs(context).getInt(KEY_FPS_LIMIT, FPS_LIMIT_UNLIMITED)

    fun setFpsLimit(context: Context, limit: Int) =
        prefs(context).edit().putInt(KEY_FPS_LIMIT, limit).apply()

    /** 合法档位，与 AetherKiri 的 `DEBUG_OVERLAY_MODES` 同名同义。 */
    val PERF_OVERLAY_MODES = listOf("off", "summary", "detail")

    /**
     * 叠加层档位。非法值或从未设置时：旧布尔开关为真按 `summary` 处理，否则 `off`
     * （AetherKiri 也是"旧键为真 → summary，否则 off"）。
     */
    fun perfOverlayMode(context: Context): String {
        val p = prefs(context)
        val stored = p.getString(KEY_PERF_OVERLAY, null)
        if (stored != null && stored in PERF_OVERLAY_MODES) return stored
        return if (p.getBoolean(KEY_SHOW_FPS, false)) "summary" else "off"
    }

    fun setPerfOverlayMode(context: Context, mode: String) {
        val normalized = if (mode in PERF_OVERLAY_MODES) mode else "off"
        prefs(context).edit().putString(KEY_PERF_OVERLAY, normalized).apply()
    }

    // ── 界面状态 ──────────────────────────────────────────────────────────

    /** 上次浏览的目录；不存在或已不可用就返回 null，由调用方回退到默认根目录。 */
    fun lastDir(context: Context): String? =
        prefs(context).getString(KEY_LAST_DIR, null)

    fun setLastDir(context: Context, path: String) =
        prefs(context).edit().putString(KEY_LAST_DIR, path).apply()

    // ── 主题 ──────────────────────────────────────────────────────────────

    /** 合法主题档位。`system` 跟随系统，也是默认值。 */
    val THEME_MODES = listOf("system", "light", "dark")

    fun themeMode(context: Context): String {
        val stored = prefs(context).getString(KEY_THEME, null)
        return if (stored != null && stored in THEME_MODES) stored else "system"
    }

    fun setThemeMode(context: Context, mode: String) {
        val normalized = if (mode in THEME_MODES) mode else "system"
        prefs(context).edit().putString(KEY_THEME, normalized).apply()
    }

    // ── 引擎字体回退 ──────────────────────────────────────────────────────

    /**
     * 引擎字体回退策略，写到引擎侧的同名选项（见 `engine_set_option`）：
     *  - `auto`  ：按字面/字形能力自动选（默认）
     *  - `legacy`：原版派系实现（单一 fallback 字面）
     *  - `chain` ：AetherKiri 派系实现（注册字面逐个回退 + 基线对齐）
     *
     * 两种实现都保留；遇到缺字（黑方块）时可在设置里手动切到另一种对比。
     */
    val FONT_FALLBACK_MODES = listOf("auto", "legacy", "chain")

    fun fontFallbackMode(context: Context): String {
        val stored = prefs(context).getString(KEY_FONT_FALLBACK, null)
        return if (stored != null && stored in FONT_FALLBACK_MODES) stored else "auto"
    }

    fun setFontFallbackMode(context: Context, mode: String) {
        val normalized = if (mode in FONT_FALLBACK_MODES) mode else "auto"
        prefs(context).edit().putString(KEY_FONT_FALLBACK, normalized).apply()
    }

    // ── krkrz 的 OGLDrawDevice 兼容层 ──────────────────────────────────────

    /**
     * krkrz（吉里吉里Z）系游戏在 `Initialize.tjs` 里会先看 `Window.OGLDrawDevice`
     * 在不在，再决定要不要加载 GPU 层脚本（`GPULayer.tjs` / `GPUAffineLayer.tjs`）。
     * 缺了它游戏**不报错**，只是静静降级，表现出来是"有声音、画面黑"。
     *
     *  - `off`  ：不提供（默认，保持既有行为）
     *  - `ogl`  ：只挂 `Window.OGLDrawDevice`
     *  - `alias`：挂 `Window.OGLDrawDevice` + `Window.GLESAdaptor`
     *  - `kag`  ：在 `alias` 之上再接管 `KAGWindow_createDrawDevice`
     *
     * 各档必须逐游戏试：`Window.OGLDrawDevice` 是闸门（挂上才会加载
     * `GPULayer.tjs` / `GPUAffineLayer.tjs`）；`Window.GLESAdaptor` 会把一部分游戏
     * （千恋万花）切进 motionplayer 的 `captureCanvas` 路径而 UI 出问题（用 `ogl` 避开）；
     * `kag` 能让千恋万花正常，但对 G2 会把主机 FBO 弄成 INCOMPLETE、连回想页都黑。
     *
     * 引擎在插件注册时读一次，所以"下次开游戏生效"。
     */
    val OGLDRAWDEVICE_COMPAT_MODES = listOf("off", "ogl", "alias", "kag")

    fun oglDrawDeviceCompat(context: Context): String {
        val stored = prefs(context).getString(KEY_OGLDRAWDEVICE_COMPAT, null)
        return if (stored != null && stored in OGLDRAWDEVICE_COMPAT_MODES) stored else "off"
    }

    fun setOglDrawDeviceCompat(context: Context, mode: String) {
        val normalized = if (mode in OGLDRAWDEVICE_COMPAT_MODES) mode else "off"
        prefs(context).edit().putString(KEY_OGLDRAWDEVICE_COMPAT, normalized).apply()
    }

    /**
     * 游戏兼容档（compat profile）—— 两条血脉差异的收敛点，见 `engine_options.h`。
     *
     * 引擎按**血脉标记**自动判档（只看游戏目录里有没有 `krkrgles.dll` /
     * `krkrlive2d.dll` / `motionplayer*.dll`，**不看游戏名字**）：
     *   - 带 krkrgles / Live2D → `krkrz-gpu`（GPU 层闸门 + GLESAdaptor）
     *   - 带 motionplayer     → `krkrz-kag`（窗口绘制设备工厂走 KAGWindow）
     *   - 其余                → `kirikiri2-classic`
     *
     * 取 `auto` 之外的具名档就是直接指定。无论哪种，只要 [oglDrawDeviceCompat]
     * 被显式设置过，引擎以显式值为准（档只在没显式设置时决定它）。
     */
    val GAME_COMPAT_PROFILES =
        listOf("auto", "kirikiri2-classic", "krkrz-gpu", "krkrz-kag", "krkrz-ogl")

    fun gameCompatProfile(context: Context): String {
        val stored = prefs(context).getString(KEY_GAME_COMPAT_PROFILE, null)
        return if (stored != null && stored in GAME_COMPAT_PROFILES) stored else "auto"
    }

    fun setGameCompatProfile(context: Context, profile: String) {
        val normalized = if (profile in GAME_COMPAT_PROFILES) profile else "auto"
        prefs(context).edit().putString(KEY_GAME_COMPAT_PROFILE, normalized).apply()
    }
}
