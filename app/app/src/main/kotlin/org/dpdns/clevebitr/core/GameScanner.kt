package org.dpdns.clevebitr.core

import java.io.File

/**
 * 在用户选定的目录里批量找游戏入口。
 *
 * ## 为什么不用 `GameEntry.inspect` 单打独斗
 *
 * [GameEntry] 的回答是给"用户当前站在哪一层"用的：它会向上找（[EntryVerdict.EntryAbove]）
 * 也会只往下看一层。批量扫描要的是另一件事——**在一棵树里把所有入口找齐**，
 * 所以这里自己走 BFS，只借用 [GameEntry.isEntry] 与 [GameEntry.mirroredChildren]
 * 两条硬判据：
 *
 * - 本层能直接启动 → 它就是入口，**不再往下**（游戏目录里的 `plugin/`、
 *   `savedata/` 不该被当成候选）。
 * - 本层是"镜像容器"（同名 `.xp3` + 同名子目录）→ 入口是同名子目录，加它、不往下。
 * - 其余目录继续往下，直到 [maxDepth]。
 *
 * 扫描是**只读**的：只看名字与存在性，不打开归档、不读文件内容。手机上递归全盘扫描
 * 不可接受，所以两层上限（深度 + 结果数）都写死在这里，并且支持取消——
 * 用户按返回键时不该等它扫完。
 */
object GameScanner {

    /** 默认往下几层。够覆盖"分组目录/整合包目录 → 游戏目录"的常见打包方式。 */
    const val DEFAULT_DEPTH = 3

    /** 默认结果上限。一次加几百个游戏既没意义，也会让列表页卡住。 */
    const val DEFAULT_LIMIT = 200

    /**
     * 不往下走的目录名。两类：点开头（`.git`、`.claude` 这类元数据目录），
     * 以及游戏**内部**的目录——它们里面不会再有入口，走下去只是浪费时间。
     */
    private val SKIP_NAMES = setOf("savedata", "plugin", "cache", "logs")

    data class Result(
        val found: List<File>,
        /** 实际看过的目录数，用来判断"扫得动吗"。 */
        val scanned: Int,
        /** 撞到 [limit] 提前结束。 */
        val truncated: Boolean,
        val cancelled: Boolean,
    )

    /**
     * 扫 [root]。
     *
     * @param maxDepth 相对 [root] 的层数（[root] 自己算第 0 层）。
     * @param limit 结果数上限。
     * @param shouldCancel 每次进入一个目录前问一次；为 true 立即返回已找到的结果。
     * @param onProgress 每看一个目录回调一次，供 UI 显示"正在看哪个目录"。
     */
    fun scan(
        root: File,
        maxDepth: Int = DEFAULT_DEPTH,
        limit: Int = DEFAULT_LIMIT,
        shouldCancel: () -> Boolean = { false },
        onProgress: (File) -> Unit = {},
    ): Result {
        val found = LinkedHashMap<String, File>()
        var scanned = 0
        var truncated = false
        var cancelled = false

        val queue = ArrayDeque<Pair<File, Int>>()
        queue += root to 0

        while (queue.isNotEmpty()) {
            if (shouldCancel()) {
                cancelled = true
                break
            }
            val (dir, depth) = queue.removeFirst()
            scanned++
            onProgress(dir)

            if (GameEntry.isEntry(dir)) {
                found[GamePaths.canonicalKey(dir)] = dir
                if (found.size >= limit) {
                    truncated = true
                    break
                }
                continue
            }

            // 镜像容器：入口是同名子目录，加完不再往下（容器本层的 .xp3 是整包镜像）
            val mirrors = GameEntry.mirroredChildren(dir)
            if (mirrors.isNotEmpty()) {
                for (pair in mirrors) {
                    found[GamePaths.canonicalKey(pair.dir)] = pair.dir
                    if (found.size >= limit) {
                        truncated = true
                        break
                    }
                }
                if (truncated) break
                continue
            }

            if (depth >= maxDepth) continue
            val children = dir.listFiles() ?: continue
            for (child in children) {
                if (!child.isDirectory) continue
                if (child.name.startsWith(".")) continue
                if (child.name.lowercase() in SKIP_NAMES) continue
                queue += child to (depth + 1)
            }
        }

        // 按路径排序：同一棵树两次扫描的结果顺序必须一致，否则 UI 每次都在跳
        val sorted = found.values.sortedBy { it.absolutePath.lowercase() }
        AppLog.i(
            TAG,
            "扫描 ${root.absolutePath}：看过 $scanned 个目录，命中 ${sorted.size} 个入口" +
                (if (truncated) "（达到上限 $limit，提前结束）" else "") +
                (if (cancelled) "（用户取消）" else ""),
        )
        return Result(sorted, scanned, truncated, cancelled)
    }

    /** 结果条目的副标题：相对扫描根的位置，让用户知道它是从哪一层找出来的。 */
    fun relativeLabel(root: File, entry: File): String {
        val rootPath = root.absolutePath.trimEnd('/')
        val path = entry.absolutePath
        return if (path.startsWith(rootPath) && path.length > rootPath.length) {
            path.substring(rootPath.length).trimStart('/').ifEmpty { entry.name }
        } else {
            path
        }
    }

    private const val TAG = "KrKr2Next/Scanner"
}
