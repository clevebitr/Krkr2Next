package org.dpdns.clevebitr.core

import org.json.JSONArray
import org.json.JSONObject

/**
 * 性能叠加层可显示的指标。
 *
 * [key] 会**落盘**（全局默认存在 `AppPrefs`，每游戏覆盖存在 `krkr2next.json`），
 * 所以已发布的键名不能再改、也不能删：旧配置里的未知键按"忽略"处理，
 * 新增项只能追加。中文名 [label] 只用于设置界面，不落盘。
 *
 * 每一项都写清"这个数字从哪来"，因为它们来源不同：前三项与 [ERRORS] 由壳自己
 * 按帧统计，[MEMORY] / [RENDERER] / [COMPAT] 是引擎 C ABI 的采样结果。
 * **不提供壳拿不到的项**——`PerfSnapshot` 里没有的字段硬凑只会显示假值。
 */
enum class OverlayField(val key: String, val label: String) {

    /** 壳在一秒滑动窗里数出来的帧率。 */
    FPS("fps", "FPS"),

    /** 一帧从 Choreographer 回调到提交的总耗时。 */
    FRAME("frame", "帧时"),

    /** 上面那段里交给引擎 `engine_tick` 的部分。 */
    TICK("tick", "引擎 tick"),

    /** 帧时减 tick：宿主自己的开销（采样、组合、提交）。 */
    UPDATE("update", "宿主开销"),

    /** 一秒窗内的分位数：P50 / P95 / P99 / Max。 */
    PERCENTILES("percentiles", "分位数"),

    /** 引擎侧内存与缓存账目（`engine_get_memory_stats`）。 */
    MEMORY("memory", "内存/缓存"),

    /** 渲染器信息串（`engine_get_renderer_info`），原样显示不解析。 */
    RENDERER("renderer", "渲染器"),

    /** 当前生效的游戏兼容档（`engine_get_compat_profile`），如 `aetherkiri alias`。 */
    COMPAT("compat", "兼容档"),

    /** 壳侧统计的 tick 失败次数；非 0 才值得看，但允许常显。 */
    ERRORS("errors", "错误数");

    companion object {
        fun fromKey(key: String?): OverlayField? = entries.firstOrNull { it.key == key }
    }
}

/** 叠加层贴哪个角。四角是手机横屏游戏里唯一不会被游戏 UI 长期遮住的位置。 */
enum class OverlayCorner(val key: String, val label: String) {
    TOP_START("topStart", "左上"),
    TOP_END("topEnd", "右上"),
    BOTTOM_START("bottomStart", "左下"),
    BOTTOM_END("bottomEnd", "右下");

    companion object {
        fun fromKey(key: String?): OverlayCorner =
            entries.firstOrNull { it.key == key } ?: TOP_START
    }
}

/**
 * 叠加层外观与字段的完整配置。**既做全局默认，也做每游戏覆盖**（同一个类型，
 * 免得两套语义各自漂移）。
 *
 * 不变量：`fontScale` 与 `alpha` 进构造就夹到合法区间。越界值只可能来自手改的
 * JSON 或旧版本，夹紧比抛异常合理——一个坏字段不该让游戏起不来。
 */
