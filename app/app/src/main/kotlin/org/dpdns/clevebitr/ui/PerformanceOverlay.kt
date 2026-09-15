@file:OptIn(ExperimentalMaterial3ExpressiveApi::class)

package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import java.util.Locale
import org.dpdns.clevebitr.core.EngineSession

/** 叠加层档位。与 `AppPrefs.perfOverlayMode` 存的字符串一一对应，也与 AetherKiri 的 `DEBUG_OVERLAY_MODES` 同名同义。 */
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
) {
    /** 宿主每帧除引擎 tick 之外的耗时：对应 AetherKiri detail 档的 `Update`。 */
    val updateMs: Float get() = (frameMs - tickMs).coerceAtLeast(0f)

    /**
     * 第一行：渲染器 + 帧率 + 帧时间 + 错误数。
     *
     * 与 AetherKiri 的差异是**数据可得性**造成的，不是排版省略：它有 Texture/Size/Surface/
     * Fallback 四项（Godot 的帧纹理与渲染器信息），KiriNext 的引擎不暴露这些，硬凑只会
     * 显示假值。渲染器串直接原样显示——KiriNext 的 `engine_get_renderer_info` 返回自己的
     * key=value 串，解析它的键名再拼字段反而更容易随引擎改动而失真。
     */
    fun summaryText(): String = buildString {
        if (rendererInfo.isNotEmpty()) {
            append("Renderer: ").append(rendererInfo.trim()).append(" | ")
        }
        append("FPS: ").append(fps.toInt())
        append(" | Frame: ").append(fmt2(frameMs)).append(" ms")
        append(" | Errors: ").append(errors)
    }

    /**
     * 第二行：内存与缓存账目。对齐 AetherKiri 的 `Memory: ... Cache ...` 位置，但字段按
     * KiriNext 实际有的填——KiriNext 的 `engine_memory_stats_t` 没有进程物理内存字段，
     * 所以这里不出现 "App/Peak/Headroom"。
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

    /** detail 档追加的第三行：tick/update 分解与 1 秒窗分位数。 */
    fun detailText(): String = buildString {
        append("Tick: ").append(fmt2(tickMs)).append(" ms")
        append(" | Update: ").append(fmt2(updateMs)).append(" ms")
        append(" | P50/P95/P99/Max: ")
        append(fmt2(p50Ms)).append(" / ").append(fmt2(p95Ms)).append(" / ")
        append(fmt2(p99Ms)).append(" / ").append(fmt2(maxMs)).append(" ms")
    }
}

/**
 * 性能叠加层。
 *
 * 排版/刷新/交互全部对齐 AetherKiri 的 `PerformancePanel`（`main.gd:2419-2439` 与
 * `_layout_perf_overlay`）：
 * - 左上角，固定宽度面板（它按安全区宽 - 32，最小 240）；
 * - `surface` **实心** + 8dp 圆角 + 阴影，内容边距 16/14——**不是**半透明黑底；
 * - 12sp 次要色文字，**非等宽**（AetherKiri 全局用 Inter，只有日志正文才等宽）；
 * - 字段用 `" | "` 分隔；
 * - **没有阈值着色**（AetherKiri 的 warning/danger 只用于按钮，从不用于 perf 文本）；
 * - **不可拖动、不可缩放、无手势**（它的 `mouse_filter` 是 IGNORE，几何完全由布局决定）；
 * - 不画曲线/直方图（AetherKiri 也只在 detail 档给数字分位数）。
 *
 * 触摸：叠加层不设 clickable，事件照常穿透给引擎（与运行日志浮层不同，那个是可交互的）。
 */
@Composable
fun PerformanceOverlay(
    mode: String,
    snapshot: PerfSnapshot,
    modifier: Modifier = Modifier,
    maxWidth: Dp = 560.dp,
    minWidth: Dp = 240.dp,
) {
    if (mode == PerfOverlayMode.OFF) return

    Surface(
        modifier = modifier.widthIn(min = minWidth, max = maxWidth),
        shape = RoundedCornerShape(8.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        tonalElevation = 3.dp,
        shadowElevation = 8.dp,
    ) {
        Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp)) {
            Text(
                text = snapshot.summaryText(),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = snapshot.memoryText(),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (mode == PerfOverlayMode.DETAIL) {
                Text(
                    text = snapshot.detailText(),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

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
