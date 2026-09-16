package org.dpdns.clevebitr

import android.app.ActivityManager
import android.app.Application
import android.os.Build
import android.os.Process
import android.util.Log
import coil3.ImageLoader
import coil3.PlatformContext
import coil3.SingletonImageLoader
import coil3.network.okhttp.OkHttpNetworkFetcherFactory
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.CrashTracker
import org.dpdns.clevebitr.core.LogFiles
import org.dpdns.clevebitr.core.LogcatCapture
import org.dpdns.clevebitr.core.NativeEngine

/**
 * 每个进程都会跑 `onCreate`，包括崩溃界面所在的 `:crash` 进程。所以这里第一件事就是
 * 按进程名分流——`:crash` 只负责把界面画出来，绝不能顺带加载引擎、装异常处理器、
 * 初始化日志：那等于把主进程崩溃时的处境重演一遍，还可能再崩一次把界面一起带走。
 */
class KrKr2NextApplication : Application(), SingletonImageLoader.Factory {

    companion object {
        private const val TAG = "KrKr2Next/App"

        /** `CrashActivity` 所在进程的后缀（manifest 里的 `android:process=":crash"`）。 */
        const val CRASH_PROCESS_SUFFIX = ":crash"

        /**
         * 引擎共享库是否加载成功。加载失败时 UI 需要给出明确提示，
         * 而不是让每次触碰 NativeEngine 都抛 UnsatisfiedLinkError。
         */
        @Volatile
        var engineLibraryLoaded: Boolean = false
            private set
    }

    /**
     * Coil 的 ImageLoader 工厂（Coil 3 的接入点）。
     *
     * 之所以显式给网络 fetcher：Coil 3 把网络支持拆成了独立 artifact
     * （`coil-network-okhttp`），只把它放在 classpath 上要靠 ServiceLoader 自动发现，
     * 一旦被裁剪或混淆就退化成"图片永远加载不出来"。这里写死，行为可预期。
     */
    override fun newImageLoader(context: PlatformContext): ImageLoader =
        ImageLoader.Builder(context)
            .components { add(OkHttpNetworkFetcherFactory()) }
            .build()

    override fun onCreate() {
        super.onCreate()

        val processName = currentProcessName()
        if (processName?.endsWith(CRASH_PROCESS_SUFFIX) == true) {
            Log.i(TAG, "crash process ($processName): skip engine & log init")
            return
        }

        // 日志目录先落定：含可写性探测与逐级回退。结果被缓存，崩溃处理器随后只读
        // 这个缓存，不再做任何解析。
        val logDir = LogFiles.logsDir(this)
        AppLog.init(this)

        // 尽早装：装得越早，能捕获到的崩溃窗口越大
        CrashTracker.install(this)

        AppLog.i(TAG, "app start pid=${Process.myPid()} process=${processName ?: "?"} logs=$logDir")

        // logcat 采集：补上只进 logcat 的那部分（引擎的 AndroidInfoLog、debuggerd
        // 的 tombstone）。取不到就自己降级，不影响别的。
        // 开关在设置页，改动下次启动生效——采集要常驻一条 logcat 子进程，关掉它是
        // 省电/省 IO 的手段。
        if (AppPrefs.logcatCapture(this)) {
            LogcatCapture.start(this)
        } else {
            AppLog.i(TAG, "logcat capture disabled by preference")
        }

        try {
            // 触发 NativeEngine 的类初始化（内部 System.loadLibrary("engine_api")）
            // 并把 Application Context 交给引擎，供 AndroidUtils.cpp 在拿不到
            // Activity 时回退使用（getExternalFilesDirs / getFilesDir 等）。
            NativeEngine.nativeSetApplicationContext(this)
            engineLibraryLoaded = true
            AppLog.i(TAG, "libengine_api.so loaded, Application Context handed over")

            // 引擎日志也落到同一个外部目录：原生崩溃时那串 FATAL SIGNAL 与原生栈
            // 就写在这里（引擎的 CrashSignalHandler 走的是 spdlog）。
            //
            // 必须先 mkdirs：engine_set_log_file_path 的 JNI 包装没有 try/catch，
            // 目录不存在时 spdlog 抛的异常会直接穿过 JNI 打到 Kotlin 侧。
            val engineLog = LogFiles.engineLog(this)
            engineLog.parentFile?.mkdirs()
            // 先由 Java 侧把文件建出来：真机上 native 的 fopen 新建文件会间歇性
            // 返回 ENOENT（同一目录里 Java 写文件却正常），而"打开已存在的文件"
            // 稳定成功。建不出来也不阻断 —— native 侧现在会返回错误码而不是抛异常
            // 穿 JNI，日志退化为只进 logcat。
            try {
                if (!engineLog.isFile) engineLog.createNewFile()
            } catch (t: Throwable) {
                AppLog.w(TAG, "预建 ${engineLog.name} 失败，日志将只进 logcat", t)
            }
            val rc = NativeEngine.engineSetLogFilePath(engineLog.absolutePath)
            AppLog.i(TAG, "engine log -> ${engineLog.absolutePath} (rc=$rc)")
        } catch (e: UnsatisfiedLinkError) {
            AppLog.e(TAG, "failed to load libengine_api.so — 引擎未打包进 APK？", e)
        }
    }

    /**
     * API 28+ 直接用 `Application.getProcessName()`（注意它是 **static** 方法，
     * 不能当实例属性写成 `processName`）；更低版本只能按 pid 从运行中的进程列表反查
     * （该 API 已废弃，但对自身进程仍然有效）。
     */
    private fun currentProcessName(): String? {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            return getProcessName()
        }
        return try {
            getSystemService(ActivityManager::class.java)
                ?.runningAppProcesses
                ?.firstOrNull { it.pid == Process.myPid() }
                ?.processName
        } catch (t: Throwable) {
            Log.w(TAG, "cannot determine process name", t)
            null
        }
    }
}
