package org.dpdns.clevebitr.core.scrape

import android.content.Context
import java.io.File
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GameConfigStore
import org.dpdns.clevebitr.core.GameLibrary
import org.dpdns.clevebitr.core.GameMetadata
import org.dpdns.clevebitr.core.LibraryGame

/**
 * 刮削流程的编排：搜索 → 排序 → 落库 → 同步进游戏目录的配置。
 *
 * 分成"搜索"与"应用"两步是有意的：候选选择必须由用户拍板（[TitleMatch] 的分数只用于
 * 排序），所以中间那个选择界面不是可省的装饰。
 */
object ScrapeService {

    private const val TAG = "KrKr2Next/Scrape"

    /**
     * 搜索并排序候选。
     *
     * @param query 用户输入或 [TitleMatch.guessQuery] 推断出来的关键词。
     * @param dirName 游戏目录名；用于二次打分与年份提取。
     * @throws ScrapeException 网络/解析失败（UI 直接显示它的 message）。
     */
    suspend fun search(query: String, dirName: String?): List<ScoredCandidate> {
        val vns = VndbClient.search(query)
        val ranked = TitleMatch.rank(query, dirName, vns)
        AppLog.i(
            TAG,
            "刮削搜索 \"$query\"：候选 ${vns.size} 条，最佳 " +
                (ranked.firstOrNull()?.let { "${it.vn.id} ${it.percent}%" } ?: "无"),
        )
        return ranked
    }

    /**
     * 把选中的候选落到库里，并**同步一份到游戏目录的 `krkr2next.json`**。
     *
     * 两处都写是刻意的：库负责列表展示（离开游戏目录也要能看），游戏目录那份保证
     * "换设备后重新扫描目录，标题/厂商/封面 id 还在"。同步失败（目录只读）不影响入库，
     * 只记一条日志——用户仍能在库里看到完整信息。
     *
     * @return 更新后的库记录；游戏不在库里返回 null。
     */
    suspend fun apply(
        context: Context,
        library: GameLibrary,
        gameId: String,
        candidate: ScoredCandidate,
    ): LibraryGame? {
        val game = library.findById(gameId) ?: return null
        val vn = candidate.vn

        val coverFile = CoverStore.download(context, vn)
        val metadata = GameMetadata(
            title = vn.title.takeIf { it.isNotBlank() },
            developer = vn.developers.joinToString("、").takeIf { it.isNotBlank() },
            vndbId = vn.id.takeIf { it.isNotBlank() },
            released = vn.released.takeIf { it.isNotBlank() },
            tags = vn.tags,
            description = vn.description.takeIf { it.isNotBlank() },
            coverFile = coverFile,
            scrapedAt = System.currentTimeMillis(),
        )

        val updated = library.update(gameId) { it.mergeMetadata(metadata) } ?: return null
        syncRecordToGameDir(context, updated)
        AppLog.i(TAG, "刮削已应用：${vn.id} -> ${updated.title}（封面 ${coverFile ?: "无"}）")
        return updated
    }

    /**
     * 把库记录同步进游戏目录的 `krkr2next.json`：**元数据与备注来自库记录，其余段
     * （引擎覆盖、叠加层覆盖）沿用 [base]**。
     *
     * 详情页保存、刮削落库都走这里，两条路径写出来的文件结构因此必然一致。
     *
     * @param base 详情页刚编辑好的配置；为 null 时读盘上现有的那一份。
     */
    fun syncRecordToGameDir(
        context: Context,
        game: LibraryGame,
        base: GameConfig? = null,
    ): GameConfigStore.SaveResult? {
        val dir = File(game.path)
        if (!dir.isDirectory) return null
        return try {
            val config = base ?: GameConfigStore.load(context, dir)
            val result = GameConfigStore.save(
                context,
                dir,
                config.copy(
                    metadata = game.toMetadata(),
                    notes = game.notes.takeIf { it.isNotBlank() },
                ),
            )
            if (!result.ok) AppLog.w(TAG, "配置同步失败：${dir.absolutePath}")
            result
        } catch (t: Throwable) {
            AppLog.w(TAG, "配置同步异常：${dir.absolutePath} / $t")
            null
        }
    }

}
