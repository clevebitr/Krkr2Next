package org.tvp.kirikiri2

import org.dpdns.clevebitr.core.MessageBoxHost

/**
 * 引擎消息框回调的落点类（**不是 Activity**）。
 *
 * `cpp/core/environ/android/AndroidUtils.cpp` 用 JNI 静态方法名调用
 * `org/tvp/kirikiri2/KR2Activity.ShowMessageBox` / `ShowInputBox`（原 krkr2 Android
 * 壳的类名），并阻塞等待 `nativeOnMessageBoxResult` / `nativeOnInputBoxResult`
 * 回传按钮下标。**包名与类名是引擎侧硬编码的，不能改**；改这里会只在运行时以
 * `JNI: class not found` + 消息框静默失败暴露（`scripts/check_jni_symbols.py`
 * 会拦 Kotlin external ↔ C++ 符号的不一致）。
 *
 * 调用线程是引擎线程（此刻引擎正阻塞在消息框等待里），这里只做入队，
 * 由 [MessageBoxHost] + Compose 层负责显示与回复。
 */
object KR2Activity {

    /** 引擎侧签名：`(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V` */
    @JvmStatic
    fun ShowMessageBox(title: String, text: String, buttons: Array<String>) {
        MessageBoxHost.enqueueMessageBox(title, text, buttons.toList()) { index, _ ->
            nativeOnMessageBoxResult(index)
        }
    }

    /**
     * 引擎侧签名：`(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V`，
     * 实参顺序是 `(caption, prompt, text, buttons)`（见 AndroidUtils.cpp 的调用点）。
     * 取消时回原文：C++ 侧 `text = MessageBoxRetText` 是无条件覆写。
     */
    @JvmStatic
    fun ShowInputBox(title: String, prompt: String, text: String, buttons: Array<String>) {
        MessageBoxHost.enqueueInputBox(title, prompt, text, buttons.toList()) { index, input ->
            nativeOnInputBoxResult(index, input ?: text)
        }
    }

    /** 实现见 `AndroidUtils.cpp`：置结果并唤醒阻塞中的引擎线程。 */
    @JvmStatic
    external fun nativeOnMessageBoxResult(result: Int)

    @JvmStatic
    external fun nativeOnInputBoxResult(result: Int, text: String)
}
