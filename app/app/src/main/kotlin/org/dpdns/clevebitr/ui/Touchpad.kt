package org.dpdns.clevebitr.ui

import android.view.MotionEvent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import kotlin.math.roundToInt
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.InputEvent

/**
 * 光标触控板模式。
 *
 * 触屏直接把手指标成绝对坐标时，**期望鼠标的游戏**（PC 移植、需要悬停高亮 / 右键菜单 /
 * 精确点选）会很难操作：手指按下的位置就是指针位置，没有"先移动再点"的余地。
 * 触控板模式把手指变成触控板——**相对移动**驱动一个虚拟光标，轻点才是点击。
 *
 * 手势约定（与常见触控板一致）：
 *  - 单指拖动：光标按 `位移 × 灵敏度` 相对移动；
 *  - 单指轻点（几乎没移动）：在光标处左键单击；
 *  - 双指轻点：在光标处右键单击；
 *  - 双指上下拖：滚轮。
 *
 * 光标位置用**视图坐标（物理像素）**，与 `handleTouch` 的绝对模式同口径；引擎侧
 * `DrawDevice::TransformToPrimaryLayerManager` 负责换算到 layer 坐标（AGENTS §11）。
 */
class TouchpadState {
    /** 虚拟光标（视图坐标）。Compose 状态，浮层据此重绘。 */
    var cursor by mutableStateOf(Offset.Zero)
        private set

    /** 是否已经向引擎报过光标位置（首帧要补一次 POINTER_MOVE，否则光标停在 0,0）。 */
    var reported by mutableStateOf(false)
        private set

    private var lastX = 0f
    private var lastY = 0f
    private var downX = 0f
    private var downY = 0f
    private var moved = false
    private var twoFinger = false
    private var lastAvgY = 0f
    private var scrollAccum = 0f

    /** 视图尺寸变化（旋转/分屏）时把光标夹回可见范围。 */
    fun clampTo(width: Float, height: Float) {
        cursor = Offset(
            cursor.x.coerceIn(0f, width.coerceAtLeast(1f)),
            cursor.y.coerceIn(0f, height.coerceAtLeast(1f)),
        )
    }

    private fun ensureInitialized(width: Float, height: Float) {
        if (!reported && width > 0f && height > 0f) {
            cursor = Offset(width / 2f, height / 2f)
        }
    }

    fun onDown(session: EngineSession, event: MotionEvent, width: Float, height: Float) {
        ensureInitialized(width, height)
        lastX = event.x
        lastY = event.y
        downX = event.x
        downY = event.y
        moved = false
        twoFinger = false
        // 首次交互先把光标位置告诉引擎：很多游戏在第一次指针事件之前读到的
        // cursorX/cursorY 是 0,0。
        if (!reported) {
            reported = true
            sendMove(session)
        }
    }

    fun onPointerDown(event: MotionEvent) {
        if (event.pointerCount >= 2) {
            twoFinger = true
            lastAvgY = averageY(event)
            scrollAccum = 0f
        }
    }

    fun onMove(
        session: EngineSession,
        event: MotionEvent,
        width: Float,
        height: Float,
        sensitivity: Float,
    ) {
        if (event.pointerCount >= 2) {
            val avgY = averageY(event)
            val dy = avgY - lastAvgY
            lastAvgY = avgY
            // 触控板式滚动：手指上滑 = 内容下移 = 滚轮向下（正 delta）。
            scrollAccum += dy
            val step = 1f
            while (abs(scrollAccum) >= step) {
                val dir = if (scrollAccum > 0) 1f else -1f
                scrollAccum -= dir * step
                session.sendInput(
                    InputEvent.POINTER_SCROLL,
                    x = cursor.x.toDouble(),
                    y = cursor.y.toDouble(),
                    deltaY = (dir * SCROLL_PER_STEP).toDouble(),
                    pointerId = 0,
                )
            }
            moved = true
            return
        }

        val dx = (event.x - lastX) * sensitivity
        val dy = (event.y - lastY) * sensitivity
        lastX = event.x
        lastY = event.y
        if (dx == 0f && dy == 0f) return

        cursor = Offset(
            (cursor.x + dx).coerceIn(0f, width.coerceAtLeast(1f)),
            (cursor.y + dy).coerceIn(0f, height.coerceAtLeast(1f)),
        )
        if (abs(cursor.x - downX) > TAP_SLOP_PX || abs(cursor.y - downY) > TAP_SLOP_PX) {
            moved = true
        }
        sendMove(session)
    }

