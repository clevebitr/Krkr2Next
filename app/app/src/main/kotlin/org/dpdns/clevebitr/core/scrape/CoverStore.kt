package org.dpdns.clevebitr.core.scrape

import android.content.Context
import java.io.File
import org.dpdns.clevebitr.core.AppLog

/**
 * 封面下载与缓存。
 *
 * 存应用私有目录（`filesDir/covers/`）而不是游戏目录：封面是壳的数据，写进用户的游戏
 * 目录会污染整合包（有些整合包会校验目录内容），而且 VNDB 的图也不是游戏自带资源。
 *
 * 文件名用 VNDB id（`v12345.jpg`）：同一个游戏重新刮削会**覆盖**同一个文件，
 * 换候选也不会留下一堆孤儿图（下载成功后会清掉同 id 的其它扩展名）。
 */
object CoverStore {

    private const val TAG = "KrKr2Next/Cover"
    private const val DIR_NAME = "covers"

    /** 单张上限。VNDB 封面通常几百 KB，超过这个数说明拿到的不是封面。 */
    private const val MAX_BYTES = 12 * 1024 * 1024

    private val EXTENSIONS = listOf("jpg", "jpeg", "png", "webp")

    fun dir(context: Context): File = File(context.applicationContext.filesDir, DIR_NAME).apply {
        if (!isDirectory) mkdirs()
    }

    fun file(context: Context, name: String): File = File(dir(context), name)

    /** 这个 id 已经缓存过的封面文件，没有就返回 null。 */
    fun existing(context: Context, vndbId: String): File? {
        if (vndbId.isBlank()) return null
        val dir = dir(context)
        return EXTENSIONS.asSequence()
            .map { File(dir, "$vndbId.$it") }
            .firstOrNull { it.isFile && it.length() > 0 }
    }

    /**
     * 下载封面。已有缓存且 [force] 为假时直接返回缓存文件名。
     *
     * 顺序：先原图（[VndbVn.imageUrl]），失败再缩略图（[VndbVn.thumbnailUrl]）。
     * PocketKrKr 是反过来的（先缩略图）——手机上看大图更清楚，而失败回退一样能兜住。
     *
     * @return 文件名（相对 [dir]），全部失败返回 null。**不抛异常**：封面拿不到不该
     *   让"已经刮到的标题/厂商"一起丢掉。
     */
    suspend fun download(context: Context, vn: VndbVn, force: Boolean = false): String? {
        val id = vn.id.takeIf { it.isNotBlank() } ?: return null
        if (!force) {
            existing(context, id)?.let { return it.name }
        }
        for (url in listOf(vn.imageUrl, vn.thumbnailUrl)) {
            if (url.isBlank()) continue
            val bytes = VndbClient.fetchImage(url) ?: continue
            if (bytes.isEmpty() || bytes.size > MAX_BYTES) {
                AppLog.w(TAG, "封面字节数异常（${bytes.size}）：$url")
                continue
            }
            val ext = sniffExtension(bytes, url)
            val target = File(dir(context), "$id.$ext")
            if (writeAtomically(target, bytes)) {
                // 换了扩展名（原图 jpg → 缩略图 png 之类）时清掉旧的，避免同 id 两份
                EXTENSIONS.filter { it != ext }.forEach { File(dir(context), "$id.$it").delete() }
                AppLog.i(TAG, "封面已缓存：${target.name}（${bytes.size / 1024} KB）")
                return target.name
            }
        }
        AppLog.w(TAG, "封面下载失败：${vn.id} ${vn.title}")
        return null
    }

    /** 删除某个 id 的所有缓存（"清除此游戏封面"）。 */
    fun delete(context: Context, vndbId: String): Boolean {
        if (vndbId.isBlank()) return false
        return EXTENSIONS.map { File(dir(context), "$vndbId.$it").delete() }.any { it }
    }

