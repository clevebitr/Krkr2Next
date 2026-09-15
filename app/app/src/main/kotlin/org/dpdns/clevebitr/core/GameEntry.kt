package org.dpdns.clevebitr.core

import java.io.File

/**
 * 「打包镜像 + 同名工程目录」这一对。
 *
 * @param dir 真正的工程目录——能启动的是它。
 * @param archive 本层与 [dir] 同名的 `.xp3`。它是把整个工程重新打成的 xp3 镜像（有的
 *   运行时还会把多个归档串进一个整包），**不是**补丁层，引擎不会拿它去补 [dir]。
 */
data class MirrorPair(val dir: File, val archive: File)

/** [GameEntry.inspect] 的结论。 */
sealed interface EntryVerdict {

    /** 本目录含 `startup.tjs` 或 `.xp3`，可以直接作为入口启动。 */
    data object Entry : EntryVerdict

    /** 本目录不是入口，正确入口是 [entry]。 */
    data class EntryAbove(val entry: File) : EntryVerdict

    /**
     * 本目录不是入口，但 [candidates] 这些直接子目录是。
     *
     * @param mirrors 非空表示这些子目录在本层各有一个同名的 `.xp3` 打包镜像（见
     *   [MirrorPair]）。这里存这一对而不是一个布尔标志，因为提示要点出归档的名字，
     *   而它的名字与被镜像的目录同名、与 [candidates] 的路径不同名。
     */
    data class EntryBelow(
        val candidates: List<File>,
        val mirrors: List<MirrorPair> = emptyList(),
    ) : EntryVerdict

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
 * 1. 本层有 `startup.tjs` 或 `*.xp3`，且本层**不是**"镜像容器"→ 可作为入口。
 * 2. 本层同时有 `<子目录名>.xp3` 与子目录 `<子目录名>/`，且那个子目录自己能启动 →
 *    **子目录**才是入口，本层是打包者留下的分组目录。
 * 3. 都不满足时，先按 2 的镜像关系找，再向上找最近的入口，或在直接子目录里找入口。
 *
 * ## 第 2 条的方向（曾经写反过）
 *
 * 这种布局一眼看上去像"原版目录 + 同名覆盖补丁归档"，于是很容易判成"归档才是打完补丁
 * 的工程，入口在上层"。**实测相反**：设备上 `AZ16417/` 里 2.28 GB 的
 * `nainiuniu5krkr.xp3` 是 PackinOne 把原版 8 个归档串成的整包镜像，882 个文件与
 * `nainiuniu5krkr/data.xp3` 逐字节相同，中文命中数为 2（无关字）；真正的汉化补丁是
 * `nainiuniu5krkr/patch_append2.xp3`（274 个文件含中文），只能从**子目录**启动才挂得上。
 * 更关键的是引擎 `TVPAutoMountSiblingXP3Archives()` 会**跳过**与工程同名的兄弟归档
 * （`StorageImpl.cpp` 里 `baseLower == projBaseStr` 那条 `continue`），所以"上层归档会
 * 被自动挂成补丁层"这件事从来不成立，照着它指路只会把用户送进缺素材的半个工程。
 *
 * 代价上只探测**一层**子目录与有限层父目录：手机上递归全盘扫描不可接受，而用户本来就在
 * 逐级浏览，把结论摆在他眼前比替他扫描便宜得多。
 *
 * 只看文件系统结构，**不解析 `.xp3` 索引**——"归档里到底装了哪几个子归档"要解析索引才
 * 知道，那是引擎侧的事，不该在壳里重做一遍。因此第 1、2 条都只看名字与存在性。
 *
 * 结论按"提示"用，不按"拦截"用：同名规则在极端布局下可能误判，而误判的代价只是一句
 * 话，用户仍可点按钮照常启动。
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

    /** 本层直接含 `startup.tjs` 或 `*.xp3`——不看它是不是"镜像容器"。 */
    private fun hasLaunchableContent(dir: File): Boolean =
        hasStartupScript(dir) || hasArchive(dir)

