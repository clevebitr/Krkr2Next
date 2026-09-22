package org.dpdns.clevebitr.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.UUID
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlinx.coroutines.delay
import org.dpdns.clevebitr.core.KeyButton
import org.dpdns.clevebitr.core.KeyPadProfile
import org.dpdns.clevebitr.core.VkCodes

/**
 * 自定义按键浮层。
 *
 * ## 触摸穿透（本组件最容易踩的坑）
 *
 * 浮层铺满整个游戏画面，但**只有按钮自身的命中区消费事件**：容器本身不挂任何
 * `pointerInput`，所以按钮以外区域的触摸照常落到下面的引擎 SurfaceView。这与
 * `PerformanceOverlay` 的做法一致（它也不消费触摸）。
 *
 * 编辑态是例外：那时 [editing] 为真，`GameScreen` 的 SurfaceView 监听会直接吞掉
 * 全部触摸（否则拖按钮会连带把一次 `POINTER_DOWN` 送进游戏），因此编辑态下整个
 * 浮层才算"接管输入"。
 *
 * ## 编辑态：拖拽/缩放为什么用"绝对位置 + 手势起点"
 *
 * `pointerInput(key)` 的 block 只在 key 变化时重建，因此它捕获的 `button` 是**创建时**
 * 的那一份。若拖拽时按 `当前值 + 增量` 累加，block 不重建就永远拿旧值，每帧只会把
 * 按钮挪到"起点 + 最后一帧增量"——表现出来就是拖不动、缩放弹回。所以这里：
 *  - 手势开始时快照起点（经 [rememberUpdatedState] 取最新值）；
 *  - 拖拽中累加**相对手势起点**的总位移，再按绝对目标写回；
 *  - 回调本身也经 [rememberUpdatedState] 取最新，避免捕获过期闭包。
 *
 * ## 按键注入
 *
 * `onKeyDown` / `onKeyUp` 收到的是 **Windows VK 码**（[KeyButton.vk]），由调用方
 * 转成 `engine_input_event_t` 投递。按下/抬起必须成对：长按由本组件按系统 repeat
 * 的心跳补发 down（引擎侧不生成 repeat，见 `EngineLoop::HandleKeyDown`）。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun KeyPadOverlay(
    profile: KeyPadProfile,
    editing: Boolean,
    selectedId: String?,
    onProfileChange: (KeyPadProfile) -> Unit,
    onSelect: (String?) -> Unit,
    onKeyDown: (Int) -> Unit,
    onKeyUp: (Int) -> Unit,
    modifier: Modifier = Modifier,
    /** 编辑态“完成”按钮；null 时不显示（无浮层宿主时）。 */
    onExitEdit: (() -> Unit)? = null,
) {
    if (!editing && !profile.visible) return

    // 属性面板：编辑态下可完整配置选中按钮（键位/文字/图标/颜色/大小/透明度/描边）。
    var propertiesOpen by remember { mutableStateOf(false) }

    // 对齐参考线（归一化坐标）。拖动时由 snapPosition 填入，手势结束清空。
    var vGuides by remember { mutableStateOf<List<Float>>(emptyList()) }
    var hGuides by remember { mutableStateOf<List<Float>>(emptyList()) }

    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        val cw = constraints.maxWidth.toFloat().coerceAtLeast(1f)
        val ch = constraints.maxHeight.toFloat().coerceAtLeast(1f)

        profile.buttons.forEach { button ->
            key(button.id) {
                KeyPadButtonView(
                    button = button,
                    containerWidthPx = cw,
                    containerHeightPx = ch,
                    editing = editing,
                    selected = button.id == selectedId,
                    onMoveTo = { nx, ny ->
                        // 拖动时自动停靠：对齐画面边缘/中线与其他按钮的边/中心，
                        // 命中就把位置改成对齐值并记下参考线（类似 PS 的参考线）。
                        val snapped = snapPosition(
                            x = nx,
                            y = ny,
                            w = button.w,
                            h = button.h,
                            buttons = profile.buttons,
                            selfId = button.id,
                            containerWidthPx = cw,
                            containerHeightPx = ch,
                        )
                        vGuides = snapped.vGuides
                        hGuides = snapped.hGuides
                        onProfileChange(
                            profile.withButton(
                                button.copy(x = snapped.x, y = snapped.y).sanitized(),
                            ),
                        )
                    },
                    onResizeTo = { nw, nh ->
                        onProfileChange(profile.withButton(button.copy(w = nw, h = nh).sanitized()))
                    },
                    onDragEnd = {
                        vGuides = emptyList()
                        hGuides = emptyList()
                    },
                    onSelect = { onSelect(button.id) },
                    onKeyDown = onKeyDown,
                    onKeyUp = onKeyUp,
                )
            }
        }

        if (editing && (vGuides.isNotEmpty() || hGuides.isNotEmpty())) {
            // 参考线画在按钮之上（不然会被按钮盖住），但不消费触摸。
            Canvas(modifier = Modifier.fillMaxSize()) {
                val stroke = 1.dp.toPx()
                vGuides.forEach { gx ->
                    val x = gx * cw
                    drawLine(GuideColor, Offset(x, 0f), Offset(x, ch), strokeWidth = stroke)
                }
                hGuides.forEach { gy ->
                    val y = gy * ch
                    drawLine(GuideColor, Offset(0f, y), Offset(cw, y), strokeWidth = stroke)
                }
            }
        }

        if (editing) {
            KeyPadEditToolbar(
                hasSelection = selectedId != null,
                onAdd = {
                    val added = KeyButton(
                        id = UUID.randomUUID().toString(),
                        vk = VkCodes.RETURN,
                        label = "键",
                        x = 0.42f,
                        y = 0.42f,
                        w = 0.12f,
                        h = 0.14f,
                    )
                    onProfileChange(profile.withButton(added))
                    onSelect(added.id)
                },
                onProperties = if (selectedId != null) {
                    { propertiesOpen = true }
                } else {
                    null
                },
                onDelete = {
                    val id = selectedId ?: return@KeyPadEditToolbar
                    onProfileChange(profile.withoutButton(id))
                    onSelect(null)
                },
                onExitEdit = onExitEdit,
                modifier = Modifier.align(Alignment.TopCenter).padding(top = 12.dp),
            )
        }
    }

    if (propertiesOpen) {
        ModalBottomSheet(onDismissRequest = { propertiesOpen = false }) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(bottom = 32.dp),
            ) {
                Text(
                    text = "按键属性",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(start = 16.dp, bottom = 4.dp),
                )
                // 与设置页共用同一个编辑器：键位/图标/颜色/描边/位置大小三处语义一致。
                KeyPadConfigEditor(
                    profile = profile,
                    onProfileChange = onProfileChange,
                    initialSelectedId = selectedId,
                )
            }
        }
    }
}