    /**
     * 游戏目录里的封面副本名（`krkr2next.cover.jpg`）。
     *
     * 与 `krkr2next.json` 同前缀同目录，明显是壳自己的文件，不会和游戏资源撞名。
     */
    fun gameDirCoverFile(gameDir: File, coverName: String): File {
        val ext = coverName.substringAfterLast('.', "jpg").lowercase()
            .takeIf { it in EXTENSIONS } ?: "jpg"
        return File(gameDir, "krkr2next.cover.$ext")
    }

    /**
     * 把封面**另存一份**到游戏目录。
     *
     * 为什么需要：私有 `covers/` 会随卸载一起没掉，只把文件名写进 `krkr2next.json`
     * 并不能让封面在重装后回来——用户还是得为了封面重刮一次。副本放在游戏旁边才能
     * 真正"跟着游戏走"。写不进去（只读整合包）就静默跳过，不影响刮削结果。
     */
    fun mirrorToGameDir(context: Context, gameDir: File, coverName: String): Boolean {
        if (coverName.isBlank() || !gameDir.isDirectory) return false
        val src = file(context, coverName)
        if (!src.isFile || src.length() <= 0) return false
        return try {
            val dst = gameDirCoverFile(gameDir, coverName)
            src.copyTo(dst, overwrite = true)
            AppLog.i(TAG, "封面已随配置备份到游戏目录：${dst.absolutePath}")
            true
        } catch (t: Throwable) {
            AppLog.w(TAG, "封面备份到游戏目录失败（不影响刮削）：${gameDir.absolutePath} / $t")
            false
        }
    }

    /**
     * 私有封面丢失时，从游戏目录的副本恢复回去（重装后重新扫描时调用）。
     * @return 是否真的恢复了一份。
     */
    fun restoreFromGameDir(context: Context, gameDir: File, coverName: String): Boolean {
        if (coverName.isBlank()) return false
        if (file(context, coverName).isFile) return false
        val src = gameDirCoverFile(gameDir, coverName)
        if (!src.isFile || src.length() <= 0) return false
        return try {
            dir(context)
            src.copyTo(file(context, coverName), overwrite = true)
            AppLog.i(TAG, "封面已从游戏目录副本恢复：$coverName")
            true
        } catch (t: Throwable) {
            AppLog.w(TAG, "封面恢复失败：$coverName / $t")
            false
        }
    }

    /**
     * 扩展名以**内容**为准，URL 只作兜底：VNDB 的图床给的是 jpg，但 URL 偶尔带查询串，
     * 直接取后缀会得到 `jpg?width=600` 这种不合法文件名。
     */
    private fun sniffExtension(bytes: ByteArray, url: String): String {
        if (bytes.size >= 3 && bytes[0] == 0xFF.toByte() && bytes[1] == 0xD8.toByte() &&
            bytes[2] == 0xFF.toByte()
        ) {
            return "jpg"
        }
        if (bytes.size >= 8 && bytes[0] == 0x89.toByte() && bytes[1] == 'P'.code.toByte() &&
            bytes[2] == 'N'.code.toByte() && bytes[3] == 'G'.code.toByte()
        ) {
            return "png"
        }
        if (bytes.size >= 12 && bytes[0] == 'R'.code.toByte() && bytes[1] == 'I'.code.toByte() &&
            bytes[2] == 'F'.code.toByte() && bytes[3] == 'F'.code.toByte() &&
            bytes[8] == 'W'.code.toByte() && bytes[9] == 'E'.code.toByte()
        ) {
            return "webp"
        }
        val lower = url.substringBefore('?').lowercase()
        return EXTENSIONS.firstOrNull { lower.endsWith(".$it") } ?: "jpg"
    }

    private fun writeAtomically(target: File, bytes: ByteArray): Boolean = try {
        target.parentFile?.mkdirs()
        val tmp = File(target.parentFile, "${target.name}.tmp")
        tmp.writeBytes(bytes)
        if (target.exists()) target.delete()
        val ok = tmp.renameTo(target)
        if (!ok) tmp.delete()
        ok
    } catch (t: Throwable) {
        AppLog.w(TAG, "封面写盘失败：${target.absolutePath} / $t")
        false
    }
}
