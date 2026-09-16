package org.dpdns.clevebitr.core

/**
 * 宿主机消息框队列。
 *
 * 引擎弹出消息框（`System.inform` / 致命错误框 / 修补包提示）时会**阻塞引擎线程**
 * 等回复（见 `cpp/core/environ/android/AndroidUtils.cpp`），所以入队方只登记请求、
 * 不碰 UI；Compose 层（`MainActivity`）按固定节拍轮询 [peek] 渲染对话框，用户点按后
 * 调 [reply] 把下标回传引擎。
 *
 * 线程约定：入队在**引擎线程**，轮询与回复在 **UI 线程**，状态用锁保护。
 * 本对象刻意不依赖 Compose / 协程——core 包要能被 kotlinc 单独编译。
 *
 * 没有 UI 接管时请求立即按 -1（取消）回掉：否则引擎会永久卡在等待里。
 */
object MessageBoxHost {
    private const val TAG = "KrKr2Next/MessageBox"

    /** 引擎一次只会有一个阻塞中的请求；多出来的是异常情形，超过上限即丢弃。 */
    private const val MAX_PENDING = 4

    /**
     * 一个待显示的消息框请求。
     * `inputPrompt != null` 表示输入框（`TVPShowSimpleInputBox`），此时 [reply] 的
     * 第二个参数是用户输入文本。
     */
    class Request(
        val title: String,
        val text: String,
        val buttons: List<String>,
        val inputPrompt: String? = null,
        val initialInput: String = "",
        internal val replyTo: (index: Int, text: String?) -> Unit,
    )

    private val lock = Any()
    private val pending = ArrayDeque<Request>()
    private var uiAttached = false

    /** UI 层进入（Activity onCreate）时调用。 */
    fun attachUi() {
        synchronized(lock) { uiAttached = true }
    }

    /** UI 层退场（Activity onDestroy）时调用；未回复的请求按取消回掉，不留挂账。 */
    fun detachUi() {
        val dropped = synchronized(lock) {
            uiAttached = false
            val list = pending.toList()
            pending.clear()
            list
        }
        for (request in dropped) request.replyTo(-1, null)
    }

    /** 引擎侧：入队一个消息框请求（由 `org.tvp.kirikiri2.KR2Activity` 调用）。 */
    fun enqueueMessageBox(
        title: String,
        text: String,
        buttons: List<String>,
        replyTo: (Int, String?) -> Unit,
    ) = enqueue(Request(title, text, buttons, replyTo = replyTo))

    /**
     * 引擎侧：入队一个输入框请求。
     * `prompt` 是提示语（作为输入框标签），`initialInput` 是预填文本。
     */
    fun enqueueInputBox(
        title: String,
        prompt: String,
        initialInput: String,
        buttons: List<String>,
        replyTo: (Int, String?) -> Unit,
    ) = enqueue(Request(title, "", buttons, prompt, initialInput, replyTo))

    private fun enqueue(request: Request) {
        val queued = synchronized(lock) {
            if (!uiAttached || pending.size >= MAX_PENDING) {
                false
            } else {
                pending.addLast(request)
                true
            }
        }
        if (queued) return

        AppLog.w(TAG, "消息框未入队（uiAttached=$uiAttached），按取消回掉：${request.text.take(120)}")
        request.replyTo(-1, null)
    }

    /**
     * UI 侧：取队首请求；没有则返回 null。
     * 只读取不移除——回复必须走 [reply]，否则引擎侧等不到结果。
     */
    fun peek(): Request? = synchronized(lock) { pending.firstOrNull() }

    /** UI 侧：回复队首请求。[text] 仅输入框使用。 */
    fun reply(request: Request, index: Int, text: String? = null) {
        val removed = synchronized(lock) { pending.remove(request) }
        if (removed) {
            request.replyTo(index, text)
        } else {
            AppLog.w(TAG, "回复了不在队列里的消息框（已丢弃/已超时），忽略")
        }
    }
}