/**
 * 编辑态工具条：添加 / 属性 / 删除所选 / 完成。放在画面顶部中间——避开右上角的
 * 性能叠加层与右下角的悬浮菜单。
 */
@Composable
private fun KeyPadEditToolbar(
    hasSelection: Boolean,
    onAdd: () -> Unit,
    onProperties: (() -> Unit)?,
    onDelete: () -> Unit,
    onExitEdit: (() -> Unit)?,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(24.dp),
        color = Color(0xE61F1F1F),
        contentColor = Color.White,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            ToolbarAction(
                label = "添加",
                enabled = true,
                icon = { Icon(Icons.Filled.Add, contentDescription = null) },
                onClick = onAdd,
            )
            ToolbarAction(
                label = "属性",
                enabled = onProperties != null,
                icon = { Icon(Icons.Filled.Tune, contentDescription = null) },
                onClick = onProperties ?: {},
            )
            ToolbarAction(
                label = "删除",
                enabled = hasSelection,
                icon = { Icon(Icons.Filled.Delete, contentDescription = null) },
                onClick = onDelete,
            )
            if (onExitEdit != null) {
                ToolbarAction(
                    label = "完成",
                    enabled = true,
                    icon = { Icon(Icons.Filled.Check, contentDescription = null) },
                    onClick = onExitEdit,
                )
            }
        }
    }
}

