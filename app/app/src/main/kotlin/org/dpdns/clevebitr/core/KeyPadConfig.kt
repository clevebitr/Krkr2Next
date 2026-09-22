package org.dpdns.clevebitr.core

import org.json.JSONArray
import org.json.JSONObject

/**
 * 自定义按键浮层的**一个按钮**。
 *
 * ## 为什么坐标是归一化的
 *
 * `x/y/w/h` 都是相对游戏画面的 `0..1` 比例，不是像素、也不是 dp。浮层要同时覆盖
 * 手机竖屏、手机横屏、平板三种画幅，像素坐标换一个方向就全跑偏；比例坐标在任意
 * 尺寸下都落在同一个相对位置。渲染时再乘当前画面尺寸。
 *
 * ## 为什么落盘键名不能改
 *
 * 与 [OverlayConfig] 同一套纪律：这些键写进 `krkr2next.json` 与 `AppPrefs`，
 * 发布后不能改名或删除，未知键按"忽略"处理，新增只能追加。
 *
 * ## 颜色
 *
 * 颜色是 `0xAARRGGBB` 的 Long（不是 `Int`）。`0xFFFFFFFF` 放进 Int 会溢出成负数，
 * 而 `JSONObject` 读回时按 Long 取，两边用同一个类型最不容易错。
 */
data class KeyButton(
    /** uuid，落盘；按钮增删靠它，不靠下标。 */
    val id: String,
    /** Windows VK 码（见 [VkCodes]），**不是** Android `KEYCODE_*`。 */
    val vk: Int,
    /** 按钮文字；可为空（只显示图标）。 */
    val label: String = "",
    /** MD3 图标的**稳定键名**（见 `ui/KeyPadIcons`），不落盘 `ImageVector` 名。 */
    val iconKey: String? = null,
    val x: Float = 0.05f,
    val y: Float = 0.05f,
    val w: Float = 0.10f,
    val h: Float = 0.10f,
    val textColor: Long = 0xFFFFFFFFL,
    val textSizeSp: Float = 14f,
    val bgColor: Long = 0x66000000L,
    /** 整按钮不透明度，`0.1..1`。 */
    val alpha: Float = 1f,
    val strokeWidthDp: Float = 0f,
    val strokeColor: Long = 0xFFFFFFFFL,
) {
    /**
     * 把越界值夹回合法区间。手改的 JSON、旧版本、跨分辨率都可能给出越界值，
     * 夹紧比抛异常合理——一个坏字段不该让游戏起不来（与 [OverlayConfig] 同）。
     */
    fun sanitized(): KeyButton {
        val cw = w.coerceIn(MIN_SIZE, 1f)
        val ch = h.coerceIn(MIN_SIZE, 1f)
        return copy(
            w = cw,
            h = ch,
            x = x.coerceIn(0f, 1f - cw),
            y = y.coerceIn(0f, 1f - ch),
            textSizeSp = textSizeSp.coerceIn(MIN_TEXT_SP, MAX_TEXT_SP),
            alpha = alpha.coerceIn(MIN_ALPHA, 1f),
            strokeWidthDp = strokeWidthDp.coerceIn(0f, MAX_STROKE_DP),
        )
    }

    /** 拖拽移动：按归一化增量平移，并夹回画面内。 */
    fun moved(dx: Float, dy: Float): KeyButton = copy(x = x + dx, y = y + dy).sanitized()

    /** 右下角把手缩放：改宽高，左上角不动，夹回画面内。 */
    fun resized(dw: Float, dh: Float): KeyButton = copy(w = w + dw, h = h + dh).sanitized()

    fun toJson(): JSONObject = JSONObject().apply {
        put(KEY_ID, id)
        put(KEY_VK, vk)
        put(KEY_LABEL, label)
        iconKey?.let { put(KEY_ICON, it) }
        put(KEY_X, x.toDouble())
        put(KEY_Y, y.toDouble())
        put(KEY_W, w.toDouble())
        put(KEY_H, h.toDouble())
        put(KEY_TEXT_COLOR, textColor)
        put(KEY_TEXT_SIZE, textSizeSp.toDouble())
        put(KEY_BG_COLOR, bgColor)
        put(KEY_ALPHA, alpha.toDouble())
        put(KEY_STROKE_WIDTH, strokeWidthDp.toDouble())
        put(KEY_STROKE_COLOR, strokeColor)
    }

    companion object {
        const val KEY_ID = "id"
        const val KEY_VK = "vk"
        const val KEY_LABEL = "label"
        const val KEY_ICON = "icon"
        const val KEY_X = "x"
        const val KEY_Y = "y"
        const val KEY_W = "w"
        const val KEY_H = "h"
        const val KEY_TEXT_COLOR = "textColor"
        const val KEY_TEXT_SIZE = "textSizeSp"
        const val KEY_BG_COLOR = "bgColor"
        const val KEY_ALPHA = "alpha"
        const val KEY_STROKE_WIDTH = "strokeWidthDp"
        const val KEY_STROKE_COLOR = "strokeColor"

        /** 按钮最小边长（占画面比例）：再小就点不中了。 */
        const val MIN_SIZE = 0.03f
        const val MIN_TEXT_SP = 6f
        const val MAX_TEXT_SP = 72f
        const val MIN_ALPHA = 0.1f
        const val MAX_STROKE_DP = 12f

        /**
         * 从 JSON 读一个按钮。**缺 id 或 vk 非法时返回 null**——这两个字段没有
         * 合理默认值，宁可丢掉这个按钮也不要造出一个点不动的幽灵。
         */
        fun fromJson(json: JSONObject?): KeyButton? {
            if (json == null) return null
            val id = json.optString(KEY_ID, "").takeIf { it.isNotEmpty() } ?: return null
            if (!json.has(KEY_VK)) return null
            val vk = json.optInt(KEY_VK, -1)
            if (vk <= 0) return null
            return KeyButton(
                id = id,
                vk = vk,
                label = json.optString(KEY_LABEL, ""),
                iconKey = json.optString(KEY_ICON, "").takeIf { it.isNotEmpty() },
                x = json.optDouble(KEY_X, 0.05).toFloat(),
                y = json.optDouble(KEY_Y, 0.05).toFloat(),
                w = json.optDouble(KEY_W, 0.10).toFloat(),
                h = json.optDouble(KEY_H, 0.10).toFloat(),
                textColor = json.optLong(KEY_TEXT_COLOR, 0xFFFFFFFFL),
                textSizeSp = json.optDouble(KEY_TEXT_SIZE, 14.0).toFloat(),
                bgColor = json.optLong(KEY_BG_COLOR, 0x66000000L),
                alpha = json.optDouble(KEY_ALPHA, 1.0).toFloat(),
                strokeWidthDp = json.optDouble(KEY_STROKE_WIDTH, 0.0).toFloat(),
                strokeColor = json.optLong(KEY_STROKE_COLOR, 0xFFFFFFFFL),
            ).sanitized()
        }
    }
}

