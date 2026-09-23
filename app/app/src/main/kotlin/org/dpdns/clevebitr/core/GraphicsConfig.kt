package org.dpdns.clevebitr.core

import org.json.JSONObject

/**
 * 纹理压缩方式。**落盘的是 [key]**（引擎键 `ogl_compress_tex` 的取值），
 * 已发布的键名不能改。
 *
 * 引擎侧语义：`RenderManager_ogl` 在创建静态纹理时按这个值分发
 * （`CreateStaticTexture2D_auto`），并对 `etc2`/`pvrtc` 做 GL 扩展校验 ——
 * 设备不支持时自动回退到不压缩，不会出错。
 */
enum class TextureCompression(
    val key: String,
    val label: String,
    val detail: String,
) {
    NONE("none", "不压缩", "显存占用最大、兼容性最好（默认）"),
    HALF("half", "半分辨率", "上传时缩到 1/2，显存与带宽减半，画质略降"),
    ETC2("etc2", "ETC2", "压缩纹理，显存约 1/4；设备不支持时自动回退"),
    PVRTC("pvrtc", "PVRTC", "压缩纹理，显存约 1/4；设备不支持时自动回退");

    companion object {
        fun fromKey(key: String?): TextureCompression =
            entries.firstOrNull { it.key == key } ?: NONE
    }
}

/**
 * 引擎内存占用档（引擎键 `memusage`）。它决定引擎认为自己能用的物理内存上限，
 * 进而影响各类缓存（图形缓存 / 归档段缓存）的预算。
 */
enum class MemoryUsage(val key: String, val label: String) {
    UNLIMITED("unlimited", "不限（按设备实际内存）"),
    LOW("low", "低（当作 0）"),
    MEDIUM("medium", "中（128 MB）"),
    HIGH("high", "高（256 MB）");

    companion object {
        fun fromKey(key: String?): MemoryUsage =
            entries.firstOrNull { it.key == key } ?: UNLIMITED
    }
}

/**
 * 图形设置。**既做全局默认，也做每游戏覆盖**（同一个类型，免得两套语义各自漂移）。
 *
 * 每一项都对应一个**引擎已有的配置键**（见 `engine_set_option` 的图形分支）：
 * 壳只负责把值下发，不重新实现渲染逻辑。
 *
 * 注意"生效时机"：
 *  - [textureCompression] / [accurateRender] / [maxTextureSize] 都带惰性缓存，
 *    `engine_set_option` 写入后会显式失效，所以**换游戏即生效**（不必重启应用）；
 *  - [memoryUsage] 在引擎启动（`TVPBeforeSystemInit`）时读一次，同样是**每局生效**。
 */
data class GraphicsConfig(
    val textureCompression: TextureCompression = TextureCompression.NONE,
    /** 精确渲染（引擎键 `ogl_accurate_render`）：关掉"快速 GPU 路径"，走精确软件合成。 */
    val accurateRender: Boolean = false,
    /** 最大纹理尺寸上限，0 = 不覆盖（引擎键 `ogl_max_texsize`）。 */
    val maxTextureSize: Int = 0,
    val memoryUsage: MemoryUsage = MemoryUsage.UNLIMITED,
) {
    /** 全默认 = 与"没有这段配置"等价，`krkr2next.json` 里就不该写它。 */
    val isEmpty: Boolean
        get() = textureCompression == TextureCompression.NONE &&
            !accurateRender && maxTextureSize <= 0 &&
            memoryUsage == MemoryUsage.UNLIMITED

    fun toJson(): JSONObject = JSONObject().apply {
        put(KEY_TEXTURE_COMPRESSION, textureCompression.key)
        put(KEY_ACCURATE_RENDER, accurateRender)
        put(KEY_MAX_TEXTURE_SIZE, maxTextureSize)
        put(KEY_MEMORY_USAGE, memoryUsage.key)
    }

    companion object {
        const val KEY_TEXTURE_COMPRESSION = "textureCompression"
        const val KEY_ACCURATE_RENDER = "accurateRender"
        const val KEY_MAX_TEXTURE_SIZE = "maxTextureSize"
        const val KEY_MEMORY_USAGE = "memoryUsage"

        /** 最大纹理尺寸的可选项（0 = 不覆盖）。 */
        val MAX_TEXTURE_SIZE_CHOICES = listOf(0, 1024, 2048, 4096)

        fun default(): GraphicsConfig = GraphicsConfig()

        /**
         * `json` 为 null（没有这段）时返回 null，表示"继承"——调用方据此区分
         * "用户显式设成了默认值"与"用户没管这一项"。
         */
        fun fromJson(json: JSONObject?): GraphicsConfig? {
            if (json == null) return null
            val size = if (json.has(KEY_MAX_TEXTURE_SIZE)) {
                json.optInt(KEY_MAX_TEXTURE_SIZE, 0).coerceAtLeast(0)
            } else {
                0
            }
            return GraphicsConfig(
                textureCompression = TextureCompression.fromKey(
                    json.optString(KEY_TEXTURE_COMPRESSION, "").takeIf { it.isNotEmpty() },
                ),
                accurateRender = json.optBoolean(KEY_ACCURATE_RENDER, false),
                maxTextureSize = size,
                memoryUsage = MemoryUsage.fromKey(
                    json.optString(KEY_MEMORY_USAGE, "").takeIf { it.isNotEmpty() },
                ),
            )
        }
    }
}