@Composable
private fun ToolbarAction(
    label: String,
    enabled: Boolean,
    icon: @Composable () -> Unit,
    onClick: () -> Unit,
) {
    val tint = Color.White.copy(alpha = if (enabled) 1f else 0.4f)
    Row(
        modifier = Modifier
            .padding(horizontal = 6.dp)
            .then(
                if (enabled) {
                    Modifier.pointerInput(label) { detectTapGestures { onClick() } }
                } else {
                    Modifier
                },
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CompositionLocalProvider(LocalContentColor provides tint) {
            icon()
        }
        Text(
            text = label,
            color = tint,
            style = MaterialTheme.typography.labelLarge,
            modifier = Modifier.padding(start = 4.dp),
        )
    }
}

/** 长按开始补发 down 的延迟；与 Android 系统键盘的初始 repeat 延迟量级一致。 */
private const val REPEAT_INITIAL_DELAY_MS = 400L

/** 补发 down 的间隔；~16 次/秒，够游戏把它当成持续按键。 */
private const val REPEAT_INTERVAL_MS = 60L

/** 按钮圆角。 */
private val KeyButtonShape = RoundedCornerShape(10.dp)

/** 对齐参考线的颜色。亮青色：在深浅两种游戏画面上都看得见。 */
private val GuideColor = Color(0xFF00E5FF)

/** 吸附阈值（dp）。8dp 是手指拖拽时既不“拉不动”又能明显停靠的量级。 */
private const val SNAP_DP = 8f

/** 一次拖动的吸附结果：新位置 + 命中的参考线（归一化坐标）。 */
private data class SnapResult(
    val x: Float,
    val y: Float,
    val vGuides: List<Float>,
    val hGuides: List<Float>,
)

/**
 * 自动对齐：把按钮的左/中/右与上/中/下分别吸到最近的候选线上。
 *
 * 候选线 = 画面左/中/右（上/中/下）+ 其它按钮的同名边与中心。阈值按**像素**给，
 * 换算成归一化坐标后再比较，这样不同分辨率下手感一致。
 */
private fun snapPosition(
    x: Float,
    y: Float,
    w: Float,
    h: Float,
    buttons: List<KeyButton>,
    selfId: String,
    containerWidthPx: Float,
    containerHeightPx: Float,
): SnapResult {
    val thresholdX = SNAP_DP / containerWidthPx.coerceAtLeast(1f)
    val thresholdY = SNAP_DP / containerHeightPx.coerceAtLeast(1f)

    val xTargets = mutableListOf(0f, 0.5f, 1f)
    val yTargets = mutableListOf(0f, 0.5f, 1f)
    buttons.forEach { other ->
        if (other.id == selfId) return@forEach
        xTargets += other.x
        xTargets += other.x + other.w / 2f
        xTargets += other.x + other.w
        yTargets += other.y
        yTargets += other.y + other.h / 2f
        yTargets += other.y + other.h
    }

    var bestX = x
    var bestDX = thresholdX
    var guideX: Float? = null
    listOf(x, x + w / 2f, x + w).forEach { cand ->
        xTargets.forEach { target ->
            val d = abs(cand - target)
            if (d <= bestDX) {
                bestDX = d
                bestX = x + (target - cand)
                guideX = target
            }
        }
    }

    var bestY = y
    var bestDY = thresholdY
    var guideY: Float? = null
    listOf(y, y + h / 2f, y + h).forEach { cand ->
        yTargets.forEach { target ->
            val d = abs(cand - target)
            if (d <= bestDY) {
                bestDY = d
                bestY = y + (target - cand)
                guideY = target
            }
        }
    }

    return SnapResult(
        x = bestX,
        y = bestY,
        vGuides = listOfNotNull(guideX),
        hGuides = listOfNotNull(guideY),
    )
}

@Composable
private fun KeyPadButtonView(
    button: KeyButton,
    containerWidthPx: Float,
    containerHeightPx: Float,
    editing: Boolean,
    selected: Boolean,
    onMoveTo: (Float, Float) -> Unit,
    onResizeTo: (Float, Float) -> Unit,
    onDragEnd: () -> Unit,
    onSelect: () -> Unit,
    onKeyDown: (Int) -> Unit,
    onKeyUp: (Int) -> Unit,
) {
    val density = LocalDensity.current
    val widthPx = button.w * containerWidthPx
    val heightPx = button.h * containerHeightPx
    val widthDp = with(density) { widthPx.toDp() }
    val heightDp = with(density) { heightPx.toDp() }

    // 手势 block 不随 x/y/w/h 重建，所以必须用 rememberUpdatedState 读最新值/最新回调。
    val currentButton by rememberUpdatedState(button)
    val moveTo by rememberUpdatedState(onMoveTo)
    val resizeTo by rememberUpdatedState(onResizeTo)
    val select by rememberUpdatedState(onSelect)
    val dragEnd by rememberUpdatedState(onDragEnd)

    // 按下状态驱动"长按补发 down"的循环。编辑态不注入按键。
    var pressed by remember(button.id) { mutableStateOf(false) }
    if (pressed && !editing) {
        LaunchedEffect(button.id, pressed) {
            delay(REPEAT_INITIAL_DELAY_MS)
            while (true) {
                onKeyDown(button.vk)
                delay(REPEAT_INTERVAL_MS)
            }
        }
    }

    val baseColor = Color(button.bgColor)
    val background = baseColor.copy(alpha = baseColor.alpha * button.alpha)

    val appears = Modifier
        .background(background, KeyButtonShape)
        .then(
            if (button.strokeWidthDp > 0f) {
                Modifier.border(button.strokeWidthDp.dp, Color(button.strokeColor), KeyButtonShape)
            } else {
                Modifier
            },
        )
        .then(
            if (editing && selected) {
                Modifier.border(2.dp, MaterialTheme.colorScheme.primary, KeyButtonShape)
            } else {
                Modifier
            },
        )

    val interaction = if (editing) {
        // 选择与拖拽放在**同一个**手势处理器里：`detectTapGestures` 会在 down 上
        // `consume()`，与同一节点的 `detectDragGestures` 冲突（拖拽会被取消）。
        Modifier.pointerInput(button.id, "edit-gesture") {
            val slop = viewConfiguration.touchSlop
            // 右下角缩放把手占的边长；落在这里的按下交给把手处理，本节点不拖。
            val handlePx = 22.dp.toPx()
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                select()
                val fromHandle = down.position.x >= size.width - handlePx &&
                    down.position.y >= size.height - handlePx
                if (!fromHandle) {
                    val startX = currentButton.x
                    val startY = currentButton.y
                    var accX = 0f
                    var accY = 0f
                    var dragging = false
                    while (true) {
                        val event = awaitPointerEvent()
                        val change = event.changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        accX += change.positionChange().x
                        accY += change.positionChange().y
                        if (!dragging && accX * accX + accY * accY >= slop * slop) {
                            dragging = true
                        }
                        if (dragging) {
                            moveTo(
                                startX + accX / containerWidthPx,
                                startY + accY / containerHeightPx,
                            )
                        }
                        change.consume()
                    }
                    dragEnd()
                }
            }
        }
    } else {
        Modifier.pointerInput(button.id, "press") {
            detectTapGestures(
                onPress = {
                    pressed = true
                    onKeyDown(button.vk)
                    try {
                        tryAwaitRelease()
                    } finally {
                        pressed = false
                        onKeyUp(button.vk)
                    }
                },
            )
        }
    }

    Box(
        modifier = Modifier
            .offset {
                IntOffset(
                    (button.x * containerWidthPx).roundToInt(),
                    (button.y * containerHeightPx).roundToInt(),
                )
            }
            .size(widthDp, heightDp)
            .then(appears)
            .then(interaction),
        contentAlignment = Alignment.Center,
    ) {
        val icon = KeyPadIcons.resolve(button.iconKey)
        val tint = Color(button.textColor).copy(alpha = button.alpha)
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            if (icon != null) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = tint,
                    modifier = Modifier.size(
                        with(density) {
                            (heightPx * 0.45f).coerceAtMost(widthPx * 0.8f).toDp()
                        },
                    ),
                )
            }
            if (button.label.isNotBlank()) {
                Text(
                    text = button.label,
                    color = tint,
                    fontSize = button.textSizeSp.sp,
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.padding(horizontal = 2.dp),
                )
            }
        }

        if (editing && selected) {
            // 右下角缩放把手。它比按钮后声明，因此叠在上层、优先拿到事件。
            Box(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .size(22.dp)
                    .background(
                        MaterialTheme.colorScheme.primary,
                        RoundedCornerShape(topStart = 8.dp),
                    )
                    .pointerInput(button.id, "resize") {
                        var startW = 0f
                        var startH = 0f
                        var accX = 0f
                        var accY = 0f
                        detectDragGestures(
                            onDragStart = {
                                startW = currentButton.w
                                startH = currentButton.h
                                accX = 0f
                                accY = 0f
                            },
                            onDrag = { change, dragAmount ->
                                change.consume()
                                accX += dragAmount.x
                                accY += dragAmount.y
                                resizeTo(
                                    startW + accX / containerWidthPx,
                                    startH + accY / containerHeightPx,
                                )
                            },
                        )
                    },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "⤡",
                    color = MaterialTheme.colorScheme.onPrimary,
                    fontSize = 14.sp,
                )
            }
        }
    }
}