    /**
     * [dir] 里"本层有同名 `.xp3` 镜像、自己又能启动"的直接子目录。
     *
     * 只认"同名"这个结构，不看具体游戏名，所以换游戏依然成立。判据里用
     * [hasLaunchableContent] 而不是 [isEntry]，否则会与本函数互相递归。
     */
    fun mirroredChildren(dir: File): List<MirrorPair> {
        val children = dir.listFiles() ?: return emptyList()
        val archives = children.asSequence()
            .filter { it.isFile && it.name.endsWith(".xp3", ignoreCase = true) }
            .associateBy { it.name.dropLast(4).lowercase() }
        if (archives.isEmpty()) return emptyList()
        return children.asSequence()
            .filter { it.isDirectory && !it.name.startsWith(".") }
            .mapNotNull { sub ->
                val archive = archives[sub.name.lowercase()] ?: return@mapNotNull null
                if (hasLaunchableContent(sub)) MirrorPair(sub, archive) else null
            }
            .sortedBy { it.dir.name.lowercase() }
            .toList()
    }

    /** 本目录能否**直接**作为入口启动。 */
    fun isEntry(dir: File): Boolean =
        hasLaunchableContent(dir) && mirroredChildren(dir).isEmpty()

    /**
     * 自 [dir] 向外最近的入口：先看上层各层自己是不是入口，再看上层某一层有没有
     * 镜像子目录（那时入口是那个子目录，而不是上层本身）。到 [ANCESTOR_DEPTH] 层或
     * 文件系统根为止。
     */
    fun nearestEntryAbove(dir: File): File? {
        var cur = dir.parentFile
        var depth = 0
        while (cur != null && depth < ANCESTOR_DEPTH) {
            if (isEntry(cur)) return cur
            mirroredChildren(cur).firstOrNull()?.let { return it.dir }
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
        if (isEntry(dir)) return EntryVerdict.Entry

        // 镜像关系优先于"向上找"：分组目录本层也有 .xp3（那个镜像），只看"有归档"会把
        // 它判成入口，只看"向上找"又会把用户送到更外层去。两个都不是他要的。
        val mirrored = mirroredChildren(dir)
        if (mirrored.isNotEmpty()) {
            return EntryVerdict.EntryBelow(mirrored.map { it.dir }, mirrored)
        }

        nearestEntryAbove(dir)?.let { return EntryVerdict.EntryAbove(it) }
        val below = entryChildren(dir)
        if (below.isNotEmpty()) return EntryVerdict.EntryBelow(below)
        return EntryVerdict.None
    }

    /** 当前目录顶部的提示文案；不需要提示时返回 null。 */
    fun hint(verdict: EntryVerdict): String? = when (verdict) {
        EntryVerdict.Entry -> null

        is EntryVerdict.EntryAbove ->
            "此目录不是游戏入口，入口在上层：${verdict.entry.absolutePath}"

        is EntryVerdict.EntryBelow ->
            if (verdict.mirrors.isEmpty()) {
                "此目录不是游戏入口，入口在其下：" +
                    verdict.candidates.joinToString("、") { it.name }
            } else {
                "此目录里的 " + verdict.mirrors.joinToString("、") { it.archive.name } +
                    " 是整包镜像，不是补丁、也不能单独启动；工程目录是同名子目录：" +
                    verdict.candidates.joinToString("、") { it.name } + "。请进入该子目录再启动。"
            }

        EntryVerdict.None ->
            "未在此目录发现 .xp3 或 startup.tjs —— 若游戏在上层目录，请先返回。"
    }

    /**
     * 目录列表中单个条目的短标注；没必要时返回 null。
     *
     * 只在两种情况下标注：能直接启动的目录，以及"本层是镜像容器、入口在它下面"的分组
     * 目录——后者正是用户容易停下来的那一层。非入口的普通子目录
     * （游戏目录下的 `plugin/`、`savedata/`、`全CG存档/`）不挂标注：它们本来就不是要
     * 启动的目标，给每一行都挂一句纯属噪音。
     */
    fun badge(verdict: EntryVerdict): String? = when (verdict) {
        EntryVerdict.Entry -> "可能是游戏目录"
        is EntryVerdict.EntryBelow -> if (verdict.mirrors.isEmpty()) null else "镜像容器 · 入口在其下"
        else -> null
    }
}
