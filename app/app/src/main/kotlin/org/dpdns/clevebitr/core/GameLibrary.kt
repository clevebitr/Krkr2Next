package org.dpdns.clevebitr.core

import android.content.Context
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * 库里的一条游戏。
 *
 * [path] 是**权威字段**：其余都可以被刮削覆盖、被用户手改，路径不行——它决定启动哪个
 * 目录，改了就变成另一条记录。[id] 由路径哈希得来（见 [GamePaths.stableId]），
 * 因此"移出库再重新加回来"会得到同一个 id，封面与私有配置能对上。
 *
 * 与 PocketKrKr 的字段差异是有意的：它只有 6 个字段且**每次刮削整条重写**；这里把
 * `addedAt` / `lastPlayedAt` 与刮削字段分开，刮削失败不会连"什么时候加的、玩过几次"一起丢。
 */
data class LibraryGame(
    val id: String,
    val path: String,
    val title: String,
    val developer: String = "",
    val vndbId: String = "",
    val released: String = "",
    val tags: List<String> = emptyList(),
    val description: String = "",
    /** 封面文件名（`covers/` 下），空 = 没有封面。 */
    val coverFile: String = "",
    val notes: String = "",
    val addedAt: Long = 0L,
    val lastPlayedAt: Long = 0L,
    val playCount: Int = 0,
) {
    val dir: File get() = File(path)

    /** 列表页的副标题：厂商 + 发售年，都没有就不占位。 */
    fun subtitle(): String {
        val year = released.take(4).takeIf { it.length == 4 && it.all(Char::isDigit) }
        return listOfNotNull(
            developer.takeIf { it.isNotBlank() },
            year,
        ).joinToString(" · ")
    }

    fun toJson(): JSONObject = JSONObject().apply {
        put(KEY_ID, id)
        put(KEY_PATH, path)
        put(KEY_TITLE, title)
        if (developer.isNotEmpty()) put(KEY_DEVELOPER, developer)
        if (vndbId.isNotEmpty()) put(KEY_VNDB_ID, vndbId)
        if (released.isNotEmpty()) put(KEY_RELEASED, released)
        if (tags.isNotEmpty()) put(KEY_TAGS, JSONArray(tags))
        if (description.isNotEmpty()) put(KEY_DESCRIPTION, description)
        if (coverFile.isNotEmpty()) put(KEY_COVER, coverFile)
        if (notes.isNotEmpty()) put(KEY_NOTES, notes)
        put(KEY_ADDED_AT, addedAt)
        put(KEY_LAST_PLAYED_AT, lastPlayedAt)
        put(KEY_PLAY_COUNT, playCount)
    }

    /** 把刮削结果并进来；[metadata] 里为 null 的字段保持原值（手填的不该被 null 冲掉）。 */
    fun mergeMetadata(metadata: GameMetadata): LibraryGame = copy(
        title = metadata.title?.takeIf { it.isNotBlank() } ?: title,
        developer = metadata.developer ?: developer,
        vndbId = metadata.vndbId ?: vndbId,
        released = metadata.released ?: released,
        tags = metadata.tags.ifEmpty { tags },
        description = metadata.description ?: description,
        coverFile = metadata.coverFile ?: coverFile,
    )

    /** 反向：库记录 → 可写进 `krkr2next.json` 的元数据段。 */
    fun toMetadata(): GameMetadata = GameMetadata(
        title = title.takeIf { it.isNotBlank() },
        developer = developer.takeIf { it.isNotBlank() },
        vndbId = vndbId.takeIf { it.isNotBlank() },
        released = released.takeIf { it.isNotBlank() },
        tags = tags,
        description = description.takeIf { it.isNotBlank() },
        coverFile = coverFile.takeIf { it.isNotBlank() },
    )

    companion object {
        const val KEY_ID = "id"
        const val KEY_PATH = "path"
        const val KEY_TITLE = "title"
        const val KEY_DEVELOPER = "developer"
        const val KEY_VNDB_ID = "vndbId"
        const val KEY_RELEASED = "released"
        const val KEY_TAGS = "tags"
        const val KEY_DESCRIPTION = "description"
        const val KEY_COVER = "coverFile"
        const val KEY_NOTES = "notes"
        const val KEY_ADDED_AT = "addedAt"
        const val KEY_LAST_PLAYED_AT = "lastPlayedAt"
        const val KEY_PLAY_COUNT = "playCount"

        /**
         * 从 JSON 读一条。**缺 path 或缺 title 就返回 null**（这两项没有合理默认值），
         * 其余字段缺失一律有默认值——库文件是从旧版本升上来的，不该因为少一个键就丢记录。
         */
        fun fromJson(json: JSONObject): LibraryGame? {
            val path = json.optString(KEY_PATH, "").takeIf { it.isNotBlank() } ?: return null
            val title = json.optString(KEY_TITLE, "").takeIf { it.isNotBlank() }
                ?: File(path).name.takeIf { it.isNotBlank() }
                ?: return null
            val tags = json.optJSONArray(KEY_TAGS)?.let { arr ->
                (0 until arr.length()).mapNotNull { i -> arr.optString(i, "").takeIf { it.isNotEmpty() } }
            } ?: emptyList()
            return LibraryGame(
                id = json.optString(KEY_ID, "").takeIf { it.isNotBlank() }
                    ?: GamePaths.stableId(File(path)),
                path = path,
                title = title,
                developer = json.optString(KEY_DEVELOPER, ""),
                vndbId = json.optString(KEY_VNDB_ID, ""),
                released = json.optString(KEY_RELEASED, ""),
                tags = tags,
                description = json.optString(KEY_DESCRIPTION, ""),
                coverFile = json.optString(KEY_COVER, ""),
                notes = json.optString(KEY_NOTES, ""),
                addedAt = json.optLong(KEY_ADDED_AT, 0L),
                lastPlayedAt = json.optLong(KEY_LAST_PLAYED_AT, 0L),
                playCount = json.optInt(KEY_PLAY_COUNT, 0),
            )
        }
    }
}

