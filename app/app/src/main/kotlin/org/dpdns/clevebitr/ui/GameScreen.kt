package org.dpdns.clevebitr.ui

import android.annotation.SuppressLint
import android.os.Build
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlin.math.roundToInt
import kotlinx.coroutines.delay
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.InputEvent
import org.dpdns.clevebitr.core.NativeEngine

private const val TAG = "KrKr2Next/Game"

/**
 * 游戏画面。
 *
 * 只负责**渲染 surface 与触摸**；引擎生命周期、按键转发由 `MainActivity` 持有，
 * 因为 Activity 能可靠地拿到 `dispatchKeyEvent` 与 `onPause/onResume`。
 *
 * 用 `AndroidView` + [SurfaceView]（而不是 Compose 的 `AndroidExternalSurface`）：
 * 引擎用 `eglSwapBuffers` 直出到 surface buffer，这条零拷贝路径要求 SurfaceView
 * 系的独立 surface——TextureView 走合成路径，不适用。
 *
 * **不要**在这里用 Compose 的 `Modifier.pointerInput` 转发触摸：那样坐标是 Compose
 * 坐标系，需要额外换算。这里直接把 `OnTouchListener` 挂在 SurfaceView 上，
 * `MotionEvent.getX()/getY()` 就是视图坐标（物理像素），正是引擎期望的输入。
 *
 * 右下角有一个悬浮按钮：点开是两项菜单（显示运行时日志 / 退出游戏）。日志浮层是
 * 半透明的，内容取 [AppLog] 的内存环形缓冲——引擎日志经 `EngineSession.onLog`
 * 也汇进那里，所以浮层里看到的是壳与引擎混排的真实时序。
 * 菜单或浮层打开时，SurfaceView 的触摸监听会直接吞掉事件（见下面的 `setOnTouchListener`），
 * 否则点浮层会连带把一次 POINTER_DOWN 送进游戏。
 */
@Composable
fun GameScreen(
    session: EngineSession,
    startupState: Int,
    statusText: String,
    onExit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    // 设置里改的是"下次启动游戏生效"，这里读一次即可
    val showFps = remember { AppPrefs.showFps(context) }

    var fps by remember { mutableStateOf(0f) }
    if (showFps) {
        LaunchedEffect(session) {
            while (true) {
                fps = session.measuredFps
                delay(500)
            }
        }
    }

    // 悬浮菜单与运行时日志浮层
    var menuOpen by remember { mutableStateOf(false) }
    var logsVisible by remember { mutableStateOf(false) }
    var logLines by remember { mutableStateOf<List<String>>(emptyList()) }

    // 日志由任意线程写入 AppLog，这里按固定节拍取快照——不要在组合里直接读，
    // 否则每次重组都要去抢那把锁，而且没有"变了"的信号可依赖。
    LaunchedEffect(logsVisible) {
        if (!logsVisible) return@LaunchedEffect
        while (true) {
            logLines = AppLog.recent()
            delay(300)
        }
    }

    Box(modifier = modifier.fillMaxSize().background(Color.Black)) {

        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { context ->
                SurfaceView(context).apply {
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) = Unit

                        override fun surfaceChanged(
                            holder: SurfaceHolder,
                            format: Int,
                            width: Int,
                            height: Int,
                        ) {
                            session.attachSurface(holder.surface, width, height)
                        }

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            session.detachSurface()
                        }
                    })
                    setOnTouchListener { _, event ->
                        // 菜单或日志浮层打开时吞掉触摸：否则点浮层会连带把
                        // POINTER_DOWN 送进游戏。menuOpen/logsVisible 是 Compose
                        // State，闭包每次读到的都是当时的值。
                        if (!menuOpen && !logsVisible) handleTouch(session, event)
                        true
                    }
                }
            },
        )

        // FPS 叠加：不加背景的话在浅色画面上读不出来；不设 clickable，触摸照样穿透给引擎
        if (showFps) {
            Text(
                text = "FPS ${(fps * 10).roundToInt() / 10f}",
                color = Color.White,
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(8.dp)
                    .background(Color(0f, 0f, 0f, 0.6f))
                    .padding(horizontal = 6.dp, vertical = 2.dp),
            )
        }

        // 引擎出第一帧前的进度覆盖层
        if (startupState != NativeEngine.STARTUP_SUCCEEDED) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator()
                    Text(
                        text = statusText,
                        color = Color.White,
                        modifier = Modifier.padding(top = 16.dp),
                    )
                }
            }
        }

        // 悬浮按钮 + 展开菜单。放右下角，避开左上角的 FPS。
        // 收起/展开共用同一个按钮：展开后图标变成叉，再点即收起——不引入额外的
        // 全屏遮罩，免得和 SurfaceView 的触摸派发纠缠。
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(16.dp),
        ) {
            Column(horizontalAlignment = Alignment.End) {
                if (menuOpen) {
                    GameMenu(
                        onShowLogs = {
                            menuOpen = false
                            logLines = AppLog.recent()
                            logsVisible = true
                        },
                        onExit = {
                            menuOpen = false
                            onExit()
                        },
                    )
                    Spacer(Modifier.height(12.dp))
                }
                FloatingActionButton(
                    onClick = { menuOpen = !menuOpen },
                    containerColor = Color(0xCC1F1F1F),
                    contentColor = Color.White,
                ) {
                    Icon(
                        imageVector = if (menuOpen) Icons.Filled.Close else Icons.Filled.MoreVert,
                        contentDescription = if (menuOpen) "收起菜单" else "游戏菜单",
                    )
                }
            }
        }

        // 运行时日志浮层：必须放最后，才是压在其它叠加层之上的那一层
        if (logsVisible) {
            RuntimeLogOverlay(
                lines = logLines,
                onClose = { logsVisible = false },
            )
        }
    }
}

