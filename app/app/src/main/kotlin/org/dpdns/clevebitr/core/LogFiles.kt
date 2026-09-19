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

    /** 记录"最后一个开过的游戏目录"的文件，见 [lastGame] / [setLastGame]。 */
    const val LAST_GAME_NAME = "last-game.txt"

    private const val APP_LOG_LIMIT_BYTES = 2L * 1024 * 1024
    private const val APP_LOG_BACKUPS = 3

    /**
     * 每游戏日志保留的**启动次数**（每次开游戏一份 `engine-<时间戳>.log`）。
     *
     * 每游戏日志不是"一个不断增长的流"，而是"每个游戏每次启动一份现场"：一次启动一份，
     * 排障时要对比的正是"这次和上次哪里不一样"。留太多会让目录无限膨胀，只留最近
     * [GAME_LOG_BACKUPS] 份既能对照，又不会把存储吃掉。
     */
    private const val GAME_LOG_BACKUPS = 3

    /** 分享时每游戏最多带几份（最新的）。再多收件方也读不动。 */
    private const val SHARE_PER_GAME = 1

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

    // ── 每游戏日志 ─────────────────────────────────────────────────────────
    //
    // 为什么需要：引擎日志此前只有一份进程级 `engine.log`，而 `engine_set_log_file_path`
    // 是**可以重复调用**的（内部会先摘掉旧 sink，见 engine_api.cpp 的
    // DetachFileSinkFromLoggers）。所以开游戏时把它切到该游戏自己的文件即可，
    // 多个游戏不再混在一个文件里。
    //
    // 目录结构：`logs/games/<安全名>-<路径短哈希>/engine-<时间戳>.log`
    //   - 安全名：取游戏目录的叶子名，剔掉路径分隔符与控制字符，限长（中文名照常保留）；
    //   - 短哈希：同一叶子名的不同路径（如两个目录都叫 `game`）不会互相覆盖；
    //   - 时间戳：同一游戏多次启动各自一份，便于对照"这次跑的是什么状态"。

    /** 每游戏日志子目录名。 */
    const val GAME_LOG_DIR_NAME = "games"

    /** 每游戏日志的文件名前缀（`engine-<时间戳>.log`）。 */
    const val GAME_LOG_PREFIX = "engine-"

    private fun sanitizeGameName(raw: String): String {
        val leaf = raw.trimEnd('/', '\\').substringAfterLast('/').substringAfterLast('\\')
        val cleaned = leaf.map { ch ->
            when {
                ch.isLetterOrDigit() || ch == '.' || ch == '_' || ch == '-' -> ch
                ch == ' ' -> '_'
                else -> '_'
            }
        }.joinToString("")
        val trimmed = cleaned.trim('_', '.').ifEmpty { "game" }
        return if (trimmed.length > 48) trimmed.take(48) else trimmed
    }

    private fun pathTag(raw: String): String = try {
        val md = java.security.MessageDigest.getInstance("SHA-256")
        md.digest(raw.toByteArray(Charsets.UTF_8)).take(4).joinToString("") { "%02x".format(it) }
    } catch (t: Throwable) {
        // 摘要算不出来也不能挡住开游戏：退回"路径长度 + 无哈希"。
        "n${raw.length}"
    }

    /** 该游戏的日志目录（不创建）。 */
    fun gameLogDir(context: Context, gameRootPath: String): File {
        val name = "${sanitizeGameName(gameRootPath)}-${pathTag(gameRootPath)}"
        return File(File(logsDir(context), GAME_LOG_DIR_NAME), name)
    }

    /**
     * 该游戏本轮的引擎日志文件：`<gameLogDir>/engine-<时间戳>.log`（不创建）。
     */
    fun gameEngineLog(context: Context, gameRootPath: String): File {
        val stamp = synchronized(this) { this.stamp.format(java.util.Date()) }
        return File(gameLogDir(context, gameRootPath), "$GAME_LOG_PREFIX$stamp.log")
    }

    /**
     * 只留该游戏最近 [GAME_LOG_BACKUPS] 份引擎日志，返回删掉的份数。
     *
     * 在**写出新一轮日志之前**调用：保留数就是"含本轮在内"的数量，所以这里是
     * `backups - 1`。按文件名里的时间戳排序而不是 mtime——mtime 会被"打开旧文件"
     * 之类的外部动作改掉，文件名里的时间戳是写入时就定下的。
     */
    fun pruneGameLogs(context: Context, gameRootPath: String, backups: Int = GAME_LOG_BACKUPS): Int {
        return try {
            val dir = gameLogDir(context, gameRootPath)
            val files = dir.listFiles { f -> f.isFile && f.name.startsWith(GAME_LOG_PREFIX) }
                ?.sortedByDescending { it.name } ?: return 0
            var deleted = 0
            files.drop((backups - 1).coerceAtLeast(0)).forEach { if (it.delete()) deleted++ }
            deleted
        } catch (e: Exception) {
            Log.w(TAG, "pruneGameLogs failed", e)
            0
        }
    }

    /**
     * 该游戏全部引擎日志文件（新 → 旧）。
     *
     * 按名字前缀过滤：这个目录将来可能放进别的东西（比如游戏自己吐的崩溃报告），
     * 而排序取"最新一份"的调用方（[recentGameLogs]、[collectForSharing]）必须拿到日志，
     * 不是随便一个新文件。
     */
    fun gameLogFiles(context: Context, gameRootPath: String): List<File> =
        gameLogDir(context, gameRootPath)
            .listFiles { f -> f.isFile && f.name.startsWith(GAME_LOG_PREFIX) }
            ?.sortedByDescending { it.name }
            ?: emptyList()

    /**
     * 最近一次开过的游戏目录（`logs/last-game.txt`），没有则 null。
     *
     * 为什么要落盘：`Application.onCreate` 与崩溃处理器都在"还没有游戏会话"的时刻运行，
     * 只有磁盘上这条记录能告诉它们"上一局是哪个游戏"——异常退出的提示因此能指明游戏，
     * 分享日志也能默认带上那个游戏那一轮。
     *
     * 只存路径，不存标题：标题属于游戏库，日志层不该依赖库文件。
     */
    fun lastGame(context: Context): String? = try {
        val f = File(logsDir(context), LAST_GAME_NAME)
        if (f.isFile) f.readText().trim().takeIf { it.isNotEmpty() } else null
    } catch (e: Exception) {
        Log.w(TAG, "read last-game failed", e)
        null
    }

    /**
     * 记下"这一局跑的是哪个游戏"。开游戏时调用（见 `EngineSession.switchEngineLogToGame`）。
     *
     * 写失败不报错：它只影响提示与默认分享范围，不能挡住开游戏。
     */
    fun setLastGame(context: Context, gameRootPath: String) {
        try {
            File(logsDir(context), LAST_GAME_NAME).writeText(gameRootPath + "\n")
        } catch (e: Exception) {
            Log.w(TAG, "write last-game failed", e)
        }
    }

    /** 上次那个游戏的日志文件（新 → 旧）；没有记录时是空表。 */
    fun lastGameLogs(context: Context): List<File> =
        lastGame(context)?.let { gameLogFiles(context, it) } ?: emptyList()

    /**
     * 最近改动过的几个**游戏日志目录**及其最新一份日志（新 → 旧），供设置页展示
     * "日志在哪"。
     *
     * 为什么按目录 mtime 排而不是只认 `last-game.txt`：这份列表要能在指针文件缺失
     * （首次升级、写失败）时照样工作。游戏日志目录里只有日志文件，最后一次写入时间
     * 就是"最后一次玩它"的近似值——这个用途只需要近似。
     *
     * 目录名是 `<安全名>-<路径哈希>`，从名字反查不到游戏路径，所以第一项返回目录名
     * （比返回空串有用：用户能拿它去 games/ 下面对号）。
     */
    fun recentGameLogs(context: Context, limit: Int = 3): List<Pair<String, File>> {
        return try {
            File(logsDir(context), GAME_LOG_DIR_NAME)
                .listFiles { f -> f.isDirectory }
                ?.sortedByDescending { it.lastModified() }
                ?.mapNotNull { gameDir ->
                    val newest = gameDir.listFiles { f ->
                        f.isFile && f.name.startsWith(GAME_LOG_PREFIX)
                    }?.maxByOrNull { it.name } ?: return@mapNotNull null
                    gameDir.name to newest
                }
                ?.take(limit)
                ?: emptyList()
        } catch (e: Exception) {
            Log.w(TAG, "recentGameLogs failed", e)
            emptyList()
        }
    }

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
     *
     * 另外**最后一个开过的游戏那一轮日志永不删**：每游戏日志按启动轮次切份，正在写的
     * 那份文件名（`engine-<时间戳>.log`）与 [protected] 里的固定名字对不上，只按名字
     * 保护会把它当"最旧的一份"删掉——而那恰好是用户要拿去排障的那一份。
     */
    fun pruneTotal(context: Context, protected: Collection<String>) {
        try {
            val dir = logsDir(context)
            // 统计范围含 `games/` 子树：每游戏日志也占配额，否则它们会无上限增长
            // （目录上限的意义就在"日志总量不失控"）。顶层文件与子树一视同仁，
            // 统一按 mtime 升序删。
            val files = ArrayList<File>()
            dir.listFiles { f -> f.isFile }?.let { files.addAll(it) }
            File(dir, GAME_LOG_DIR_NAME).listFiles { f -> f.isDirectory }?.forEach { gameDir ->
                gameDir.listFiles { f -> f.isFile }?.let { files.addAll(it) }
            }
            val currentGameDir = lastGame(context)?.let { gameLogDir(context, it).absolutePath }
            val ordered = files.sortedBy { it.lastModified() }
            var total = ordered.sumOf { it.length() }
            if (total <= DIR_TOTAL_LIMIT_BYTES) return
            for (f in ordered) {
                if (total <= DIR_TOTAL_LIMIT_BYTES) break
                if (f.name in protected) continue
                if (currentGameDir != null && f.parentFile?.absolutePath == currentGameDir) continue
                val len = f.length()
                if (f.delete()) total -= len
            }
            // 删空的游戏目录顺手收掉，避免目录树里堆一片空壳。
            File(dir, GAME_LOG_DIR_NAME).listFiles { f -> f.isDirectory }?.forEach { gameDir ->
                val left = gameDir.listFiles()
                if (left != null && left.isEmpty()) gameDir.delete()
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
     * 清空日志目录（含崩溃报告与**每游戏日志**），返回删掉的文件数。
     *
     * **只应在没有游戏在跑的时候调用**：引擎正持着当前那份 `engine-*.log` 的 fd，删掉
     * 它只是解除链接，引擎会继续往那个已 unlink 的 inode 写——日志看上去"消失了"，
     * 直到下次开游戏才重新建文件。设置页只在启动器里可达，正是这个前提。
     *
     * 每游戏日志必须一起清：只把顶层清干净、`games/` 原样留着，用户会以为"清空没生效"。
     */
    fun clearAll(context: Context): Int {
        var deleted = 0
        listOf(logsDir(context), crashDir(context)).forEach { dir ->
            dir.listFiles { f -> f.isFile }?.forEach { if (it.delete()) deleted++ }
        }
        // `games/<游戏>/` 下的每游戏日志：先删文件，空目录顺手收掉。
        File(logsDir(context), GAME_LOG_DIR_NAME)
            .listFiles { f -> f.isDirectory }
            ?.forEach { gameDir ->
                gameDir.listFiles { f -> f.isFile }?.forEach { if (it.delete()) deleted++ }
                if (gameDir.listFiles()?.isEmpty() == true) gameDir.delete()
            }
        // 指针文件已被上面删掉；这里只是保证状态一致（否则分享会指向一个空目录）。
        File(logsDir(context), LAST_GAME_NAME).delete()
        return deleted
    }

    /**
     * 分享日志时带上的文件集合：顶层全部（app.log 及轮转份、engine.log 系列、
     * logcat.log、崩溃报告）+ **每个游戏最近 [SHARE_PER_GAME] 份**引擎日志。
     *
     * 每游戏日志按启动轮次切份，把某个游戏的历史全带上会变成几十兆，收件方也读不动；
     * 一轮一份足够定位问题。最近开过的那个游戏排在最前。
     */
    fun collectForSharing(context: Context): List<File> {
        val dir = logsDir(context)
        val out = ArrayList<File>()
        val lastGame = lastGame(context)
        val lastGameDir = lastGame?.let { gameLogDir(context, it).absolutePath }

        lastGame?.let { out.addAll(gameLogFiles(context, it).take(SHARE_PER_GAME)) }
        // 目录名带路径短哈希，无法从名字反查游戏；按 mtime 取最近改动过的几个游戏目录，
        // 这样即使 last-game.txt 没写成功，"刚玩过的那个"也一定在名单里。
        File(dir, GAME_LOG_DIR_NAME).listFiles { f -> f.isDirectory }
            ?.sortedByDescending { it.lastModified() }
            ?.forEach { gameDir ->
                if (gameDir.absolutePath == lastGameDir) return@forEach
                gameDir.listFiles { f -> f.isFile }
                    ?.sortedByDescending { it.name }
                    ?.take(SHARE_PER_GAME)
                    ?.let { out.addAll(it) }
            }
        dir.listFiles { f -> f.isFile }?.let { out.addAll(it) }
        crashDir(context).listFiles { f -> f.isFile }?.let { out.addAll(it) }
        return out
    }
}
