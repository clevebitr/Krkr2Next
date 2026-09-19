package org.dpdns.clevebitr.core

import android.content.Context
import java.io.File
import org.dpdns.clevebitr.core.scrape.CoverStore
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
    /** 收藏：库页置顶/筛选用。与"分组"独立——收藏是布尔，分组是标签。 */
    val favorite: Boolean = false,
    /**
     * 分组名（便签式标签，空 = 未分组）。
     *
     * 为什么是单个字符串而不是集合：用户的心智模型是"这个游戏属于哪一类"（如"待玩"、
     * "已通关"），多标签会立刻退化成难用的树；需要更细的维度时用已有的 `tags`（刮削标签）。
     */
    val group: String = "",
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
        // 只在非缺省时写：库文件保持"只写用户真正改过的项"的风格，便于人工比对 diff。
        if (favorite) put(KEY_FAVORITE, true)
        if (group.isNotEmpty()) put(KEY_GROUP, group)
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
        const val KEY_FAVORITE = "favorite"
        const val KEY_GROUP = "group"

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
                favorite = json.optBoolean(KEY_FAVORITE, false),
                group = json.optString(KEY_GROUP, ""),
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
     *
     * **同时从游戏目录的 `krkr2next.json` 读回刮削信息**：刮削结果本来就写在那份
     * 配置里（见 [ScrapeService.syncRecordToGameDir]），但以前只有"读库"这一条路，
     * 重装应用后库是空的，重新扫描只会拿目录名建记录 ⇒ 用户每次重装都得重刮一遍。
     * 这里把配置里的标题/厂商/VNDB id/发售日/标签/简介/封面名与备注并进来，
     * 让"重装 + 重扫"就能恢复，不必再联网刮削。
     */
    fun add(gameDir: File, title: String? = null): AddResult {
        ensureLoaded()
        findByPath(gameDir.absolutePath)?.let { return AddResult(it, added = false) }
        var game = LibraryGame(
            id = GamePaths.stableId(gameDir),
            path = gameDir.absolutePath,
            title = title?.takeIf { it.isNotBlank() } ?: gameDir.name,
            addedAt = System.currentTimeMillis(),
        )
        game = game.withRestoredMetadata(gameDir, explicitTitle = title)
        items += game
        save()
        return AddResult(game, added = true)
    }

    /**
     * 给**库里已有**的记录补回游戏目录里的刮削信息。
     *
     * 覆盖两种情形：
     *   * 本功能上线前就已入库、元数据为空的记录；
     *   * 库文件丢了（重装）后重新扫描，但记录是"先建后补"的。
     *
     * 只补**空字段**，绝不覆盖用户已经改过的内容（[mergeMetadata] 的语义）。
     * @return 实际发生变化的记录数；调用方据此决定要不要刷新列表。
     */
    fun restoreMetadataFromGameDirs(): Int {
        ensureLoaded()
        var changed = 0
        for (index in items.indices) {
            val before = items[index]
            if (!before.needsMetadataRestore()) continue
            val after = before.withRestoredMetadata(File(before.path))
            if (after != before) {
                items[index] = after
                changed++
            }
        }
        if (changed > 0) {
            save()
            AppLog.i(TAG, "从游戏目录的 krkr2next.json 补回元数据：$changed 条")
        }
        return changed
    }

    private fun LibraryGame.withRestoredMetadata(
        gameDir: File,
        explicitTitle: String? = null,
    ): LibraryGame {
        val config = try {
            GameConfigStore.load(appContext, gameDir)
        } catch (t: Throwable) {
            AppLog.w(TAG, "读取游戏配置失败（跳过元数据恢复）：${gameDir.absolutePath} / $t")
            return this
        }
        if (config.metadata.isEmpty && config.notes.isNullOrBlank()) return this
        val merged = mergeMetadata(config.metadata)
        val withNotes = if (notes.isBlank() && !config.notes.isNullOrBlank()) {
            merged.copy(notes = config.notes!!)
        } else {
            merged
        }
        // 私有封面会随卸载消失：能拿回文件名却拿不到图，用户仍得为封面重刮一次。
        // 所以顺手把游戏目录里的副本恢复回私有目录（有副本才做，失败不影响元数据）。
        if (withNotes.coverFile.isNotBlank()) {
            try {
                CoverStore.restoreFromGameDir(appContext, gameDir, withNotes.coverFile)
            } catch (t: Throwable) {
                AppLog.w(TAG, "封面恢复异常（忽略）：${gameDir.absolutePath} / $t")
            }
        }
        // 显式传入的标题优先级最高（用户手动改名/调用方指定）
        return if (!explicitTitle.isNullOrBlank()) withNotes.copy(title = explicitTitle) else withNotes
    }

    /** 库里这份记录还有值得从游戏目录补的字段吗。 */
    private fun LibraryGame.needsMetadataRestore(): Boolean =
        title.isBlank() || developer.isBlank() || vndbId.isBlank() ||
            released.isBlank() || description.isBlank() || coverFile.isBlank() ||
            tags.isEmpty() || notes.isBlank()

    /**
     * 切换收藏状态。返回切换后的值（id 不存在返回 null）。
     *
     * 只改库文件；不碰游戏目录（与 [remove] 同样是"纯库操作"）。
     */
    fun toggleFavorite(id: String): Boolean? {
        val updated = update(id) { it.copy(favorite = !it.favorite) } ?: return null
        return updated.favorite
    }

    /** 设置分组名（空串 = 取消分组）。返回是否更新成功。 */
    fun setGroup(id: String, group: String): Boolean =
        update(id) { it.copy(group = group.trim()) } != null

    /** 库里出现过的分组名（去重、按名称排序），供库页做筛选条。 */
    fun groups(): List<String> {
        ensureLoaded()
        return items.map { it.group }.filter { it.isNotBlank() }.distinct().sorted()
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
