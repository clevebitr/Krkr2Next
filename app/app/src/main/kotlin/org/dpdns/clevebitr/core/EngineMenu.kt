package org.dpdns.clevebitr.core

/**
 * 游戏注册的一个窗口菜单项（KiriKiri 的 `tTVPMenuItem` / `Window.menu`）。
 *
 * Android 没有系统菜单栏，Windows 版标题栏下方那一栏在触屏上无处显示，于是由壳的
 * 侧边栏列出来。字段与 `engine_list_window_menu` 的序列化一一对应。
 */
data class EngineMenuItem(
    /** 路径 id（顶层 `0`、子项 `0.2`），触发时原样回传。 */
    val id: String,
    /** 显示文本（已去掉制表符/换行）。 */
    val title: String,
    /** 嵌套层级，0 = 顶层；侧边栏据此缩进。 */
    val depth: Int,
    /** 勾选状态（如"全屏"这类可勾选项）。 */
    val checked: Boolean,
    /** 是否可用；不可用的项置灰且不可点。 */
    val enabled: Boolean,
)

/**
 * `engine_list_window_menu` 的文本解析。
 *
 * 格式：每行一项，字段用 `\t` 分隔 —— `depth checked enabled id title`。
 * **任何不完整/非法行直接跳过**：菜单是"有就显示"的附加信息，一行坏掉不该让
 * 整个侧边栏报错。
 */
object EngineMenuParser {

    fun parse(text: String): List<EngineMenuItem> {
        if (text.isEmpty()) return emptyList()
        return text.lineSequence()
            .mapNotNull { line -> parseLine(line) }
            .toList()
    }

    private fun parseLine(line: String): EngineMenuItem? {
        if (line.isBlank()) return null
        // limit = 5：标题里若混进制表符也不会被切开（引擎侧已替换，这里再兜一层）。
        val parts = line.split('\t', limit = 5)
        if (parts.size < 5) return null
        val depth = parts[0].toIntOrNull() ?: return null
        val id = parts[3]
        if (id.isEmpty()) return null
        return EngineMenuItem(
            id = id,
            title = parts[4].ifBlank { "（无标题）" },
            depth = depth,
            checked = parts[1] == "1",
            enabled = parts[2] == "1",
        )
    }
}
