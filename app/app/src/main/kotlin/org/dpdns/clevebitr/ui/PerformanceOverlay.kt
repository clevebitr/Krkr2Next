package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.OverlayConfig
import org.dpdns.clevebitr.core.OverlayCorner
import org.dpdns.clevebitr.core.OverlayField

/**
 * 旧的三档模式（`off` / `summary` / `detail`）。
 *
 * 新配置模型（[OverlayConfig]）用字段集合表达同一件事，这个对象只留给**升级路径**：
 * 老用户存的是档位字符串，`AppPrefs.overlayConfig` 需要把它翻译成字段集合。
 * 不要再往这里加档位——加字段，别加档位。
 */
object PerfOverlayMode {
    const val OFF = "off"
    const val SUMMARY = "summary"
    const val DETAIL = "detail"
}

/**
 * 叠加层一帧的快照。采样在 `GameScreen` 里按 4Hz 做（与 AetherKiri 的
 * `PERF_UPDATE_INTERVAL = 0.25` 一致），本文件只负责排版与格式化，自己不轮询。
 */
data class PerfSnapshot(
    val fps: Float = 0f,
    val frameMs: Float = 0f,
    val tickMs: Float = 0f,
    val p50Ms: Float = 0f,
    val p95Ms: Float = 0f,
    val p99Ms: Float = 0f,
    val maxMs: Float = 0f,
    val errors: Long = 0L,
    val rendererInfo: String = "",
    val memory: EngineSession.MemoryStats? = null,
    /** 当前生效的兼容档（`<profile> <mode>`）；空串表示引擎还没定档。 */
    val compatProfile: String = "",
) {
    /** 宿主每帧除引擎 tick 之外的耗时：对应 AetherKiri detail 档的 `Update`。 */
    val updateMs: Float get() = (frameMs - tickMs).coerceAtLeast(0f)

    /**
     * 单行文本。第一行放"这是一局什么环境"（渲染器 / 兼容档）与帧率，
     * 与 AetherKiri 的 `summaryText` 同序；差异只在数据可得性——它有的
     * Texture/Surface/Fallback 三项 KiriNext 的引擎不暴露，硬凑只会显示假值。
     */
    fun summaryText(fields: Set<OverlayField>): String = joinParts(
        fields,
        OverlayField.RENDERER to { "Renderer: ${rendererInfo.trim()}" },
        OverlayField.COMPAT to { "Compat: $compatProfile" },
        OverlayField.FPS to { "FPS: ${fps.toInt()}" },
        OverlayField.FRAME to { "Frame: ${fmt2(frameMs)} ms" },
        OverlayField.ERRORS to { "Errors: $errors" },
    )

    /**
     * tick / update / 分位数。三者在同一行：它们是同一件事的三个切面（一帧的时间去哪了），
     * 分开显示反而要在三行之间对照。
     */
    fun timingText(fields: Set<OverlayField>): String = joinParts(
        fields,
        OverlayField.TICK to { "Tick: ${fmt2(tickMs)} ms" },
        OverlayField.UPDATE to { "Update: ${fmt2(updateMs)} ms" },
        OverlayField.PERCENTILES to {
            "P50/P95/P99/Max: ${fmt2(p50Ms)} / ${fmt2(p95Ms)} / ${fmt2(p99Ms)} / ${fmt2(maxMs)} ms"
        },
    )

    /**
     * 内存与缓存账目。字段按 KiriNext 的 `engine_memory_stats_t` 实有项填——
     * 它没有进程物理内存字段，所以这里不出现 "App/Peak/Headroom"。
     */
    fun memoryText(): String {
        val m = memory ?: return "Memory: -"
        return buildString {
            append("Memory: Engine ").append(m.selfUsedMb).append(" MB")
            append(" | Sys free ").append(m.systemFreeMb).append(" / ")
            append(m.systemTotalMb).append(" MB")
            append(" | Cache ").append(formatBytes(m.cacheBytes))
            append(" (Gfx ").append(formatBytes(m.graphicCacheBytes))
            append(" / XP3 ").append(formatBytes(m.xp3SegmentCacheBytes))
            append(" / PSB ").append(formatBytes(m.psbCacheBytes)).append(")")
            append(" | PSB ").append(m.psbCacheEntries).append("/")
            append(m.psbCacheEntryLimit)
            append(" (hit ").append(m.psbCacheHits)
            append(" / miss ").append(m.psbCacheMisses).append(")")
            append(" | Archive ").append(m.archiveCacheEntries).append("/")
            append(m.archiveCacheLimit)
            append(" | AutoPath ").append(m.autopathCacheEntries).append("/")
            append(m.autopathCacheLimit)
        }
    }

    /**
     * 按配置产出要显示的若干行（空行不要）。
     *
     * 顺序固定为"环境 → 时间账 → 内存"，不跟随勾选顺序：同一份配置必须每次都长一个样。
     */
    fun lines(config: OverlayConfig): List<String> {
        val fields = config.fields
        return listOf(
            summaryText(fields),
            timingText(fields),
            if (OverlayField.MEMORY in fields) memoryText() else "",
        ).filter { it.isNotBlank() }
    }

    /** 逐项拼接：只加入被勾选且当时确实有内容的项。 */
    private fun joinParts(
        fields: Set<OverlayField>,
        vararg parts: Pair<OverlayField, () -> String>,
    ): String = parts.filter { (field, _) -> field in fields }
        .map { (_, build) -> build() }
        .filter { it.isNotBlank() && !it.endsWith(": ") }
        .joinToString(" | ")
}