/**
 * 悬浮菜单面板。两项都用纯文字，不引图标——少一个图标名就对不上依赖版本的风险。
 */
@Composable
private fun GameMenu(onShowLogs: () -> Unit, onExit: () -> Unit) {
    Card(colors = CardDefaults.cardColors(containerColor = Color(0xE61F1F1F))) {
        Column {
            MenuEntry(label = "显示运行时日志", onClick = onShowLogs)
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(Color(0x33FFFFFF)),
            )
            MenuEntry(label = "退出游戏", onClick = onExit)
        }
    }
}

@Composable
private fun MenuEntry(label: String, onClick: () -> Unit) {
    Text(
        text = label,
        color = Color.White,
        style = MaterialTheme.typography.labelLarge,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 14.dp),
    )
}

/**
 * 半透明的运行时日志浮层。
 *
 * 触摸已经在 `GameScreen` 的 SurfaceView 监听里按 `logsVisible` 拦掉了，所以这里
 * 不需要再消费手势——少一层和 AndroidView 互操作纠缠的机会。
 *
 * 自动跟随：只有用户没有往上翻（`canScrollForward` 为假说明已在底部）时才滚到末尾，
 * 否则看历史日志会被新日志不断顶走。
 */
@Composable
private fun RuntimeLogOverlay(lines: List<String>, onClose: () -> Unit) {
    val listState = rememberLazyListState()

    LaunchedEffect(lines.size) {
        if (lines.isNotEmpty() && !listState.canScrollForward) {
            listState.scrollToItem(lines.lastIndex)
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xD9000000)),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(12.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "运行时日志（近 ${lines.size} 行）",
                    color = Color.White,
                    style = MaterialTheme.typography.titleSmall,
                    modifier = Modifier.weight(1f),
                )
                IconButton(onClick = onClose) {
                    Icon(
                        imageVector = Icons.Filled.Close,
                        contentDescription = "关闭日志",
                        tint = Color.White,
                    )
                }
            }
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(Color(0x33FFFFFF)),
            )
            if (lines.isEmpty()) {
                Text(
                    text = "暂无日志",
                    color = Color(0xFFAAAAAA),
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 12.dp),
                )
            } else {
                LazyColumn(
                    state = listState,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(top = 6.dp),
                ) {
                    items(lines) { line ->
                        Text(
                            text = line,
                            color = Color(0xFFD0D0D0),
                            fontFamily = FontFamily.Monospace,
                            fontSize = 11.sp,
                            lineHeight = 15.sp,
                            modifier = Modifier.padding(vertical = 1.dp),
                        )
                    }
                }
            }
        }
    }
}