/**
 * 游戏库的持久化。
 *
 * ## 为什么不是 SharedPreferences
 *
 * PocketKrKr 把整库塞进 SharedPreferences 的一个字符串键（`game_manager.dart:10`），
 * 后果是**一条坏记录会让整库读不出来**（它的 `listFromJsonString` 外层 catch 直接返回
 * 空表）。这里改成文件 + **逐条解析**：坏记录进"隔离区"，仍然原样写回，用户不会因为
 * 一条记录格式不对就丢掉整个库。
 *
 * ## 线程
 *
 * 文件只有几 KB，同步读写足够；调用方（Activity / 组合函数）在主线程调即可。
 * **不要**放进每帧路径——`touch()` 会写盘。
 */
class GameLibrary(context: Context) {

    private val appContext = context.applicationContext
    private val file: File = File(appContext.filesDir, FILE_NAME)
    private val items = mutableListOf<LibraryGame>()

    /** 解析不了的原始记录：原样保留、原样写回，绝不静默删除用户数据。 */
    private val quarantined = mutableListOf<JSONObject>()

    private var loaded = false

    fun games(): List<LibraryGame> {
        ensureLoaded()
        return items.toList()
    }

    fun findById(id: String): LibraryGame? {
        ensureLoaded()
        return items.firstOrNull { it.id == id }
    }

    fun findByPath(path: String): LibraryGame? {
        ensureLoaded()
        val key = GamePaths.canonicalKey(File(path))
        return items.firstOrNull { GamePaths.canonicalKey(File(it.path)) == key }
    }

    /** 已经在库里的（按归一化路径判重，符号链接/别名路径不会被当成两个游戏）。 */
    fun contains(gameDir: File): Boolean = findByPath(gameDir.absolutePath) != null

    /** [add] 的结果。[added] 让调用方（批量扫描）能如实统计，不必自己再猜一遍。 */
    data class AddResult(val game: LibraryGame, val added: Boolean)

    /**
     * 加入库。[title] 为空时用目录名——目录名常常就是游戏名，比空白强。
     * 已存在则原样返回（不覆盖用户改过的标题），并把 [AddResult.added] 置 false。
     */
    fun add(gameDir: File, title: String? = null): AddResult {
        ensureLoaded()
        findByPath(gameDir.absolutePath)?.let { return AddResult(it, added = false) }
        val game = LibraryGame(
            id = GamePaths.stableId(gameDir),
            path = gameDir.absolutePath,
            title = title?.takeIf { it.isNotBlank() } ?: gameDir.name,
            addedAt = System.currentTimeMillis(),
        )
        items += game
        save()
        return AddResult(game, added = true)
    }

    fun remove(id: String): Boolean {
        ensureLoaded()
        val removed = items.removeAll { it.id == id }
        if (removed) save()
        return removed
    }

    /** 通用更新：返回更新后的记录；id 不存在返回 null。 */
    fun update(id: String, transform: (LibraryGame) -> LibraryGame): LibraryGame? {
        ensureLoaded()
        val index = items.indexOfFirst { it.id == id }
        if (index < 0) return null
        val updated = transform(items[index])
        items[index] = updated
        save()
        return updated
    }

    /** 记一次启动。最后一个字段让列表能按"最近玩过"排序。 */
    fun touch(id: String) {
        update(id) {
            it.copy(
                lastPlayedAt = System.currentTimeMillis(),
                playCount = it.playCount + 1,
            )
        }
    }

    private fun ensureLoaded() {
        if (loaded) return
        loaded = true
        if (!file.isFile) return
        try {
            val root = JSONObject(file.readText())
            val arr = root.optJSONArray(KEY_GAMES) ?: return
            for (i in 0 until arr.length()) {
                val obj = arr.optJSONObject(i)
                if (obj == null) {
                    AppLog.w(TAG, "库记录 #$i 不是对象，已隔离保留")
                    continue
                }
                val game = try {
                    LibraryGame.fromJson(obj)
                } catch (t: Throwable) {
                    AppLog.w(TAG, "库记录 #$i 解析失败，已隔离保留：$t")
                    null
                }
                if (game == null) {
                    quarantined += obj
                } else {
                    items += game
                }
            }
            AppLog.i(TAG, "库加载完成：${items.size} 条（隔离 ${quarantined.size} 条）")
        } catch (t: Throwable) {
            AppLog.e(TAG, "库文件读取失败，本次按空库启动（文件保留）：$t")
        }
    }

    /** 写盘。原子替换：写坏一次等于整库丢失，不值得赌。 */
    private fun save() {
        val arr = JSONArray()
        items.forEach { arr.put(it.toJson()) }
        quarantined.forEach { arr.put(it) }
        val root = JSONObject().apply {
            put(KEY_SCHEMA, SCHEMA)
            put(KEY_GAMES, arr)
        }
        try {
            val tmp = File(file.parentFile, "$FILE_NAME.tmp")
            tmp.writeText(root.toString(2))
            if (file.exists()) file.delete()
            if (!tmp.renameTo(file)) {
                tmp.delete()
                AppLog.e(TAG, "库写盘失败（rename 失败）")
            }
        } catch (t: Throwable) {
            AppLog.e(TAG, "库写盘失败：$t")
        }
    }

    companion object {
        private const val TAG = "KrKr2Next/Library"
        private const val FILE_NAME = "library.json"
        private const val KEY_SCHEMA = "schema"
        private const val KEY_GAMES = "games"
        private const val SCHEMA = 1
    }
}
