package org.dpdns.clevebitr.core

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONObject

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

    /** 收藏目录的顺序表（换行分隔，见 [favoriteDirs]）。 */
    private const val KEY_FAV_DIRS_ORDER = "ui.fav_dirs"

    /** 主题档位（`system` / `light` / `dark`）。默认跟随系统。 */
    private const val KEY_THEME = "ui.theme"

    /** 引擎字体回退策略（`auto` / `legacy` / `chain`）。见 [FONT_FALLBACK_MODES]。 */
    private const val KEY_FONT_FALLBACK = "engine.font_fallback"

    /**
     * krkrz 的 OGLDrawDevice 兼容档位（`off` / `ogl` / `alias`）。
     * 见 [OGLDRAWDEVICE_COMPAT_MODES]。
     */
    private const val KEY_OGLDRAWDEVICE_COMPAT = "engine.ogldrawdevice_compat"

    /** 游戏兼容档（`auto` / `kirikiri2-classic` / `krkrz-gpu` / `krkrz-ogl` / `aetherkiri`）。 */
    private const val KEY_GAME_COMPAT_PROFILE = "engine.game_compat_profile"

    /** 全局默认运行模式（兼容层 + 渲染器的固定组合），见 [RunMode]。 */
    private const val KEY_RUN_MODE = "engine.run_mode"

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

    /**
     * 加载游戏时自动弹出运行时日志浮层（进游戏后自动关）。默认**关**。
     *
     * 为什么需要：引擎在 `startup state -> 2` 之前出的错（缺文件、脚本异常、兼容层
     * 没接上）往往发生在用户还没来得及点开日志浮层的时候，而问题一旦表现成“黑屏/
     * 卡住”，用户也没有机会去点。打开这个开关就把那段时间的日志直接摊在屏幕上。
     */
    private const val KEY_AUTO_LOG_ON_LAUNCH = "debug.auto_log_on_launch"

    fun autoLogOnLaunch(context: Context): Boolean =
        prefs(context).getBoolean(KEY_AUTO_LOG_ON_LAUNCH, false)

    fun setAutoLogOnLaunch(context: Context, enabled: Boolean) =
        prefs(context).edit().putBoolean(KEY_AUTO_LOG_ON_LAUNCH, enabled).apply()

    // ── 界面状态 ──────────────────────────────────────────────────────────

    /** 上次浏览的目录；不存在或已不可用就返回 null，由调用方回退到默认根目录。 */
    fun lastDir(context: Context): String? =
        prefs(context).getString(KEY_LAST_DIR, null)

    fun setLastDir(context: Context, path: String) =
        prefs(context).edit().putString(KEY_LAST_DIR, path).apply()

    // ── 文件浏览器：收藏目录 ───────────────────────────────────────────────
    //
    // 为什么放在壳偏好而不是游戏库里：它描述的是"用户的存储位置"，与具体游戏记录无关；
    // 放在这里也意味着"换游戏/清库"不会丢收藏。
    // 顺序：SharedPreferences 的字符串集合**不保证顺序**，所以另存一份有序列表
    // （`ui.fav_dirs_order`）用于展示；集合本身只用于 O(1) 判重。

    /** 收藏的目录（按加入顺序）。 */
    fun favoriteDirs(context: Context): List<String> =
        prefs(context).getString(KEY_FAV_DIRS_ORDER, "")
            ?.split('\n')
            ?.map { it.trim() }
            ?.filter { it.isNotEmpty() }
            ?: emptyList()

    /**
     * 切换某目录的收藏状态，返回切换后是否已收藏。
     *
     * 用"分隔符拼接的单个字符串"而不是 `putStringSet`：后者无序，展示时会跳来跳去；
     * 目录路径里不会出现换行，用它做分隔符最省事（不需要转义）。
     */
    fun toggleFavoriteDir(context: Context, path: String): Boolean {
        val current = favoriteDirs(context)
        val next = if (path in current) current - path else current + path
        prefs(context).edit().putString(KEY_FAV_DIRS_ORDER, next.joinToString("\n")).apply()
        return path in next
    }

    fun removeFavoriteDir(context: Context, path: String) {
        val next = favoriteDirs(context) - path
        prefs(context).edit().putString(KEY_FAV_DIRS_ORDER, next.joinToString("\n")).apply()
    }

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
     *
     * 各档必须逐游戏试：`Window.OGLDrawDevice` 是闸门（挂上才会加载
     * `GPULayer.tjs` / `GPUAffineLayer.tjs`）；`Window.GLESAdaptor` 会把一部分游戏
     * 会切进 motionplayer 的 `captureCanvas` 路径而 UI 出问题（用 `ogl` 避开）。
     *
     * KAGWindow 绘制设备接管（旧 `kag` 档）已改由 **AetherKiri 兼容层**负责，
     * 不再是本选项的取值。
     *
     * 引擎在插件注册时读一次，所以"下次开游戏生效"。
     */
    val OGLDRAWDEVICE_COMPAT_MODES = listOf("off", "ogl", "alias")

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
     *   - 带 motionplayer     → `aetherkiri`（激活 AetherKiri 层，接管 KAGWindow）
     *   - 其余                → `kirikiri2-classic`
     *
     * 取 `auto` 之外的具名档就是直接指定。无论哪种，只要 [oglDrawDeviceCompat]
     * 被显式设置过，引擎以显式值为准（档只在没显式设置时决定它）。
     */
    val GAME_COMPAT_PROFILES =
        listOf("auto", "kirikiri2-classic", "krkrz-gpu", "krkrz-ogl", "aetherkiri")

    fun gameCompatProfile(context: Context): String {
        val stored = prefs(context).getString(KEY_GAME_COMPAT_PROFILE, null)
        return if (stored != null && stored in GAME_COMPAT_PROFILES) stored else "auto"
    }

    fun setGameCompatProfile(context: Context, profile: String) {
        val normalized = if (profile in GAME_COMPAT_PROFILES) profile else "auto"
        prefs(context).edit().putString(KEY_GAME_COMPAT_PROFILE, normalized).apply()
    }

    // ── 运行模式（壳只暴露固定组合） ───────────────────────────────────────

    /**
     * 全局默认运行模式。
     *
     * 迁移：本键之前，等价信息分散在 `game.compat_profile` 与
     * `engine.ogldrawdevice_compat` 两个键里；旧安装没有 [KEY_RUN_MODE] 时按这两个值反推
     * （见 [RunMode.fromConfig]），**不写回**——用户真的在设置页选一次才落新键。
     */
    fun runMode(context: Context): RunMode {
        val stored = prefs(context).getString(KEY_RUN_MODE, null)
        if (stored != null) return RunMode.fromKey(stored)
        return RunMode.fromConfig(
            gameCompatProfile(context),
            oglDrawDeviceCompat(context),
        )
    }

    /**
     * 写运行模式：**同时**写回两个旧键。旧键仍被壳与诊断脚本读取（日志、
     * `krkr2next.json` 的对照），只写新模式会让它们读到过期值。
     */
    fun setRunMode(context: Context, mode: RunMode) {
        prefs(context).edit()
            .putString(KEY_RUN_MODE, mode.key)
            .putString(KEY_GAME_COMPAT_PROFILE, mode.compatProfile)
            .putString(KEY_OGLDRAWDEVICE_COMPAT, mode.oglDrawDeviceCompat)
            .apply()
    }

    // ── 性能叠加层（可自定义） ────────────────────────────────────────────

    /** 叠加层配置的 JSON 字符串，见 [OverlayConfig]。 */
    private const val KEY_OVERLAY_CONFIG = "debug.overlay_config"

    /**
     * **全局默认**叠加层配置。每游戏覆盖存在各自的 `krkr2next.json` 里。
     *
     * 升级路径：老用户只存过 `off`/`summary`/`detail` 档位，没有 JSON。这时用
     * [OverlayConfig.fromLegacyMode] 把档位翻译成字段集合，但**不写回**——只有用户
     * 真的在设置页动了开关才落新键，这样回退到旧版本时档位键仍然有效。
     */
    fun overlayConfig(context: Context): OverlayConfig {
        val p = prefs(context)
        if (p.contains(KEY_OVERLAY_CONFIG)) {
            val raw = p.getString(KEY_OVERLAY_CONFIG, null)
            if (!raw.isNullOrBlank()) {
                try {
                    OverlayConfig.fromJson(JSONObject(raw))?.let { return it }
                } catch (t: Throwable) {
                    AppLog.w(TAG, "叠加层配置解析失败，回退到档位键：$t")
                }
            }
        }
        return OverlayConfig.fromLegacyMode(perfOverlayMode(context))
    }

    fun setOverlayConfig(context: Context, config: OverlayConfig) {
        prefs(context).edit()
            .putString(KEY_OVERLAY_CONFIG, config.toJson().toString())
            .apply()
    }

    // ── 光标触控板模式 ────────────────────────────────────────────────────

    /** 光标触控板模式（模拟触控板驱动光标，而不是直接把手指标成绝对坐标）。 */
    private const val KEY_TOUCHPAD_MODE = "input.touchpad_mode"

    /** 触控板相对移动的灵敏度倍率。 */
    private const val KEY_TOUCHPAD_SENSITIVITY = "input.touchpad_sensitivity"

    /** 灵敏度默认值：手机屏幕上 1:1 相对移动太慢，1.8 更接近桌面触控板手感。 */
    const val TOUCHPAD_SENSITIVITY_DEFAULT = 1.8f

    val TOUCHPAD_SENSITIVITY_RANGE = 0.5f..4.0f

    fun touchpadMode(context: Context): Boolean =
        prefs(context).getBoolean(KEY_TOUCHPAD_MODE, false)

    fun setTouchpadMode(context: Context, enabled: Boolean) =
        prefs(context).edit().putBoolean(KEY_TOUCHPAD_MODE, enabled).apply()

    fun touchpadSensitivity(context: Context): Float {
        val v = prefs(context).getFloat(KEY_TOUCHPAD_SENSITIVITY, TOUCHPAD_SENSITIVITY_DEFAULT)
        return if (v.isNaN()) TOUCHPAD_SENSITIVITY_DEFAULT
        else v.coerceIn(TOUCHPAD_SENSITIVITY_RANGE)
    }

    fun setTouchpadSensitivity(context: Context, value: Float) {
        val v = if (value.isNaN()) TOUCHPAD_SENSITIVITY_DEFAULT
        else value.coerceIn(TOUCHPAD_SENSITIVITY_RANGE)
        prefs(context).edit().putFloat(KEY_TOUCHPAD_SENSITIVITY, v).apply()
    }

    // ── 自定义按键浮层 ────────────────────────────────────────────────────

    /** 游戏中右下角是否显示「引擎菜单」按钮（§4 侧边栏入口）。默认显示。 */
    private const val KEY_ENGINE_MENU_BUTTON = "input.engine_menu_button"

    fun engineMenuButton(context: Context): Boolean =
        prefs(context).getBoolean(KEY_ENGINE_MENU_BUTTON, true)

    fun setEngineMenuButton(context: Context, enabled: Boolean) =
        prefs(context).edit().putBoolean(KEY_ENGINE_MENU_BUTTON, enabled).apply()

    /** 全局默认按键浮层配置（JSON），见 [KeyPadProfile]。 */
    private const val KEY_KEYPAD_PROFILE = "input.keypad_profile"

    /** 具名模板表（JSON object：模板名 -> [KeyPadProfile]）。 */
    private const val KEY_KEYPAD_TEMPLATES = "input.keypad_templates"

    /**
     * **全局默认**按键浮层配置。每游戏覆盖存在各自的 `krkr2next.json` 里。
     *
     * 从未设置过时返回 [KeyPadProfile.starter]（**默认开启**的方向键 + 确认/返回布局）：
     * 触屏玩 KiriKiri 时这些键没有可点区域，默认给一套比默认空着更有用；不想要的
     * 用户在设置里关掉或清空即可。解析失败同样退回 starter（不抛异常）。
     */
    fun keyPadProfile(context: Context): KeyPadProfile {
        val raw = prefs(context).getString(KEY_KEYPAD_PROFILE, null)
        if (raw.isNullOrBlank()) return KeyPadProfile.starter()
        return try {
            KeyPadProfile.fromJson(JSONObject(raw)) ?: KeyPadProfile.starter()
        } catch (t: Throwable) {
            AppLog.w(TAG, "按键浮层配置解析失败，按默认处理：$t")
            KeyPadProfile.starter()
        }
    }

    fun setKeyPadProfile(context: Context, profile: KeyPadProfile) {
        prefs(context).edit()
            .putString(KEY_KEYPAD_PROFILE, profile.toJson().toString())
            .apply()
    }

    /**
     * 具名模板表。模板只存 `buttons`，应用时**重新分配 id**（见 [KeyPadProfile] 的
     * 调用方）——同一个模板套到多个游戏时，按钮 id 不能互相冲突。
     */
    fun keyPadTemplates(context: Context): Map<String, KeyPadProfile> {
        val raw = prefs(context).getString(KEY_KEYPAD_TEMPLATES, null)
        if (raw.isNullOrBlank()) return emptyMap()
        return try {
            val obj = JSONObject(raw)
            buildMap {
                obj.keys().forEach { name ->
                    KeyPadProfile.fromJson(obj.optJSONObject(name))?.let { put(name, it) }
                }
            }
        } catch (t: Throwable) {
            AppLog.w(TAG, "按键模板解析失败，按空处理：$t")
            emptyMap()
        }
    }

    /** 保存/覆盖一个具名模板。返回保存后的模板表。 */
    fun saveKeyPadTemplate(
        context: Context,
        name: String,
        profile: KeyPadProfile,
    ): Map<String, KeyPadProfile> {
        val trimmed = name.trim()
        if (trimmed.isEmpty()) return keyPadTemplates(context)
        val next = keyPadTemplates(context).toMutableMap()
        next[trimmed] = KeyPadProfile(buttons = profile.buttons, enabled = profile.enabled)
        writeKeyPadTemplates(context, next)
        return next
    }

    fun deleteKeyPadTemplate(context: Context, name: String): Map<String, KeyPadProfile> {
        val next = keyPadTemplates(context).toMutableMap()
        next.remove(name)
        writeKeyPadTemplates(context, next)
        return next
    }

    private fun writeKeyPadTemplates(context: Context, templates: Map<String, KeyPadProfile>) {
        val obj = JSONObject()
        templates.forEach { (name, profile) -> obj.put(name, profile.toJson()) }
        prefs(context).edit().putString(KEY_KEYPAD_TEMPLATES, obj.toString()).apply()
    }

    // ── 游戏库 ────────────────────────────────────────────────────────────

    /** 库列表排序（`lastPlayed` / `title` / `added`）。 */
    private const val KEY_LIBRARY_SORT = "ui.library_sort"

    val LIBRARY_SORTS = listOf("lastPlayed", "title", "added")

    fun librarySort(context: Context): String {
        val stored = prefs(context).getString(KEY_LIBRARY_SORT, null)
        return if (stored != null && stored in LIBRARY_SORTS) stored else "lastPlayed"
    }

    fun setLibrarySort(context: Context, sort: String) {
        val normalized = if (sort in LIBRARY_SORTS) sort else "lastPlayed"
        prefs(context).edit().putString(KEY_LIBRARY_SORT, normalized).apply()
    }

    private const val TAG = "KrKr2Next/Prefs"
}
