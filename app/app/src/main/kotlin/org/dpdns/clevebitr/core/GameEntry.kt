package org.dpdns.clevebitr.core

import java.io.File

/** [GameEntry.inspect] 的结论。 */
sealed interface EntryVerdict {

    /** 本目录含 `startup.tjs` 或 `.xp3`，可以直接作为入口启动。 */
    data object Entry : EntryVerdict

    /**
     * 本目录不是入口，正确入口是 [entry]。
     *
     * @param shadowingArchive 非 null 表示原因是"上层存在与本目录同名的 `.xp3`"：那个归档
     *   才是打完补丁的完整工程，本目录是未打补丁的原版。这里存归档**本身**而不是一个布尔
     *   标志，因为提示要点出它的名字，而它的名字与被遮蔽目录同名、与 [entry] 不同名。
     */
    data class EntryAbove(val entry: File, val shadowingArchive: File? = null) : EntryVerdict {
        val shadowed: Boolean get() = shadowingArchive != null
    }

    /** 本目录不是入口，但 [candidates] 这些直接子目录是。 */
    data class EntryBelow(val candidates: List<File>) : EntryVerdict

    /** 既不像入口，上下也没找到入口。 */
    data object None : EntryVerdict
}

/**
 * 目录作为"游戏入口"的判定，以及"不是入口时入口在哪"。
 *
 * ## 为什么壳侧要判
 *
 * 引擎只在**被选中目录的当前层**找 `startup.tjs` 与 `.xp3`，不会替用户去上层或下层翻。
 * 用户点错一层，症状就是"游戏打不开"，而他手上没有任何线索——这不是引擎的错，是壳
 * 没把入口讲清楚。
 *
 * ## 判据
 *
 * 1. 本层有 `startup.tjs` 或 `*.xp3` → 可作为入口。
 * 2. 上层有 `<本目录名>.xp3` 的同名归档 → 本目录**不是**入口。这种布局是"原版目录 +
 *    同名覆盖补丁归档"：补丁把整个工程（含根 `startup.tjs`）重新打成一个归档，与未打
 *    补丁的原版目录并排；引擎从父目录启动才能把它当工程挂载。这条规则**不看具体名字**，
 *    只看"同名"这个结构，所以换游戏依然成立。
 * 3. 都不满足时，向上找最近的入口，或在直接子目录里找入口。
 *
 * 代价上只探测**一层**子目录与有限层父目录：手机上递归全盘扫描不可接受，而用户本来就在
 * 逐级浏览，把结论摆在他眼前比替他扫描便宜得多。
 *
 * 只看文件系统结构，**不解析 `.xp3` 索引**——"归档里有没有根 `startup.tjs`"要解析索引才
 * 知道，那是引擎侧 `AutoMountProjectXP3` 的事，不该在壳里重做一遍。
 *
 * 因此结论按"提示"用，不按"拦截"用：同名规则在极端布局下可能误判，而误判的代价只是一句
 * 话，用户仍可照常启动。
 */
object GameEntry {

    /** 向上探测的层数上限。够覆盖"游戏根目录外面还有两三层分组目录"的常见打包方式。 */
    private const val ANCESTOR_DEPTH = 6

    /** 本层直接含 `startup.tjs`。 */
    fun hasStartupScript(dir: File): Boolean = File(dir, "startup.tjs").isFile

    /** 本层直接含 `*.xp3`。 */
    fun hasArchive(dir: File): Boolean {
        val children = dir.list() ?: return false
        return children.any { it.endsWith(".xp3", ignoreCase = true) }
    }

    /** 父目录中与 [dir] 同名的 `.xp3`；没有则 null。大小写不敏感。 */
    fun shadowingArchive(dir: File): File? {
        val parent = dir.parentFile ?: return null
        val children = parent.list() ?: return null
        val wanted = dir.name + ".xp3"
        val hit = children.firstOrNull { it.equals(wanted, ignoreCase = true) } ?: return null
        return File(parent, hit)
    }

    /** 本目录能否**直接**作为入口启动。 */
    fun isEntry(dir: File): Boolean =
        shadowingArchive(dir) == null && (hasStartupScript(dir) || hasArchive(dir))

    /** 自 [dir] 向上最近的入口；到 [ANCESTOR_DEPTH] 层或文件系统根为止。 */
    fun nearestEntryAbove(dir: File): File? {
        var cur = dir.parentFile
        var depth = 0
        while (cur != null && depth < ANCESTOR_DEPTH) {
            if (isEntry(cur)) return cur
            cur = cur.parentFile
            depth++
        }
        return null
    }

    /** [dir] 的直接子目录中的入口，按名字排序。 */
    fun entryChildren(dir: File): List<File> =
        dir.listFiles()
            ?.filter { it.isDirectory && !it.name.startsWith(".") && isEntry(it) }
            ?.sortedBy { it.name.lowercase() }
            ?: emptyList()

    fun inspect(dir: File): EntryVerdict {
        // 同名归档必须排在"本层有 .xp3 就算入口"之前：原版目录本身有整套 .xp3，只看这一条
        // 会把它判成入口，正好落进用户踩的那个坑。
        val shadow = shadowingArchive(dir)
        if (shadow != null) {
            return EntryVerdict.EntryAbove(nearestEntryAbove(dir) ?: dir.parentFile!!, shadow)
        }
        if (hasStartupScript(dir) || hasArchive(dir)) return EntryVerdict.Entry
        nearestEntryAbove(dir)?.let { return EntryVerdict.EntryAbove(it) }
        val below = entryChildren(dir)
        if (below.isNotEmpty()) return EntryVerdict.EntryBelow(below)
        return EntryVerdict.None
    }

    /** 当前目录顶部的提示文案；不需要提示时返回 null。 */
    fun hint(verdict: EntryVerdict): String? = when (verdict) {
        EntryVerdict.Entry -> null

        is EntryVerdict.EntryAbove -> when (val archive = verdict.shadowingArchive) {
            null -> "此目录不是游戏入口，入口在上层：${verdict.entry.absolutePath}"
            else ->
                "此目录是未打补丁的原版：上层有同名的 ${archive.name}，" +
                    "那是打完补丁的完整工程，引擎只能从上层挂载它。请返回上层启动。"
        }

        is EntryVerdict.EntryBelow ->
            "此目录不是游戏入口，入口在其下：" +
                verdict.candidates.joinToString("、") { it.name }

        EntryVerdict.None ->
            "未在此目录发现 .xp3 或 startup.tjs —— 若游戏在上层目录，请先返回。"
    }

    /**
     * 目录列表中单个条目的短标注；没必要时返回 null。
     *
     * 只在"被同名补丁归档遮蔽"时标注。非遮蔽的"入口在上层"**不**放到列表行上：
     * 游戏目录下的 plugin/、savedata/、全CG存档/ 全都不是入口，给每一行都挂一句
     * "入口在上层"纯属噪音——用户本来也不是要启动它们；真正需要提醒的是那个看起来
     * 和正常入口一模一样的原版目录。
     */
    fun badge(verdict: EntryVerdict): String? = when (verdict) {
        EntryVerdict.Entry -> "可能是游戏目录"
        is EntryVerdict.EntryAbove -> if (verdict.shadowed) "原版目录 · 入口在上层" else null
        else -> null
    }
}