/** [OverlayCorner] → Compose 的对齐方式。放在 ui 层：core 不该依赖 Compose。 */
fun OverlayCorner.toAlignment(): Alignment = when (this) {
    OverlayCorner.TOP_START -> Alignment.TopStart
    OverlayCorner.TOP_END -> Alignment.TopEnd
    OverlayCorner.BOTTOM_START -> Alignment.BottomStart
    OverlayCorner.BOTTOM_END -> Alignment.BottomEnd
}

/**
 * 性能叠加层。
 *
 * 排版默认值仍对齐 AetherKiri 的 `PerformancePanel`：固定宽度面板（最小 240dp）、
 * 8dp 圆角、实心 surface + 阴影、12sp 次要色文字、`" | "` 分隔、**没有阈值着色**、
 * 不画曲线、不设 clickable（触摸照常穿透给引擎）。
 *
 * 与 AetherKiri 的差别是**可配置**：显示哪些行、字号倍率、整体不透明度、贴哪个角，
 * 全部来自 [config]（全局默认 + 每游戏覆盖）。位置由调用方通过 `modifier` 里的
 * `align(...)` 决定，本组件不自己找位置。
 */
@Composable
fun PerformanceOverlay(
    config: OverlayConfig,
    snapshot: PerfSnapshot,
    modifier: Modifier = Modifier,
    maxWidth: Dp = 560.dp,
    minWidth: Dp = 240.dp,
) {
    if (!config.visible) return
    val lines = snapshot.lines(config)
    if (lines.isEmpty()) return

    Surface(
        modifier = modifier
            .alpha(config.alpha)
            .widthIn(min = minWidth, max = maxWidth),
        shape = RoundedCornerShape(8.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        tonalElevation = 3.dp,
        shadowElevation = 8.dp,
    ) {
        Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp)) {
            lines.forEach { line ->
                Text(
                    text = line,
                    style = MaterialTheme.typography.labelMedium.copy(
                        // 只改字号，保留 labelMedium 的字重/字距：换整套 style 会让
                        // 面板在 0.6x 与 2.0x 之间看起来像两个不同的组件
                        fontSize = 12.sp * config.fontScale,
                    ),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

/**
 * 设置页里的实时预览：拿假数据按当前配置画一遍。
 *
 * 用假数据而不是真实采样是有意的——设置页也可能在**没开游戏**时打开，那时没有会话，
 * 拿不到任何真实数字；预览的职责是让用户看清"字号/透明度/勾选项"的效果。
 */
@Composable
fun OverlayPreview(config: OverlayConfig, modifier: Modifier = Modifier) {
    Box(modifier = modifier) {
        PerformanceOverlay(
            config = config,
            snapshot = PREVIEW_SNAPSHOT,
            modifier = Modifier.align(config.corner.toAlignment()),
            maxWidth = 420.dp,
            minWidth = 200.dp,
        )
    }
}

private val PREVIEW_SNAPSHOT = PerfSnapshot(
    fps = 59f,
    frameMs = 16.7f,
    tickMs = 9.2f,
    p50Ms = 16.4f,
    p95Ms = 18.1f,
    p99Ms = 21.3f,
    maxMs = 24.9f,
    errors = 0L,
    rendererInfo = "gles3/mali",
    compatProfile = "krkrz-kag kag",
    memory = null,
)

/** 两位小数，固定 `Locale.US`：日志与叠加层的数字不该跟着系统语言变小数点。 */
private fun fmt2(value: Float): String = String.format(Locale.US, "%.2f", value)

/**
 * 字节格式化，口径与 AetherKiri `main.gd:11079 _format_monitor_bytes` 一致：
 * `<=0 → "-"`、`>=1GiB → %.2f GiB`、`>=1MiB → %.0f MiB`、`>=1KiB → %.0f KiB`、否则 `%d B`；
 * **不加千分位**。
 */
internal fun formatBytes(bytes: Long): String {
    val kib = 1L shl 10
    val mib = 1L shl 20
    val gib = 1L shl 30
    return when {
        bytes <= 0L -> "-"
        bytes >= gib -> String.format(Locale.US, "%.2f GiB", bytes.toDouble() / gib)
        bytes >= mib -> String.format(Locale.US, "%.0f MiB", bytes.toDouble() / mib)
        bytes >= kib -> String.format(Locale.US, "%.0f KiB", bytes.toDouble() / kib)
        else -> "$bytes B"
    }
}
