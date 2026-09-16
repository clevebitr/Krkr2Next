package org.dpdns.clevebitr.core

import android.content.Context
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import org.json.JSONArray
import org.json.JSONObject

/**
 * **每游戏**的壳侧覆盖项。字段为 null 表示"这一项继承全局默认"——这是整套覆盖
 * 机制的核心约定：配置文件只写用户真正改过的项，`krkr2next.json` 因此始终很短，
 * 也不会在全局设置改变后留下一个悄悄生效的旧值。
 */
data class EngineOverride(
    /** 游戏兼容档（`auto` / `kirikiri2-classic` / `krkrz-gpu` / `krkrz-kag` / `krkrz-ogl`）。 */
    val compatProfile: String? = null,
    /** krkrz OGLDrawDevice 兼容档（`off` / `ogl` / `alias` / `kag`）。 */
    val oglDrawDeviceCompat: String? = null,
    /** 帧率上限，0 = 不限速。 */
    val fpsLimit: Int? = null,
    /** 字体回退策略（`auto` / `legacy` / `chain`）。 */
    val fontFallbackMode: String? = null,
) {
    val isEmpty: Boolean
        get() = compatProfile == null && oglDrawDeviceCompat == null &&
            fpsLimit == null && fontFallbackMode == null

    fun toJson(): JSONObject = JSONObject().apply {
        compatProfile?.let { put(KEY_COMPAT_PROFILE, it) }
        oglDrawDeviceCompat?.let { put(KEY_OGLDRAWDEVICE, it) }
        fpsLimit?.let { put(KEY_FPS_LIMIT, it) }
        fontFallbackMode?.let { put(KEY_FONT_FALLBACK, it) }
    }

    companion object {
        const val KEY_COMPAT_PROFILE = "compatProfile"
        const val KEY_OGLDRAWDEVICE = "oglDrawDeviceCompat"
        const val KEY_FPS_LIMIT = "fpsLimit"
        const val KEY_FONT_FALLBACK = "fontFallbackMode"

        fun fromJson(json: JSONObject?): EngineOverride {
            if (json == null) return EngineOverride()
            return EngineOverride(
                compatProfile = json.optString(KEY_COMPAT_PROFILE, "")
                    .takeIf { it in AppPrefs.GAME_COMPAT_PROFILES },
                oglDrawDeviceCompat = json.optString(KEY_OGLDRAWDEVICE, "")
                    .takeIf { it in AppPrefs.OGLDRAWDEVICE_COMPAT_MODES },
                // has() 判断不能省：`optInt` 在键缺失时返回 0，而 0 是合法值（不限速），
                // 直接读会把"没写"当成"设为不限速"。
                fpsLimit = if (json.has(KEY_FPS_LIMIT)) json.optInt(KEY_FPS_LIMIT, 0)
                    .takeIf { it >= 0 } else null,
                fontFallbackMode = json.optString(KEY_FONT_FALLBACK, "")
                    .takeIf { it in AppPrefs.FONT_FALLBACK_MODES },
            )
        }
    }
}

/**
 * 刮削到的游戏信息。**同时写进库里与游戏目录的 `krkr2next.json`**：库负责列表展示，
 * 游戏目录那份保证"换个设备重新扫描目录，标题/厂商/封面 id 还在"。
 *
 * 与 PocketKrKr 的差别是有意的：它只存 6 个字段（标题/厂商/封面/时长），刮削结果
 * 用完即弃；这里保留 VNDB id 与抓取时间，才能判断"要不要重新刮"。
 */
