package org.dpdns.clevebitr.core

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Environment
import android.util.Log
import androidx.core.content.FileProvider
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 日志落盘位置与文件生命周期。
 *
 * ## 为什么是 Android/media/<包名>/logs
 *
 * 这个目录在 API 30+ **不需要任何权限**就能读写，且不受分区存储限制——任何 Android
 * 版本的文件管理器都能直接打开它把日志拷走。相比之下 `Android/data/<包名>/files`
 * 在 Android 11+ 对第三方文件管理器不可见，`/sdcard` 根目录下的自定义目录则要
 * 依赖 all-files 权限。代价是卸载 App 时会随应用数据一起删除。
 *
 * ## 回退链
 *
 * `Android/media/<包名>` 的路径在 API 21+ 都拿得到，但 **API 24-29 没有
 * WRITE_EXTERNAL_STORAGE 时写不进去**，所以不能只看路径能不能拿到，必须真的写一下。
 * 逐级回退：media → externalFiles（免权限的外部私有目录）→ filesDir → cacheDir。
 * 越往后越"外部拿不到"，但至少日志不会丢。
 */
object LogFiles {

    private const val TAG = "KrKr2Next/Log"
    private const val DIR_NAME = "logs"

    /** 引擎自己那份日志的文件名（`engine_set_log_file_path` 的落点）。 */
    const val ENGINE_LOG_NAME = "engine.log"

    /** 壳自己的日志。 */
    const val APP_LOG_NAME = "app.log"

    /** logcat 采集落盘的文件（见 [LogcatCapture]）。 */
    const val LOGCAT_LOG_NAME = "logcat.log"

    /** 崩溃报告子目录。 */
    const val CRASH_DIR_NAME = "crash"

    /** 会话标记文件名，用于下次启动判断上次是不是正常退出。 */
    const val SESSION_MARKER_NAME = "session.txt"

    private const val APP_LOG_LIMIT_BYTES = 2L * 1024 * 1024
    private const val APP_LOG_BACKUPS = 3

    /** 目录总量上限，含引擎那 4MiB×3。超出按 mtime 从最旧的开始删。 */
    private const val DIR_TOTAL_LIMIT_BYTES = 32L * 1024 * 1024
    private const val CRASH_KEEP = 5

