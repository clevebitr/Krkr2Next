package dev.kirinext.ui

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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import dev.kirinext.core.EngineSession
import dev.kirinext.core.InputEvent
import dev.kirinext.core.NativeEngine

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
 * 详见 `docs/dev/input-contract.md`。
 */
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
        MotionEvent.ACTION_DOWN ->
            session.sendInput(
                InputEvent.POINTER_DOWN,
                x = event.x.toDouble(), y = event.y.toDouble(),
                pointerId = pointerId, button = button,
            )

        MotionEvent.ACTION_MOVE -> {
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

        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL ->
            session.sendInput(
                InputEvent.POINTER_UP,
                x = event.x.toDouble(), y = event.y.toDouble(),
                pointerId = pointerId, button = button,
            )

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