data class GameMetadata(
    val title: String? = null,
    val developer: String? = null,
    /** VNDB 的 `v12345`。有它才谈得上"重新刮这一条"。 */
    val vndbId: String? = null,
    /** 发售日，形如 `2019-03-29`；VNDB 可能只给年或年月，原样保存。 */
    val released: String? = null,
    val tags: List<String> = emptyList(),
    val description: String? = null,
    /** 封面文件名（相对应用私有 `covers/`），不是绝对路径——换设备后路径必然不同。 */
    val coverFile: String? = null,
    /** 抓取时刻（毫秒）；null 表示手填或从未刮过。 */
    val scrapedAt: Long? = null,
) {
    val isEmpty: Boolean
        get() = title == null && developer == null && vndbId == null && released == null &&
            tags.isEmpty() && description == null && coverFile == null

    fun toJson(): JSONObject = JSONObject().apply {
        title?.let { put(KEY_TITLE, it) }
        developer?.let { put(KEY_DEVELOPER, it) }
        vndbId?.let { put(KEY_VNDB_ID, it) }
        released?.let { put(KEY_RELEASED, it) }
        if (tags.isNotEmpty()) put(KEY_TAGS, JSONArray(tags))
        description?.let { put(KEY_DESCRIPTION, it) }
        coverFile?.let { put(KEY_COVER, it) }
        scrapedAt?.let { put(KEY_SCRAPED_AT, it) }
    }

    companion object {
        const val KEY_TITLE = "title"
        const val KEY_DEVELOPER = "developer"
        const val KEY_VNDB_ID = "vndbId"
        const val KEY_RELEASED = "released"
        const val KEY_TAGS = "tags"
        const val KEY_DESCRIPTION = "description"
        const val KEY_COVER = "coverFile"
        const val KEY_SCRAPED_AT = "scrapedAt"

        fun fromJson(json: JSONObject?): GameMetadata {
            if (json == null) return GameMetadata()
            val tags = json.optJSONArray(KEY_TAGS)?.let { arr ->
                (0 until arr.length()).mapNotNull { i ->
                    arr.optString(i, "").takeIf { it.isNotEmpty() }
                }
            } ?: emptyList()
            return GameMetadata(
                title = json.optString(KEY_TITLE, "").takeIf { it.isNotEmpty() },
                developer = json.optString(KEY_DEVELOPER, "").takeIf { it.isNotEmpty() },
                vndbId = json.optString(KEY_VNDB_ID, "").takeIf { it.isNotEmpty() },
                released = json.optString(KEY_RELEASED, "").takeIf { it.isNotEmpty() },
                tags = tags,
                description = json.optString(KEY_DESCRIPTION, "").takeIf { it.isNotEmpty() },
                coverFile = json.optString(KEY_COVER, "").takeIf { it.isNotEmpty() },
                scrapedAt = if (json.has(KEY_SCRAPED_AT)) json.optLong(KEY_SCRAPED_AT) else null,
            )
        }
    }
}

/** 一个游戏的完整壳配置。三段都可以为空——空文件等于"全部继承全局"。 */
data class GameConfig(
    val engine: EngineOverride = EngineOverride(),
    /** null = 叠加层也继承全局。 */
    val overlay: OverlayConfig? = null,
    val metadata: GameMetadata = GameMetadata(),
    /** 用户备注；刮削给不了的信息（汉化组、版本、踩坑记录）放这里。 */
    val notes: String? = null,
) {
    val isEmpty: Boolean
        get() = engine.isEmpty && overlay == null && metadata.isEmpty && notes.isNullOrBlank()

    fun toJson(): JSONObject = JSONObject().apply {
        put(KEY_SCHEMA, SCHEMA)
        if (!engine.isEmpty) put(KEY_ENGINE, engine.toJson())
        overlay?.let { put(KEY_OVERLAY, it.toJson()) }
        if (!metadata.isEmpty) put(KEY_METADATA, metadata.toJson())
        notes?.takeIf { it.isNotBlank() }?.let { put(KEY_NOTES, it) }
    }

    /** 手改过的 JSON 可能带 BOM/换行，`toString(2)` 保证人可读、diff 友好。 */
    fun toPrettyJson(): String = toJson().toString(2)

    companion object {
        const val KEY_SCHEMA = "schema"
        const val KEY_ENGINE = "engine"
        const val KEY_OVERLAY = "overlay"
        const val KEY_METADATA = "metadata"
        const val KEY_NOTES = "notes"

        /** 当前写出的 schema 版本。读到更高版本时不报错，只是按已知字段尽力解析。 */
        const val SCHEMA = 1

        fun fromJson(json: JSONObject?): GameConfig {
            if (json == null) return GameConfig()
            return GameConfig(
                engine = EngineOverride.fromJson(json.optJSONObject(KEY_ENGINE)),
                overlay = OverlayConfig.fromJson(json.optJSONObject(KEY_OVERLAY)),
                metadata = GameMetadata.fromJson(json.optJSONObject(KEY_METADATA)),
                notes = json.optString(KEY_NOTES, "").takeIf { it.isNotEmpty() },
            )
        }
    }
}

