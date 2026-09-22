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
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SmallFloatingActionButton
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
import org.dpdns.clevebitr.core.EngineMenuItem
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.KeyPadProfile
import org.dpdns.clevebitr.core.OverlayConfig
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
    /**
     * 本次会话生效的叠加层配置（全局默认与每游戏覆盖**已在启动时合并**）。
     * **由 MainActivity 持有并传进来**，不在本文件里读偏好设置：游戏内菜单能直接打开
     * 设置页，改完要立刻生效，而 `remember { AppPrefs... }` 只在首次组合时读一次，
     * 改了要退出重进才看得到。
     */
    overlayConfig: OverlayConfig,
    /** 本次会话生效的自定义按键浮层（全局默认与每游戏覆盖**已在启动时合并**）。 */
    keypadConfig: KeyPadProfile,
    /** 按键浮层编辑态：为真时浮层接管全部触摸（游戏收不到），并显示拖拽/缩放把手。 */
    keypadEditing: Boolean,
    onKeypadChange: (KeyPadProfile) -> Unit,
    onKeypadEditingChange: (Boolean) -> Unit,
    /** 光标触控板模式：手指变触控板，相对移动驱动虚拟光标。 */
    touchpadMode: Boolean,
    touchpadSensitivity: Float,
    onTouchpadModeChange: (Boolean) -> Unit,
    /** 右下角是否显示「引擎菜单」按钮（§4 侧边栏）。 */
    engineMenuButton: Boolean,
    onEngineMenuButtonChange: (Boolean) -> Unit,
    /** 打开设置页。设置页会盖在游戏之上，引擎与 SurfaceView 不被销毁。 */
    onOpenSettings: () -> Unit,
    onExit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // 性能叠加层：4Hz 采样，与 AetherKiri 的 PERF_UPDATE_INTERVAL = 0.25s 对齐。
    // 关档时不启动这个循环——不采样，也不调那两次 JNI。
    var perf by remember { mutableStateOf(PerfSnapshot()) }
    // 只勾了"错误数"时也要采样，所以判据是 visible（enabled + 至少一个字段），
    // 不是旧版的 mode != off
    if (overlayConfig.visible) {
        LaunchedEffect(session, overlayConfig) {
            while (true) {
                perf = PerfSnapshot(
                    fps = session.measuredFps,
                    frameMs = session.frameMs,
                    tickMs = session.tickMs,
                    p50Ms = session.frameP50Ms,
                    p95Ms = session.frameP95Ms,
                    p99Ms = session.frameP99Ms,
                    maxMs = session.frameMaxMs,
                    errors = session.tickFailureCount,
                    rendererInfo = session.rendererInfo(),
                    memory = session.memoryStats(),
                    compatProfile = session.compatProfile(),
                )
                delay(250)
            }
        }
    }

    // 悬浮菜单与运行时日志浮层
    var menuOpen by remember { mutableStateOf(false) }
    var logsVisible by remember { mutableStateOf(false) }
    var logLines by remember { mutableStateOf<List<String>>(emptyList()) }
    // 按键浮层里当前选中的按钮（仅编辑态有意义）。
    var keypadSelectedId by remember { mutableStateOf<String?>(null) }
    // 触控板模式下的虚拟光标与手势状态。
    val touchpadState = remember { TouchpadState() }

    // 引擎菜单侧边栏（§4）。菜单快照由引擎在 tick 上刷新，这里按 2Hz 拉取。
    var engineMenuVisible by remember { mutableStateOf(false) }
    var engineMenuItems by remember { mutableStateOf<List<EngineMenuItem>>(emptyList()) }
    LaunchedEffect(engineMenuVisible) {
        if (!engineMenuVisible) return@LaunchedEffect
        while (true) {
            engineMenuItems = session.windowMenu()
            delay(500)
        }
    }

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
                    setOnTouchListener { view, event ->
                        // 菜单/日志浮层打开、或按键编辑态时吞掉触摸：否则点浮层/拖按钮
                        // 会连带把 POINTER_DOWN 送进游戏。menuOpen/logsVisible/
                        // keypadEditing 是 Compose State，闭包每次读到的都是当时的值。
                        if (!menuOpen && !logsVisible && !keypadEditing && !engineMenuVisible) {
                            if (touchpadMode) {
                                handleTouchpadTouch(
                                    session = session,
                                    event = event,
                                    state = touchpadState,
                                    width = view.width.toFloat(),
                                    height = view.height.toFloat(),
                                    sensitivity = touchpadSensitivity,
                                )
                            } else {
                                handleTouch(session, event)
                            }
                        }
                        true
                    }
                }
            },
        )

        // 自定义按键浮层：贴在引擎 Surface 之上、性能叠加层之下。
        // 非编辑态只有按钮命中区消费事件，其余区域穿透给引擎（见 KeyPadOverlay）。
        KeyPadOverlay(
            profile = keypadConfig,
            editing = keypadEditing,
            selectedId = keypadSelectedId,
            onProfileChange = onKeypadChange,
            onSelect = { keypadSelectedId = it },
            onKeyDown = { vk -> session.sendInput(InputEvent.KEY_DOWN, keyCode = vk) },
            onKeyUp = { vk -> session.sendInput(InputEvent.KEY_UP, keyCode = vk) },
            onExitEdit = { onKeypadEditingChange(false) },
        )

        // 触控板模式的虚拟光标：压在浮层之上（否则会被按键盖住），但不消费触摸。
        if (touchpadMode && !keypadEditing && !menuOpen && !logsVisible) {
            TouchpadCursor(position = touchpadState.cursor)
        }

        // 性能叠加层：左上角 (16,12)，与 AetherKiri 的 _layout_perf_overlay 同位。
        // 不设 clickable，触摸照常穿透给引擎。
        PerformanceOverlay(
            config = overlayConfig,
            snapshot = perf,
            modifier = Modifier
                .align(overlayConfig.corner.toAlignment())
                .padding(start = 16.dp, top = 12.dp, end = 16.dp, bottom = 12.dp),
        )

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
                        keypadEditing = keypadEditing,
                        touchpadMode = touchpadMode,
                        engineMenuButton = engineMenuButton,
                        onToggleEngineMenuButton = {
                            menuOpen = false
                            onEngineMenuButtonChange(!engineMenuButton)
                        },
                        onToggleTouchpad = {
                            menuOpen = false
                            onTouchpadModeChange(!touchpadMode)
                        },
                        onToggleKeypadEdit = {
                            menuOpen = false
                            onKeypadEditingChange(!keypadEditing)
                        },
                        onShowLogs = {
                            menuOpen = false
                            logLines = AppLog.recent()
                            logsVisible = true
                        },
                        onOpenSettings = {
                            menuOpen = false
                            onOpenSettings()
                        },
                        onExit = {
                            menuOpen = false
                            onExit()
                        },
                    )
                    Spacer(Modifier.height(12.dp))
                }
                if (engineMenuButton) {
                    SmallFloatingActionButton(
                        onClick = { engineMenuVisible = true },
                        containerColor = Color(0xCC1F1F1F),
                        contentColor = Color.White,
                    ) {
                        Icon(Icons.Filled.Menu, contentDescription = "引擎菜单")
                    }
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

        // 引擎菜单侧边栏：压在游戏与按键浮层之上（展开时遮罩会吃掉游戏输入）。
        if (engineMenuVisible) {
            EngineMenuSidebar(
                items = engineMenuItems,
                onInvoke = { id -> session.invokeWindowMenu(id) },
                onClose = { engineMenuVisible = false },
            )
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
 * 悬浮菜单面板。三项都用纯文字，不引图标——少一个图标名就对不上依赖版本的风险。
 */
@Composable
private fun GameMenu(
    keypadEditing: Boolean,
    touchpadMode: Boolean,
    engineMenuButton: Boolean,
    onToggleEngineMenuButton: () -> Unit,
    onToggleTouchpad: () -> Unit,
    onToggleKeypadEdit: () -> Unit,
    onShowLogs: () -> Unit,
    onOpenSettings: () -> Unit,
    onExit: () -> Unit,
) {
    Card(colors = CardDefaults.cardColors(containerColor = Color(0xE61F1F1F))) {
        Column {
            MenuEntry(
                label = if (touchpadMode) "触控板模式：开" else "触控板模式：关",
                onClick = onToggleTouchpad,
            )
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(Color(0x33FFFFFF)),
            )
            MenuEntry(
                label = if (engineMenuButton) "隐藏引擎菜单按钮" else "显示引擎菜单按钮",
                onClick = onToggleEngineMenuButton,
            )
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(Color(0x33FFFFFF)),
            )
            MenuEntry(
                label = if (keypadEditing) "完成按键编辑" else "编辑自定义按键",
                onClick = onToggleKeypadEdit,
            )
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(Color(0x33FFFFFF)),
            )
            MenuEntry(label = "显示运行时日志", onClick = onShowLogs)
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(Color(0x33FFFFFF)),
            )
            MenuEntry(label = "设置", onClick = onOpenSettings)
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
