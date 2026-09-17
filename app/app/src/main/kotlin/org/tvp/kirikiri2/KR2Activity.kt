package org.tvp.kirikiri2

import org.dpdns.clevebitr.core.AppLog
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

    // ───────────────────────────────────────────────────────────────────────
    // 文件操作
    //
    // `AndroidUtils.cpp` 会用 JNI **静态**方法名找下面这几个；找不到时它走 POSIX
    // 回退，所以功能上一直是好的 —— 但每次查找失败都会由 JniHelper 打一条
    // `JniHelper: static method '...KR2Activity.DeleteFile(...)' not found` 到 logcat，
    // 而引擎删临时文件/存盘很频繁，于是 logcat 里错误日志一直涨（真机实测）。
    // 这里补齐实现，让查找成功；语义与 C++ 侧的 POSIX 回退保持一致。
    // ───────────────────────────────────────────────────────────────────────

    /** 签名 `(Ljava/lang/String;)Z`。 */
    @JvmStatic
    fun DeleteFile(path: String): Boolean = runCatching {
        val f = java.io.File(path)
        !f.exists() || f.delete()
    }.getOrDefault(false)

    /** 签名 `(Ljava/lang/String;Ljava/lang/String;)Z`。 */
    @JvmStatic
    fun RenameFile(from: String, to: String): Boolean = runCatching {
        java.io.File(from).renameTo(java.io.File(to))
    }.getOrDefault(false)

    /** 签名 `(Ljava/lang/String;)Z`。 */
    @JvmStatic
    fun CreateFolders(path: String): Boolean = runCatching {
        val f = java.io.File(path)
        f.isDirectory || f.mkdirs() || f.isDirectory
    }.getOrDefault(false)

    /** 签名 `(Ljava/lang/String;[B)Z`。 */
    @JvmStatic
    fun WriteFile(path: String, bytes: ByteArray): Boolean = runCatching {
        val f = java.io.File(path)
        f.parentFile?.mkdirs()
        f.writeBytes(bytes)
        true
    }.getOrDefault(false)

    /** 签名 `(Ljava/lang/String;)Z`。 */
    @JvmStatic
    fun isWritableNormalOrSaf(path: String): Boolean = runCatching {
        val f = java.io.File(path)
        if (f.isDirectory) f.canWrite() else f.parentFile?.let { it.isDirectory && it.canWrite() } == true
    }.getOrDefault(false)

    /**
     * `getStoragePath()` 在 C++ 侧是按**实例方法**找的（`getMethodInfo` +
     * `CallObjectMethod(INSTANCE, ...)`），所以这里不能加 `@JvmStatic`。
     * 返回空数组是安全的：C++ 侧拿不到就走 `TVPGetAppStoragePath()` 回退。
     */
    fun getStoragePath(): Array<String> = emptyArray()

    // ───────────────────────────────────────────────────────────────────────
    // 其余被引擎查找的静态方法
    //
    // 这些宿主侧没有对应实现，但**必须存在**：JniHelper 找不到就每个调用点打一条
    // error 到 logcat，而它们都在游戏流程里（文本输入、设备 id、广告位控制），
    // 于是错误日志会持续增长。这里给出"明确的降级实现"，让查找成功、行为可预期。
    // 注意：`exit()` 与 `GetVersion()` **故意不实现** —— 前者在宿主模式强制退出会
    // 与 worker 线程抢锁（见 AndroidUtils.cpp 的说明），后者只影响错误框标题里的
    // 版本串。
    // ───────────────────────────────────────────────────────────────────────

    /** 签名 `(III)V`。宿主没有广告位/对话框控制器，明确忽略。 */
    @JvmStatic
    fun MessageController(adType: Int, arg1: Int, arg2: Int) {
        AppLog.d(TAG, "MessageController($adType,$arg1,$arg2) 宿主未实现，忽略")
    }

    /**
     * 签名 `(IIII)V` / `()V`。软键盘目前没有接入点（需要 Activity 引用），
     * 这里明确降级为"不弹键盘"，而不是让查找失败刷 error。
     */
    @JvmStatic
    fun showTextInput(x: Int, y: Int, w: Int, h: Int) {
        AppLog.d(TAG, "showTextInput($x,$y,$w,$h) 宿主暂无 IME 接入点")
    }

    @JvmStatic
    fun hideTextInput() {
        AppLog.d(TAG, "hideTextInput 宿主暂无 IME 接入点")
    }

    /** 签名 `()Ljava/lang/String;`。由 Build 字段派生，稳定且不依赖 Context。 */
    @JvmStatic
    fun getDeviceId(): String = DEVICE_ID

    /** 签名 `()Ljava/lang/String;`。 */
    @JvmStatic
    fun getLocaleName(): String = java.util.Locale.getDefault().toString()

    private const val TAG = "KrKr2Next/KR2Activity"

    /** 只算一次：同一台设备上必须稳定（引擎会把它当保存数据的一部分）。 */
    private val DEVICE_ID: String by lazy {
        val fingerprint = listOf(
            android.os.Build.MANUFACTURER, android.os.Build.MODEL,
            android.os.Build.DEVICE, android.os.Build.FINGERPRINT,
        ).joinToString("|")
        "krkr2next-" + Integer.toHexString(fingerprint.hashCode())
    }
}
