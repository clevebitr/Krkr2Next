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
    /** 是否有子项（侧边栏据此决定要不要画折叠箭头）。 */
    val hasChildren: Boolean = false,
)

/** 侧边栏用的菜单树节点。 */
data class EngineMenuNode(
    val item: EngineMenuItem,
    val children: List<EngineMenuNode>,
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
        val items = text.lineSequence()
            .mapNotNull { line -> parseLine(line) }
            .toList()
        // 子项判定：紧跟在后面、层级更深的那一项就是它的孩子。
        return items.mapIndexed { index, item ->
            val next = items.getOrNull(index + 1)
            item.copy(hasChildren = next != null && next.depth > item.depth)
        }
    }

    /**
     * 按 `depth` 把扁平列表还原成树。depth 跳变（引擎侧不该发生，但配置/版本差异
     * 可能有）时按"挂到最近的合法父节点"处理，不抛异常。
     */
    fun parseTree(text: String): List<EngineMenuNode> = buildTree(parse(text))

    fun buildTree(items: List<EngineMenuItem>): List<EngineMenuNode> {
        if (items.isEmpty()) return emptyList()
        val roots = mutableListOf<MutableNode>()
        // 栈里保存当前路径上"可能成为父节点"的节点，栈顶是最近的。
        val stack = ArrayDeque<MutableNode>()

        items.forEach { item ->
            val node = MutableNode(item)
            while (stack.isNotEmpty() && stack.last().item.depth >= item.depth) {
                stack.removeLast()
            }
            if (stack.isEmpty()) {
                roots += node
            } else {
                stack.last().children += node
            }
            stack.addLast(node)
        }
        return roots.map { it.freeze() }
    }

    private class MutableNode(val item: EngineMenuItem) {
        val children = mutableListOf<MutableNode>()
        fun freeze(): EngineMenuNode =
            EngineMenuNode(item, children.map { it.freeze() })
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
