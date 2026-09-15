package org.dpdns.clevebitr.ui

import android.annotation.SuppressLint
import android.os.Build
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.unit.dp
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
 */
@Composable
fun GameScreen(
    session: EngineSession,
    startupState: Int,
    statusText: String,
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
                        handleTouch(session, event)
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