/**
 * 每游戏配置文件（`<游戏目录>/krkr2next.json`）的读写。
 *
 * ## 为什么放在游戏目录
 *
 * 用户选的：配置跟着游戏走，换设备/重装应用后仍在。代价是**必须能写用户目录**，
 * 而有些整合包目录是只读的（挂在只读分区、或被文件管理器设了权限），所以这里
 * 有一条私有回退：写不进去就写 `<filesDir>/gameconfig/<路径哈希>.json`，
 * 读的时候两份都看（游戏目录优先）。回退发生时 UI 要如实告诉用户"这份配置存在应用里"，
 * 否则他会以为配置丢了。
 *
 * ## 为什么探测要实写
 *
 * `File.canWrite()` 在 Android 上并不可靠（FUSE 权限、SELinux、SD 卡只读挂载都会
 * 骗过它），而"探测失败"的代价只是一次多余的私有写入。所以这里真的建一个临时文件
 * 再删掉，并把结果按路径缓存——每个游戏只探一次。
 */
object GameConfigStore {

    const val FILE_NAME = "krkr2next.json"

    private const val PROBE_NAME = ".krkr2next_probe"

    /** 每个游戏目录只实写探测一次；键是 canonicalPath。 */
    private val writableCache = ConcurrentHashMap<String, Boolean>()

    fun gameDirFile(gameDir: File): File = File(gameDir, FILE_NAME)

    fun fallbackDir(context: Context): File = File(context.filesDir, "gameconfig")

    /** 私有回退文件。命名用路径哈希：同一个游戏目录在不同挂载路径下应落到同一个文件。 */
    fun fallbackFile(context: Context, gameDir: File): File =
        File(fallbackDir(context), "${GamePaths.stableId(gameDir)}.json")

    fun hasGameDirFile(gameDir: File): Boolean = gameDirFile(gameDir).isFile

    /**
     * 读配置：游戏目录那份优先，没有就退回私有那份。**任何解析失败都返回空配置**
     * ——一个坏文件绝不该让游戏起不来。
     */
    fun load(context: Context, gameDir: File): GameConfig {
        val fromGame = readConfig(gameDirFile(gameDir))
        if (fromGame != null) return fromGame
        return readConfig(fallbackFile(context, gameDir)) ?: GameConfig()
    }

    private fun readConfig(file: File): GameConfig? {
        if (!file.isFile) return null
        return try {
            val text = file.readText()
            if (text.isBlank()) return null
            GameConfig.fromJson(JSONObject(text))
        } catch (t: Throwable) {
            AppLog.w(TAG, "配置读取失败（按空配置处理）：${file.absolutePath} / $t")
            null
        }
    }

    /** 配置写到哪了。UI 必须把 [PRIVATE] 如实显示出来。 */
    enum class Target { GAME_DIR, PRIVATE }

    data class SaveResult(val target: Target, val file: File, val ok: Boolean)

    /**
     * 写配置。先试游戏目录，不可写就落私有回退。
     *
     * 写盘走"临时文件 + rename"：配置文件被中断写坏的话，下一次读会得到空配置，
     * 用户会以为设置全丢了；rename 在同一个文件系统上是原子的。
     */
    fun save(context: Context, gameDir: File, config: GameConfig): SaveResult {
        val text = config.toPrettyJson()
        if (isWritable(gameDir)) {
            val target = gameDirFile(gameDir)
            if (writeAtomically(target, text)) return SaveResult(Target.GAME_DIR, target, true)
        }
        val fallback = fallbackFile(context, gameDir)
        fallback.parentFile?.mkdirs()
        val ok = writeAtomically(fallback, text)
        if (!ok) AppLog.e(TAG, "配置写入失败（游戏目录与应用私有都失败）：${gameDir.absolutePath}")
        return SaveResult(Target.PRIVATE, fallback, ok)
    }

