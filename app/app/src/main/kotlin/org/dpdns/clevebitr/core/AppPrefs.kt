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

    /** 游戏画面左上角叠加 FPS。默认关。 */
    private const val KEY_SHOW_FPS = "debug.show_fps"

    /** 上次浏览到的目录，下次启动回到这里。 */
    private const val KEY_LAST_DIR = "ui.last_dir"

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

    fun showFps(context: Context): Boolean =
        prefs(context).getBoolean(KEY_SHOW_FPS, false)

    fun setShowFps(context: Context, enabled: Boolean) =
        prefs(context).edit().putBoolean(KEY_SHOW_FPS, enabled).apply()

    // ── 界面状态 ──────────────────────────────────────────────────────────

    /** 上次浏览的目录；不存在或已不可用就返回 null，由调用方回退到默认根目录。 */
    fun lastDir(context: Context): String? =
        prefs(context).getString(KEY_LAST_DIR, null)

    fun setLastDir(context: Context, path: String) =
        prefs(context).edit().putString(KEY_LAST_DIR, path).apply()
}