/**
 * MotionEvent → `engine_input_event_t`。
 *
 * 坐标直接用 `getX()/getY()`（视图坐标 = 物理像素），**不要**乘 density：
 * 引擎侧 `DrawDevice::TransformToPrimaryLayerManager` 负责物理像素 → layer 坐标。
 * 详见 `README.md`「硬约束」。
 */
/**
 * 触摸轨迹的聚合状态。
 *
 * move 是高频事件（一秒几十条），逐条记日志会把它彻底淹掉——高频日志必须
 * 采样/限频/仅边沿。这里的做法是：按下记一条、抬起记一条汇总
 * （时长 / move 次数 / 位移），移动本身只更新计数。
 *
 * 触摸事件都来自 UI 线程的同一条事件流，因此这些字段不需要同步。
 */
private object TouchStats {
    var active = false
    var downAt = 0L
    var startX = 0f
    var startY = 0f
    var lastX = 0f
    var lastY = 0f
    var moveCount = 0

    fun begin(x: Float, y: Float) {
        active = true
        downAt = System.currentTimeMillis()
        startX = x
        startY = y
        lastX = x
        lastY = y
        moveCount = 0
    }

    fun trace(x: Float, y: Float) {
        moveCount++
        lastX = x
        lastY = y
    }

    fun summary(): String {
        val ms = System.currentTimeMillis() - downAt
        val dx = lastX - startX
        val dy = lastY - startY
        return "touch up: ${ms}ms, move=${moveCount}, from=(${startX.toInt()},${startY.toInt()})" +
            " to=(${lastX.toInt()},${lastY.toInt()}) d=(${dx.toInt()},${dy.toInt()})"
    }
}

@SuppressLint("ClickableViewAccessibility")
private fun handleTouch(session: EngineSession, event: MotionEvent) {
    // 按钮映射：0=左 1=右 2=中（对应 tTVPMouseButton）
    val button = when {
        event.buttonState and MotionEvent.BUTTON_SECONDARY != 0 -> 1
        event.buttonState and MotionEvent.BUTTON_TERTIARY != 0 -> 2
        else -> 0
    }
    val pointerId = event.getPointerId(0)

    when (event.actionMasked) {
        MotionEvent.ACTION_DOWN -> {
            TouchStats.begin(event.x, event.y)
            AppLog.i(TAG, "touch down (${event.x.toInt()},${event.y.toInt()}) button=$button ptr=$pointerId")
            session.sendInput(
                InputEvent.POINTER_DOWN,
                x = event.x.toDouble(), y = event.y.toDouble(),
                pointerId = pointerId, button = button,
            )
        }

        MotionEvent.ACTION_MOVE -> {
            TouchStats.trace(event.x, event.y)
            // 一个 MotionEvent 可能合并了多个采样点，逐点补发以免丢失轨迹
            for (i in 0 until event.historySize) {
                session.sendInput(
                    InputEvent.POINTER_MOVE,
                    x = event.getHistoricalX(0, i).toDouble(),
                    y = event.getHistoricalY(0, i).toDouble(),
                    pointerId = pointerId, button = button,
                )
            }
            session.sendInput(
                InputEvent.POINTER_MOVE,
                x = event.x.toDouble(), y = event.y.toDouble(),
                pointerId = pointerId, button = button,
            )
        }

        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
            if (TouchStats.active) {
                AppLog.i(TAG, TouchStats.summary())
                TouchStats.active = false
            }
            session.sendInput(
                InputEvent.POINTER_UP,
                x = event.x.toDouble(), y = event.y.toDouble(),
                pointerId = pointerId, button = button,
            )
        }

        MotionEvent.ACTION_SCROLL ->
            session.sendInput(
                InputEvent.POINTER_SCROLL,
                x = event.x.toDouble(), y = event.y.toDouble(),
                deltaY = event.getAxisValue(MotionEvent.AXIS_VSCROLL).toDouble(),
                pointerId = pointerId,
            )

        // 外接鼠标滚轮在 API < 26 走 HOVER_MOVE
        MotionEvent.ACTION_HOVER_MOVE ->
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
                val vscroll = event.getAxisValue(MotionEvent.AXIS_VSCROLL)
                if (vscroll != 0f) {
                    session.sendInput(
                        InputEvent.POINTER_SCROLL,
                        x = event.x.toDouble(), y = event.y.toDouble(),
                        deltaY = vscroll.toDouble(),
                        pointerId = pointerId,
                    )
                }
            }
    }
}