    /** 删除两份文件（"重置此游戏配置"）。返回是否删掉了游戏目录那份。 */
    fun delete(context: Context, gameDir: File): Boolean {
        val inGame = gameDirFile(gameDir).delete()
        fallbackFile(context, gameDir).delete()
        return inGame
    }

    /** 这个游戏目录能不能写；结果按路径缓存。 */
    fun isWritable(gameDir: File): Boolean {
        if (!gameDir.isDirectory) return false
        val key = GamePaths.canonicalKey(gameDir)
        writableCache[key]?.let { return it }
        val probe = File(gameDir, PROBE_NAME)
        val ok = try {
            if (probe.createNewFile()) {
                probe.delete()
                true
            } else {
                false
            }
        } catch (t: Throwable) {
            false
        }
        writableCache[key] = ok
        AppLog.i(TAG, "配置可写探测：${gameDir.absolutePath} -> $ok")
        return ok
    }

    private fun writeAtomically(target: File, text: String): Boolean = try {
        val tmp = File(target.parentFile, "${target.name}.tmp")
        tmp.writeText(text)
        if (target.exists() && !target.delete()) {
            tmp.delete()
            false
        } else {
            val renamed = tmp.renameTo(target)
            if (!renamed) tmp.delete()
            renamed
        }
    } catch (t: Throwable) {
        AppLog.w(TAG, "配置写入失败：${target.absolutePath} / $t")
        false
    }

    private const val TAG = "KrKr2Next/GameConfig"
}

/**
 * 壳侧设置的**全局默认**。从 `AppPrefs` 读出来后传进 [GameConfig.resolve]，
 * 这样合并逻辑不依赖 `Context`，可以单独测。
 */
data class GlobalDefaults(
    val compatProfile: String,
    val oglDrawDeviceCompat: String,
    val fpsLimit: Int,
    val fontFallbackMode: String,
    val overlay: OverlayConfig,
)

/** 合并后的结果：壳各处（启动、叠加层）只认它。 */
data class ResolvedShellSettings(
    val compatProfile: String,
    val oglDrawDeviceCompat: String,
    val fpsLimit: Int,
    val fontFallbackMode: String,
    val overlay: OverlayConfig,
)

/** 逐项合并：每游戏写了用它的，没写用全局默认。 */
fun GameConfig.resolve(global: GlobalDefaults): ResolvedShellSettings = ResolvedShellSettings(
    compatProfile = engine.compatProfile ?: global.compatProfile,
    oglDrawDeviceCompat = engine.oglDrawDeviceCompat ?: global.oglDrawDeviceCompat,
    fpsLimit = engine.fpsLimit ?: global.fpsLimit,
    fontFallbackMode = engine.fontFallbackMode ?: global.fontFallbackMode,
    overlay = overlay ?: global.overlay,
)

/**
 * 路径 → 稳定 id / 键的小工具。
 *
 * 用路径的 SHA-1 前 16 位：库里的 id、私有配置文件名、私有封面名都靠它保持一致，
 * 而且不含用户路径的明文（日志与导出文件里不该出现设备路径）。
 */
object GamePaths {

    fun stableId(gameDir: File): String = sha1Hex(canonicalKey(gameDir)).take(16)

    /**
     * 归一化后的路径键。canonicalPath 解析符号链接，避免 `/sdcard/x` 与
     * `/storage/emulated/0/x` 被当成两个游戏（内容完全相同）。
     */
    fun canonicalKey(gameDir: File): String = try {
        gameDir.canonicalPath
    } catch (t: Throwable) {
        gameDir.absolutePath
    }

    private fun sha1Hex(text: String): String = try {
        val md = java.security.MessageDigest.getInstance("SHA-1")
        md.digest(text.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
    } catch (t: Throwable) {
        // 理论上不会发生；退化成 hashCode 也比抛异常好
        Integer.toHexString(text.hashCode())
    }
}