    fun onPointerUp(event: MotionEvent) {
        // 从双指回到单指：重置滚动基准，避免用旧的平均 Y 算出一个巨大 delta。
        if (event.pointerCount <= 1) {
            twoFinger = false
            scrollAccum = 0f
        }
    }

    fun onUp(session: EngineSession) {
        when {
            twoFinger -> click(session, button = 1) // 双指轻点 = 右键
            !moved -> click(session, button = 0) // 单指轻点 = 左键
        }
        moved = false
        twoFinger = false
        scrollAccum = 0f
    }

    private fun sendMove(session: EngineSession) {
        session.sendInput(
            InputEvent.POINTER_MOVE,
            x = cursor.x.toDouble(),
            y = cursor.y.toDouble(),
            pointerId = 0,
        )
    }

    private fun click(session: EngineSession, button: Int) {
        // 按下/抬起必须成对，且都落在光标处：引擎按"最后位置"决定点到了哪。
        session.sendInput(
            InputEvent.POINTER_DOWN,
            x = cursor.x.toDouble(),
            y = cursor.y.toDouble(),
            pointerId = 0,
            button = button,
        )
        session.sendInput(
            InputEvent.POINTER_UP,
            x = cursor.x.toDouble(),
            y = cursor.y.toDouble(),
            pointerId = 0,
            button = button,
        )
    }

    private fun averageY(event: MotionEvent): Float {
        var sum = 0f
        for (i in 0 until event.pointerCount) sum += event.getY(i)
        return sum / event.pointerCount
    }

    companion object {
        /** 判定"轻点"的位移阈值（视图像素）。 */
        private const val TAP_SLOP_PX = 24f

        /** 每个滚动步进的滚轮量。 */
        private const val SCROLL_PER_STEP = 1.0f
    }
}

/**
 * 触控板触摸分发。返回 true 表示已消费（`GameScreen` 的 SurfaceView 监听恒返回 true）。
 */
@Suppress("ClickableViewAccessibility")
fun handleTouchpadTouch(
    session: EngineSession,
    event: MotionEvent,
    state: TouchpadState,
    width: Float,
    height: Float,
    sensitivity: Float,
) {
    when (event.actionMasked) {
        MotionEvent.ACTION_DOWN -> state.onDown(session, event, width, height)
        MotionEvent.ACTION_POINTER_DOWN -> state.onPointerDown(event)
        MotionEvent.ACTION_MOVE -> state.onMove(session, event, width, height, sensitivity)
        MotionEvent.ACTION_POINTER_UP -> state.onPointerUp(event)
        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> state.onUp(session)
    }
}

/**
 * 虚拟光标。压在引擎 Surface 之上，不消费触摸（`GameScreen` 已在 SurfaceView 监听里
 * 处理输入）。画成"圆环 + 十字"：游戏画面明暗不定，纯圆点容易看不见。
 */
@Composable
fun TouchpadCursor(position: Offset, modifier: Modifier = Modifier) {
    val size = 26.dp
    Box(
        modifier = modifier
            .offset {
                IntOffset(
                    (position.x - size.toPx() / 2f).roundToInt(),
                    (position.y - size.toPx() / 2f).roundToInt(),
                )
            }
            .size(size)
            .background(Color(0x40000000), CircleShape)
            .border(2.dp, Color(0xFFFFFFFF), CircleShape)
            .border(1.dp, Color(0xCC000000), CircleShape),
    ) {
        Box(
            modifier = Modifier
                .align(androidx.compose.ui.Alignment.Center)
                .size(4.dp)
                .background(Color(0xFFFFFFFF), CircleShape),
        )
    }
}

/** 供日志使用：一次触控板手势的汇总（高频 move 不逐条记）。 */
internal fun touchpadLog(state: TouchpadState): String =
    "touchpad: cursor=(${state.cursor.x.toInt()},${state.cursor.y.toInt()})"

private const val TAG = "KrKr2Next/Game"

/** 首次切到触控板模式时记一条，便于日志里确认模式生效。 */
internal fun logTouchpadMode(enabled: Boolean) {
    AppLog.i(TAG, "touchpad mode = $enabled")
}