    private val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)

    /**
     * 解析出来的日志目录。
     *
     * `@Volatile` + 只在首次解析时写入：**崩溃处理器里只读这个缓存，绝不再解析**
     * ——解析要碰 `getExternalMediaDirs` / 文件 IO，崩在任意线程时做这些很危险。
     */
    @Volatile
    private var resolved: File? = null

    /** 首次调用会做一次可写性探测；之后返回缓存。 */
    fun logsDir(context: Context): File {
        resolved?.let { return it }
        val dir = resolve(context.applicationContext)
        resolved = dir
        return dir
    }

    fun engineLog(context: Context): File = File(logsDir(context), ENGINE_LOG_NAME)

    fun appLog(context: Context): File = File(logsDir(context), APP_LOG_NAME)

    fun crashDir(context: Context): File = File(logsDir(context), CRASH_DIR_NAME)

    fun sessionMarker(context: Context): File = File(logsDir(context), SESSION_MARKER_NAME)

    fun timestamp(): String = stamp.format(Date())

    private fun resolve(context: Context): File {
        val candidates = ArrayList<File>(4)
        // 外部存储不可用时 media / externalFiles 两级都没有意义，直接跳过
        if (Environment.getExternalStorageState() == Environment.MEDIA_MOUNTED) {
            context.externalMediaDirs?.firstOrNull()?.let { candidates.add(File(it, DIR_NAME)) }
            context.getExternalFilesDir(null)?.let { candidates.add(File(it, DIR_NAME)) }
        }
        candidates.add(File(context.filesDir, DIR_NAME))
        candidates.add(File(context.cacheDir, DIR_NAME))

        for (candidate in candidates) {
            if (isWritable(candidate)) {
                Log.i(TAG, "log dir = ${candidate.absolutePath}")
                return candidate
            }
        }
        // 四级全失败（理论上只有存储彻底不可用才会发生）。仍然返回 cacheDir 下那一级，
        // 让上层不必到处判空；写失败由调用方各自的 try/catch 兜住。
        val fallback = File(context.cacheDir, DIR_NAME)
        fallback.mkdirs()
        Log.w(TAG, "no writable log dir, falling back to ${fallback.absolutePath}")
        return fallback
    }

    /**
     * 真的写一个探针文件再删掉。`mkdirs()` 返回 false 可能只是"已存在"，
     * `canWrite()` 在部分 ROM 上也不可靠，只有实写才算数。
     */
    private fun isWritable(dir: File): Boolean {
        return try {
            if (!dir.isDirectory && !dir.mkdirs() && !dir.isDirectory) return false
            val probe = File(dir, ".writable")
            probe.writeBytes(byteArrayOf(0))
            probe.delete()
            true
        } catch (e: IOException) {
            false
        } catch (e: SecurityException) {
            false
        }
    }

    /**
     * 单文件超过 [limitBytes] 就轮转：`name` → `name.1` → `name.2` …… 最多留 [keep] 份。
     * 每次写入前调用即可（长度检查很便宜）。
     */
    fun rotate(file: File, limitBytes: Long, keep: Int) {
        try {
            if (!file.isFile || file.length() < limitBytes) return
            File(file.parentFile, "${file.name}.$keep").delete()
            for (i in keep - 1 downTo 1) {
                val from = File(file.parentFile, "${file.name}.$i")
                if (from.isFile) from.renameTo(File(file.parentFile, "${file.name}.${i + 1}"))
            }
            file.renameTo(File(file.parentFile, "${file.name}.1"))
        } catch (e: Exception) {
            // 轮转失败不能影响写日志本身
            Log.w(TAG, "rotate(${file.name}) failed", e)
        }
    }

    /** 崩溃报告只留最近 [CRASH_KEEP] 份。 */
    fun pruneCrashReports(context: Context) {
        try {
            val dir = crashDir(context)
            val reports = dir.listFiles { f -> f.isFile && f.name.startsWith("crash-") }
                ?.sortedByDescending { it.lastModified() } ?: return
            reports.drop(CRASH_KEEP).forEach { it.delete() }
        } catch (e: Exception) {
            Log.w(TAG, "pruneCrashReports failed", e)
        }
    }

    /**
     * 目录总量超限时按 mtime 从最旧的开始删。
     *
     * [protected] 是**当前正在写**的文件名，必须跳过——删掉引擎正在写的那个文件，
     * spdlog 的 sink 会失去落点（它按已打开的文件描述符写，删掉后写入的是已 unlink
     * 的 inode，日志就凭空消失了）。
     */
    fun pruneTotal(context: Context, protected: Collection<String>) {
        try {
            val dir = logsDir(context)
            val files = dir.listFiles { f -> f.isFile }?.sortedBy { it.lastModified() } ?: return
            var total = files.sumOf { it.length() }
            if (total <= DIR_TOTAL_LIMIT_BYTES) return
            for (f in files) {
                if (total <= DIR_TOTAL_LIMIT_BYTES) break
                if (f.name in protected) continue
                val len = f.length()
                if (f.delete()) total -= len
            }
        } catch (e: Exception) {
            Log.w(TAG, "pruneTotal failed", e)
        }
    }

    /**
     * 分享日志的 Intent。日志本来就在外部公共目录，这个只是省得用户自己找路径。
     *
     * 多文件用 `EXTRA_STREAM` 的 ArrayList，并逐条塞进 `ClipData`——只设
     * `FLAG_GRANT_READ_URI_PERMISSION` 而不放 ClipData 时，部分接收方只能拿到
     * 第一个 URI 的授权。
     */
    fun buildShareIntent(context: Context, files: List<File>): Intent? {
        val existing = files.filter { it.isFile && it.length() > 0 }
        if (existing.isEmpty()) return null
        val uris = ArrayList<Uri>(existing.size)
        for (f in existing) {
            try {
                uris.add(
                    FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", f),
                )
            } catch (e: IllegalArgumentException) {
                // 路径不在 file_paths.xml 声明白名单内。跳过而不是整体失败。
                Log.w(TAG, "not shareable: ${f.absolutePath}", e)
            }
        }
        if (uris.isEmpty()) return null

        val intent = Intent(Intent.ACTION_SEND_MULTIPLE).apply {
            type = "text/plain"
            putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris)
            putExtra(Intent.EXTRA_SUBJECT, "KrKr2Next 日志")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val clip = android.content.ClipData.newUri(context.contentResolver, "logs", uris[0])
        for (i in 1 until uris.size) clip.addItem(android.content.ClipData.Item(uris[i]))
        intent.clipData = clip
        return intent
    }

    /**
     * 清空日志目录（含崩溃报告），返回删掉的文件数。
     *
     * **只应在没有游戏在跑的时候调用**：引擎正持着 `engine.log` 的 fd，删掉它只是
     * 解除链接，引擎会继续往那个已 unlink 的 inode 写——日志看上去"消失了"，
     * 直到下次重启引擎才重新建文件。设置页只在启动器里可达，正是这个前提。
     */
    fun clearAll(context: Context): Int {
        var deleted = 0
        listOf(logsDir(context), crashDir(context)).forEach { dir ->
            dir.listFiles { f -> f.isFile }?.forEach { if(it.delete()) deleted++ }
        }
        return deleted
    }

    /** 分享日志时带上目录里的全部内容（app.log 及其轮转份、engine.log 系列、崩溃报告）。 */
    fun collectForSharing(context: Context): List<File> {
        val dir = logsDir(context)
        val out = ArrayList<File>()
        dir.listFiles { f -> f.isFile }?.let { out.addAll(it) }
        crashDir(context).listFiles { f -> f.isFile }?.let { out.addAll(it) }
        return out
    }
}
