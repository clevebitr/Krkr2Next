package org.dpdns.clevebitr.core

import android.os.Handler
import android.os.Looper

/**
 * 引擎侧"游戏请求退出"的落点。
 *
 * 链路：游戏内菜单的"退出游戏" → TJS `System.exit()` → `TVPTerminateSync`。
 * 宿主模式下 `TVPHostSuppressProcessExit=true`，所以它**不结束进程**，而是
 * `TVPTerminateAsync` 标记 `TVPTerminated` 后抛 `EAbort` 退栈回
 * `Application::Run()`；紧接着引擎的 `TVPExitApplication` 经 JNI 调
 * `KR2Activity.exit()`（见 `cpp/core/environ/android/AndroidUtils.cpp`）。
 *
 * 宿主不接手时的真机表现：点了"退出游戏"像卡住，叠加层错误数
 * （`EngineSession` 的 `tickFailures`）每帧 +1 —— 因为此后每个 `engine_tick`
 * 都返回 `INVALID_STATE`（`runtime requested termination`），而引擎每帧都在
 * 重复走 `TVPSystemUninit` / `TVPExitApplication`。这里把它转成宿主自己的
 * 退出流程（与返回键连按两次等价），引擎随即被正常销毁。
 *
 * 线程约定：请求来自**引擎线程**（此刻正在 `engine_tick` 内，且持有引擎的
 * registry 锁），所以只登记 + post，绝不回调引擎；消费在 UI 线程。
 */
object EngineExitHost {

    private const val TAG = "KrKr2Next/EngineExit"

    private val lock = Any()

    /** UI 线程的消费者；Activity 存活期间才有值。 */
    private var consumer: (() -> Unit)? = null

    /**
     * 引擎可能**每帧**都请求一次（直到宿主真的退出），用它收敛成一次。
     * 消费时清空，所以换游戏后的下一次退出请求仍会生效。
     */
    private var pending = false

    private val mainHandler by lazy { Handler(Looper.getMainLooper()) }

    /** UI 层进入（Activity onCreate）时调用；重复调用只替换消费者。 */
    fun attachUi(onExit: () -> Unit) {
        val deliver = synchronized(lock) {
            consumer = onExit
            if (pending) {
                pending = false
                true
            } else {
                false
            }
        }
        if (deliver) {
            AppLog.i(TAG, "attachUi：补投递之前收到的退出请求")
            onExit()
        }
    }

    /**
     * UI 层退场（Activity onDestroy，且正在结束）时调用。只是解除消费者，
     * 不清 [pending]：万一请求正好落在退场窗口里，下一个实例 attach 时补投。
     */
    fun detachUi() {
        synchronized(lock) { consumer = null }
    }

    /**
     * 引擎线程调用：登记一次"游戏要求退出"。
     *
     * 不阻塞：`TVPExitApplication` 是在 `engine_tick` 内被调用的，此时引擎的
     * registry 锁与句柄锁都还持有，任何回流进引擎的调用（包括同步等 UI）都会
     * 造成锁序交叉。所以非 UI 线程一律 post 到主线程再消费。
     */
    fun request(source: String) {
        val onExit = synchronized(lock) {
            if (pending) {
                return
            }
            pending = true
            consumer
        }
        AppLog.i(TAG, "游戏请求退出（$source）：交给宿主退出流程")
        if (onExit == null) {
            // UI 未就绪（Activity 重建间隙/已退场）：留在 pending，attachUi 补投。
            AppLog.w(TAG, "退出请求已登记，但当前没有 UI 消费者，等 attachUi 补投")
            return
        }

        val deliver = {
            val run = synchronized(lock) {
                if (pending) {
                    pending = false
                    true
                } else {
                    false
                }
            }
            if (run) onExit()
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            deliver()
        } else {
            mainHandler.post(deliver)
        }
    }
}