/**
 * 自定义按键浮层的完整配置：**既做每游戏配置，也做全局默认与模板**（同一个类型，
 * 免得三套语义各自漂移）。
 */
data class KeyPadProfile(
    val buttons: List<KeyButton> = emptyList(),
    val enabled: Boolean = false,
) {
    /** 一个按钮都没有时显示出来只是块空白，所以"可见"要同时满足开启与非空。 */
    val visible: Boolean get() = enabled && buttons.isNotEmpty()

    fun withButton(button: KeyButton): KeyPadProfile {
        val clean = button.sanitized()
        val idx = buttons.indexOfFirst { it.id == clean.id }
        return if (idx < 0) {
            copy(buttons = buttons + clean)
        } else {
            copy(buttons = buttons.toMutableList().also { it[idx] = clean })
        }
    }

    fun withoutButton(id: String): KeyPadProfile =
        copy(buttons = buttons.filterNot { it.id == id })

    fun button(id: String): KeyButton? = buttons.firstOrNull { it.id == id }

    fun toJson(): JSONObject = JSONObject().apply {
        put(KEY_ENABLED, enabled)
        put(KEY_BUTTONS, JSONArray(buttons.map { it.toJson() }))
    }

    companion object {
        const val KEY_ENABLED = "enabled"
        const val KEY_BUTTONS = "buttons"

        fun default(): KeyPadProfile = KeyPadProfile()

        /**
         * 从 JSON 读；任何字段缺失或非法都退回默认值，**整体解析不抛异常**。
         * 返回 null 表示传入的 JSON 对象本身不存在（调用方据此区分"没写"与"写了空"）。
         */
        fun fromJson(json: JSONObject?): KeyPadProfile? {
            if (json == null) return null
            val buttons = json.optJSONArray(KEY_BUTTONS)?.let { arr ->
                (0 until arr.length()).mapNotNull { i ->
                    KeyButton.fromJson(arr.optJSONObject(i))
                }
            } ?: emptyList()
            return KeyPadProfile(
                buttons = buttons,
                enabled = json.optBoolean(KEY_ENABLED, false),
            )
        }

        /**
         * 一个可用的起始布局：方向键 + 确认/返回。用户第一次打开浮层时不必从零摆，
         * 直接拖到顺手的位置即可。id 固定（同一份默认布局每次生成结果一致，便于
         * "恢复默认"）。
         */
        fun starter(): KeyPadProfile = KeyPadProfile(
            enabled = true,
            buttons = listOf(
                KeyButton(id = "starter-left", vk = VkCodes.LEFT, iconKey = "arrowLeft",
                    x = 0.04f, y = 0.66f, w = 0.09f, h = 0.16f),
                KeyButton(id = "starter-up", vk = VkCodes.UP, iconKey = "arrowUp",
                    x = 0.14f, y = 0.54f, w = 0.09f, h = 0.16f),
                KeyButton(id = "starter-down", vk = VkCodes.DOWN, iconKey = "arrowDown",
                    x = 0.14f, y = 0.78f, w = 0.09f, h = 0.16f),
                KeyButton(id = "starter-right", vk = VkCodes.RIGHT, iconKey = "arrowRight",
                    x = 0.24f, y = 0.66f, w = 0.09f, h = 0.16f),
                KeyButton(id = "starter-enter", vk = VkCodes.RETURN, label = "确认",
                    x = 0.86f, y = 0.78f, w = 0.11f, h = 0.13f),
                KeyButton(id = "starter-escape", vk = VkCodes.ESCAPE, label = "返回",
                    x = 0.86f, y = 0.60f, w = 0.11f, h = 0.13f),
            ),
        )
    }
}
