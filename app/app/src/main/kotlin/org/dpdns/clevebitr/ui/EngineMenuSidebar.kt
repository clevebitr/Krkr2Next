package org.dpdns.clevebitr.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import org.dpdns.clevebitr.core.EngineMenuItem

/**
 * 引擎菜单侧边栏（§4）。
 *
 * 列出游戏通过 KiriKiri 的 `tTVPMenuItem` / `Window.menu` 注册的窗口菜单项——Windows
 * 版标题栏下方那一栏在 Android 上没有系统菜单栏可放，所以由壳自己渲染。
 *
 * 交互取舍：展开时铺一层**消费触摸**的遮罩，即"打开时暂停游戏输入"。另一条路是让
 * 遮罩穿透、只有条目消费事件，但那样在侧边栏上滑动会误触到下面的游戏，反而更难用。
 *
 * 空态必须显式：很多游戏根本没注册菜单项，空白侧边栏会让人以为功能坏了。
 */
@Composable
fun EngineMenuSidebar(
    items: List<EngineMenuItem>,
    onInvoke: (String) -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.fillMaxSize()) {
        // 遮罩：点击关闭，同时吃掉触摸（见上面的取舍）。
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(0x99000000))
                .clickable(onClick = onClose),
        )

        Surface(
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .fillMaxHeight()
                .width(300.dp),
            color = MaterialTheme.colorScheme.surfaceContainerHigh,
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 4.dp, top = 12.dp, bottom = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        text = "引擎菜单",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.weight(1f),
                    )
                    IconButton(onClick = onClose) {
                        Icon(Icons.Filled.Close, contentDescription = "关闭")
                    }
                }
                HorizontalDivider()

                if (items.isEmpty()) {
                    EmptyEngineMenu()
                } else {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(items, key = { it.id }) { item ->
                            EngineMenuItemRow(
                                item = item,
                                onClick = { if (item.enabled) onInvoke(item.id) },
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun EngineMenuItemRow(item: EngineMenuItem, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (item.enabled) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(
                start = 16.dp + (item.depth.coerceAtLeast(0) * 16).dp,
                end = 16.dp,
                top = 12.dp,
                bottom = 12.dp,
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (item.checked) {
            Icon(
                imageVector = Icons.Filled.Check,
                contentDescription = "已勾选",
                tint = MaterialTheme.colorScheme.primary,
            )
        } else {
            Spacer(modifier = Modifier.width(24.dp))
        }
        Text(
            text = item.title,
            style = MaterialTheme.typography.bodyLarge,
            color = if (item.enabled) {
                MaterialTheme.colorScheme.onSurface
            } else {
                MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
            },
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(start = 8.dp),
        )
    }
}

/** 没有菜单项：说清楚是"游戏没注册"，而不是功能坏了。 */
@Composable
private fun EmptyEngineMenu() {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = androidx.compose.foundation.layout.Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "本游戏没有引擎菜单项",
            style = MaterialTheme.typography.titleSmall,
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "游戏通过 Window.menu 注册的菜单（Windows 版标题栏下方那一栏）会显示在这里。" +
                "本作没有注册，属于正常情况。",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}