data class OverlayConfig(
    val enabled: Boolean = true,
    val fields: Set<OverlayField> = DEFAULT_FIELDS,
    /** 字号倍率，`0.6`–`2.0`。 */
    val fontScale: Float = 1f,
    /** 面板不透明度，`0.2`–`1.0`；太小会看不清，太大挡画面。 */
    val alpha: Float = 0.85f,
    val corner: OverlayCorner = OverlayCorner.TOP_START,
) {
    init {
        require(fontScale in FONT_SCALE_RANGE) { "fontScale 越界" }
        require(alpha in ALPHA_RANGE) { "alpha 越界" }
    }

    /** 字段按**枚举声明顺序**输出：勾选顺序不该影响排版，否则同一份配置会有两种样子。 */
    val orderedFields: List<OverlayField>
        get() = OverlayField.entries.filter { it in fields }

    /** 一个字段都没勾时不显示（比显示一个空面板诚实）。 */
    val visible: Boolean get() = enabled && fields.isNotEmpty()

    fun withFontScale(value: Float) = copy(fontScale = clampFloat(value, FONT_SCALE_RANGE))
    fun withAlpha(value: Float) = copy(alpha = clampFloat(value, ALPHA_RANGE))

    fun toJson(): JSONObject = JSONObject().apply {
        put(KEY_ENABLED, enabled)
        put(KEY_FIELDS, JSONArray(orderedFields.map { it.key }))
        put(KEY_FONT_SCALE, fontScale.toDouble())
        put(KEY_ALPHA, alpha.toDouble())
        put(KEY_CORNER, corner.key)
    }

    companion object {
        const val KEY_ENABLED = "enabled"
        const val KEY_FIELDS = "fields"
        const val KEY_FONT_SCALE = "fontScale"
        const val KEY_ALPHA = "alpha"
        const val KEY_CORNER = "corner"

        val FONT_SCALE_RANGE = 0.6f..2.0f
        val ALPHA_RANGE = 0.2f..1.0f

        /** 默认勾选：够日常看，又不至于一进游戏就糊一片。 */
        val DEFAULT_FIELDS: Set<OverlayField> =
            setOf(OverlayField.FPS, OverlayField.FRAME, OverlayField.COMPAT,
                  OverlayField.ERRORS, OverlayField.MEMORY)

        fun default(): OverlayConfig = OverlayConfig()

        /**
         * 从 JSON 读；**任何字段缺失或非法都退回该字段的默认值**，不抛异常。
         * 返回 null 表示传入的 JSON 对象本身不存在（调用方据此区分"没写"与"写了空"）。
         */
        fun fromJson(json: JSONObject?): OverlayConfig? {
            if (json == null) return null
            val def = default()
            val fields = json.optJSONArray(KEY_FIELDS)?.let { arr ->
                (0 until arr.length()).mapNotNull { i ->
                    OverlayField.fromKey(arr.optString(i, null))
                }.toSet()
            } ?: def.fields
            return OverlayConfig(
                enabled = json.optBoolean(KEY_ENABLED, def.enabled),
                fields = fields,
                fontScale = clampFloat(
                    json.optDouble(KEY_FONT_SCALE, def.fontScale.toDouble()).toFloat(),
                    FONT_SCALE_RANGE,
                ),
                alpha = clampFloat(
                    json.optDouble(KEY_ALPHA, def.alpha.toDouble()).toFloat(),
                    ALPHA_RANGE,
                ),
                corner = OverlayCorner.fromKey(json.optString(KEY_CORNER, "").takeIf { it.isNotEmpty() }),
            )
        }

        /**
         * 旧的三档模式（`off` / `summary` / `detail`）→ 新配置。
         *
         * 升级路径：老用户存的是档位字符串，第一次打开设置页时把它翻译成字段集合，
         * 之后一律以新配置为准。档位键保留只读不改，回退旧版本仍能用。
         */
        fun fromLegacyMode(mode: String): OverlayConfig = when (mode) {
            "summary" -> OverlayConfig(
                fields = setOf(
                    OverlayField.RENDERER, OverlayField.COMPAT, OverlayField.FPS,
                    OverlayField.FRAME, OverlayField.ERRORS, OverlayField.MEMORY,
                ),
            )

            "detail" -> OverlayConfig(fields = OverlayField.entries.toSet())

            else -> OverlayConfig(enabled = false, fields = DEFAULT_FIELDS)
        }
    }
}

/** 把 [value] 夹进 [range]；NaN 也当越界处理。 */
internal fun clampFloat(value: Float, range: ClosedFloatingPointRange<Float>): Float = when {
    value.isNaN() -> range.start
    value < range.start -> range.start
    value > range.endInclusive -> range.endInclusive
    else -> value
}
