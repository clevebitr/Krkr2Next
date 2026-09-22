#ifndef KRKR2_ENGINE_API_H_
#define KRKR2_ENGINE_API_H_

#include <stddef.h>
#include <stdint.h>

/* Export macro for shared-library builds. */
#if defined(_WIN32)
#if defined(ENGINE_API_BUILD_SHARED)
#define ENGINE_API_EXPORT __declspec(dllexport)
#elif defined(ENGINE_API_USE_SHARED)
#define ENGINE_API_EXPORT __declspec(dllimport)
#else
#define ENGINE_API_EXPORT
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define ENGINE_API_EXPORT __attribute__((visibility("default")))
#else
#define ENGINE_API_EXPORT
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* ABI version: major(8bit), minor(8bit), patch(16bit). */
#define ENGINE_API_VERSION 0x01000000u
#define ENGINE_API_MAKE_VERSION(major, minor, patch)                           \
    ((((uint32_t)(major) & 0xFFu) << 24u) |                                    \
     (((uint32_t)(minor) & 0xFFu) << 16u) | ((uint32_t)(patch) & 0xFFFFu))

typedef struct engine_handle_s *engine_handle_t;

typedef enum engine_result_t {
    ENGINE_RESULT_OK = 0,
    ENGINE_RESULT_INVALID_ARGUMENT = -1,
    ENGINE_RESULT_INVALID_STATE = -2,
    ENGINE_RESULT_NOT_SUPPORTED = -3,
    ENGINE_RESULT_IO_ERROR = -4,
    ENGINE_RESULT_INTERNAL_ERROR = -5,
    /*
     * The game itself asked to quit (TJS `System.exit()` / `Application.terminate`).
     * engine_tick returns this instead of ENGINE_RESULT_INVALID_STATE so the host can
     * tell "the game ended" apart from a real error, and leave the game screen
     * instead of counting a failed tick every frame. Host mode never terminates the
     * process for this (see TVPHostSuppressProcessExit), so without host action the
     * engine only reports this code forever and the picture freezes.
     *
     * 游戏自己要求退出（TJS `System.exit()` / `Application.terminate`）。engine_tick
     * 用它把"游戏结束"与真正的错误区分开：宿主应当离开游戏界面，而不是把它记成一次
     * 失败的 tick。宿主模式下不会因此结束进程，所以宿主不接手时只会每帧拿到这个码、
     * 画面冻结。
     *
     * Appended last so existing values stay stable for already-built hosts.
     * 追加在最后，保证已编译宿主看到的既有取值不变。
     */
    ENGINE_RESULT_GAME_TERMINATED = -6,
    /*
     * The game is still starting up (StartApplication is running on the worker
     * thread). engine_tick returns this instead of ENGINE_RESULT_INVALID_STATE so
     * the host does not record every frame of a normal startup as a failed tick —
     * on device that added 300+ bogus errors per launch and made the debug
     * overlay's error counter meaningless.
     *
     * 游戏仍在启动（StartApplication 还在 worker 线程里跑）。engine_tick 用它代替
     * INVALID_STATE，宿主就不会把正常启动的每一帧都记成失败 —— 真机上每次开游戏
     * 都会因此多出 300+ 个错误，叠加层的错误计数直接失去意义。
     */
    ENGINE_RESULT_STARTUP_PENDING = -7,
    /*
     * The game closed its own window, which terminates the runtime
     * (`TVPTerminateOnWindowClose`). Distinct from ENGINE_RESULT_GAME_TERMINATED
     * because the window is already gone: the host must NOT offer "keep playing"
     * (cancelling would only resume ticking an empty scene — measured on device as
     * a frozen game), it should just leave the game screen.
     *
     * 游戏关掉了自己的窗口（TVPTerminateOnWindowClose）。与 GAME_TERMINATED 分开是
     * 因为窗口已经没了：宿主**不能**提供"继续游戏"（撤销后只是在空场景上继续跑，
     * 真机实测就是卡死），应当直接离开游戏界面。
     */
    ENGINE_RESULT_WINDOW_CLOSED = -8,
    /*
     * The game asked to close its window (KAG's quit menu: `kag.close()` →
     * `Window.close()`), but the host deferred the close so it can ask the player
     * first. Nothing has been torn down: the window still paints, the script keeps
     * its state, and the tick is simply paused until the host answers with
     * engine_resolve_window_close().
     *
     * Not an error. The host MUST answer: allow=1 quits (the next tick returns
     * ENGINE_RESULT_WINDOW_CLOSED), allow=0 cancels and the game keeps running.
     * The engine force-closes by itself if nobody answers within ~20s.
     *
     * 游戏请求关闭窗口（KAG 退出菜单），但宿主把关闭挂起以便先问用户。什么都没拆：
     * 窗口照常绘制、脚本状态完好，只是 tick 停在挂起状态等宿主用
     * engine_resolve_window_close() 给答复。**不是错误**；宿主必须答复：
     * allow=1 退出（下一帧报 WINDOW_CLOSED），allow=0 取消、游戏继续跑。
     * 无人答复时引擎约 20s 后自行关窗兜底。
     */
    ENGINE_RESULT_WINDOW_CLOSE_REQUESTED = -9
} engine_result_t;

typedef struct engine_create_desc_t {
    /* struct_size lets newer runtimes accept older callers safely. */
    /* struct_size 允许新运行时安全接收旧调用方的结构体。 */
    uint32_t struct_size;
    uint32_t api_version;
    const char *writable_path_utf8;
    const char *cache_path_utf8;
    void *user_data;
    uint64_t reserved_u64[4];
    void *reserved_ptr[4];
} engine_create_desc_t;

typedef struct engine_option_t {
    const char *key_utf8;
    const char *value_utf8;
    uint64_t reserved_u64[2];
    void *reserved_ptr[2];
} engine_option_t;

typedef enum engine_pixel_format_t {
    ENGINE_PIXEL_FORMAT_UNKNOWN = 0,
    ENGINE_PIXEL_FORMAT_RGBA8888 = 1
} engine_pixel_format_t;

typedef struct engine_frame_desc_t {
    /* Callers initialize struct_size so newer runtimes can extend this ABI. */
    /* 调用方先初始化 struct_size，运行时才能向后兼容地扩展此 ABI。 */
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    uint64_t frame_serial;
    uint64_t reserved_u64[4];
    void *reserved_ptr[4];
} engine_frame_desc_t;

typedef struct engine_memory_stats_t {
    uint32_t struct_size;
    uint32_t self_used_mb;
    uint32_t system_free_mb;
    uint32_t system_total_mb;

    uint64_t graphic_cache_bytes;
    uint64_t graphic_cache_limit_bytes;
    uint64_t xp3_segment_cache_bytes;

    uint64_t psb_cache_bytes;
    uint32_t psb_cache_entries;
    uint32_t psb_cache_entry_limit;
    uint64_t psb_cache_hits;
    uint64_t psb_cache_misses;

    uint32_t archive_cache_entries;
    uint32_t archive_cache_limit;
    uint32_t autopath_cache_entries;
    uint32_t autopath_cache_limit;
    uint32_t autopath_table_entries;
    uint32_t reserved_u32;

    uint64_t reserved_u64[4];
    void *reserved_ptr[4];
} engine_memory_stats_t;

typedef enum engine_input_event_type_t {
    ENGINE_INPUT_EVENT_POINTER_DOWN = 1,
    ENGINE_INPUT_EVENT_POINTER_MOVE = 2,
    ENGINE_INPUT_EVENT_POINTER_UP = 3,
    ENGINE_INPUT_EVENT_POINTER_SCROLL = 4,
    ENGINE_INPUT_EVENT_KEY_DOWN = 5,
    ENGINE_INPUT_EVENT_KEY_UP = 6,
    ENGINE_INPUT_EVENT_TEXT_INPUT = 7,
    ENGINE_INPUT_EVENT_BACK = 8
} engine_input_event_type_t;

typedef enum engine_startup_state_t {
    ENGINE_STARTUP_STATE_IDLE = 0,
    ENGINE_STARTUP_STATE_RUNNING = 1,
    ENGINE_STARTUP_STATE_SUCCEEDED = 2,
    ENGINE_STARTUP_STATE_FAILED = 3
} engine_startup_state_t;

typedef struct engine_input_event_t {
    uint32_t struct_size;
    uint32_t type;
    uint64_t timestamp_micros;
    double x;
    double y;
    double delta_x;
    double delta_y;
    int32_t pointer_id;
    int32_t button;
    int32_t key_code;
    int32_t modifiers;
    uint32_t unicode_codepoint;
    uint32_t reserved_u32;
    uint64_t reserved_u64[2];
    void *reserved_ptr[2];
} engine_input_event_t;

/*
 * Returns runtime API version in out_api_version.
 * out_api_version must be non-null.
 */
ENGINE_API_EXPORT engine_result_t
engine_get_runtime_api_version(uint32_t *out_api_version);

/*
 * Creates an engine handle.
 * desc and out_handle must be non-null.
 * out_handle is set only when ENGINE_RESULT_OK is returned.
 */
ENGINE_API_EXPORT engine_result_t
engine_create(const engine_create_desc_t *desc, engine_handle_t *out_handle);

/*
 * Destroys engine handle and releases all resources.
 * Idempotent: passing a null handle returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_destroy(engine_handle_t handle);

/*
 * Opens a game package/root directory.
 * handle and game_root_path_utf8 must be non-null.
 * startup_script_utf8 may be null to use default startup script.
 */
ENGINE_API_EXPORT engine_result_t
engine_open_game(engine_handle_t handle, const char *game_root_path_utf8,
                 const char *startup_script_utf8);

/*
 * Starts game opening asynchronously on a background worker.
 * Returns immediately when the startup task is scheduled.
 */
ENGINE_API_EXPORT engine_result_t
engine_open_game_async(engine_handle_t handle, const char *game_root_path_utf8,
                       const char *startup_script_utf8);

/*
 * Gets async startup state.
 * out_state must be non-null.
 */
ENGINE_API_EXPORT engine_result_t
engine_get_startup_state(engine_handle_t handle, uint32_t *out_state);

/*
 * Drains startup logs into caller buffer as UTF-8 text.
 * Each log line is terminated by '\n'.
 * Returns bytes written in out_bytes_written.
 */
ENGINE_API_EXPORT engine_result_t
engine_drain_startup_logs(engine_handle_t handle, char *out_buffer,
                          uint32_t buffer_size, uint32_t *out_bytes_written);

/*
 * Directs all engine (spdlog) output to a file: rotates at 4 MiB, keeps 3.
 * On mobile the plugin passes the app sandbox writable path
 * (Documents / filesDir) + "/krkr2_engine.log" at startup, so logs are
 * retrievable when the debug console is closed or stdout is not captured
 * (e.g. idevicesyslog on iOS does NOT surface app stdout).
 * Passing NULL or "" disables file logging (stdout sinks stay active).
 */
ENGINE_API_EXPORT engine_result_t engine_set_log_file_path(const char *path);

/*
 * Ticks engine main loop once.
 * handle must be non-null.
 * delta_ms is caller-provided elapsed milliseconds.
 * Returns ENGINE_RESULT_GAME_TERMINATED once the game asked to quit
 * (TJS `System.exit()`); the host should then leave the game screen and
 * destroy the engine. Every later call returns the same code.
 *
 * 驱动引擎主循环一帧。游戏提出退出（TJS `System.exit()`）后返回
 * ENGINE_RESULT_GAME_TERMINATED，宿主应据此离开游戏界面并销毁引擎；
 * 之后的每次调用都返回同一个码。
 * 游戏仍在启动时返回 ENGINE_RESULT_STARTUP_PENDING（同样**不是错误**，
 * 宿主不该把它计入失败次数）。
 */
ENGINE_API_EXPORT engine_result_t engine_tick(engine_handle_t handle,
                                              uint32_t delta_ms);

/*
 * Pauses runtime execution.
 * Idempotent: calling pause on a paused engine returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_pause(engine_handle_t handle);

/*
 * Resumes runtime execution.
 * Idempotent: calling resume on a running engine returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_resume(engine_handle_t handle);

/*
 * Cancels a pending game-requested termination (the state that makes
 * engine_tick return ENGINE_RESULT_GAME_TERMINATED).
 *
 * Host flow: on ENGINE_RESULT_GAME_TERMINATED ask the player whether to quit.
 *   - quit    -> destroy the engine as usual;
 *   - keep on -> call this, then keep ticking and the game continues.
 * Host mode never tears anything down while the termination is pending (it does
 * not exit the process), so cancelling just clears the flag. Idempotent: with no
 * termination pending it returns ENGINE_RESULT_OK and changes nothing.
 * Must be called on the engine's owner thread (the engine_tick thread).
 *
 * 取消"游戏请求退出"（即让 engine_tick 返回 ENGINE_RESULT_GAME_TERMINATED 的状态）。
 * 宿主流程：收到该结果码后弹确认框 —— 退出就照常销毁引擎；继续游戏就调本函数，
 * 之后照常 tick，游戏继续跑。终止挂起期间并没有真的拆掉任何东西（宿主模式不结束
 * 进程），所以"取消"只需清标志。幂等；必须在引擎 owner 线程（与 engine_tick 同一
 * 线程）调用。
 */
ENGINE_API_EXPORT engine_result_t
engine_cancel_termination(engine_handle_t handle);

/*
 * Answers a pending window-close request (the state that makes engine_tick return
 * ENGINE_RESULT_WINDOW_CLOSE_REQUESTED).
 *
 * Host flow: show a confirmation dialog.
 *   - allow != 0 -> quit: the engine performs the real window close, and the next
 *     engine_tick returns ENGINE_RESULT_WINDOW_CLOSED so the host leaves the game
 *     screen as usual.
 *   - allow == 0 -> keep playing: the request is dropped, nothing was torn down,
 *     and the game continues from exactly where it was (this is what makes
 *     "keep playing" meaningful for KAG's quit menu, unlike WINDOW_CLOSED).
 * Idempotent: with nothing pending, allow==0 returns ENGINE_RESULT_OK; allow!=0 on
 * an already terminated runtime returns ENGINE_RESULT_INVALID_STATE.
 * Must be called on the engine's owner thread (the engine_tick thread).
 *
 * 答复"游戏请求关窗"（engine_tick 返回 ENGINE_RESULT_WINDOW_CLOSE_REQUESTED 的状态）。
 * 宿主收到该结果码后弹确认框：allow!=0 退出（引擎执行真正的关窗，下一帧 tick 报
 * WINDOW_CLOSED，宿主照常离开游戏界面）；allow==0 继续游戏（丢掉请求，什么都没拆，
 * 游戏从原处接着跑 —— 这正是 KAG 退出菜单里"继续游戏"能成立的原因，与
 * WINDOW_CLOSED 那种"窗口已经没了"的情形不同）。幂等；必须在引擎 owner 线程调用。
 */
ENGINE_API_EXPORT engine_result_t
engine_resolve_window_close(engine_handle_t handle, int32_t allow_close);

/*
 * Sets runtime option by UTF-8 key/value pair.
 * handle and option must be non-null.
 */
ENGINE_API_EXPORT engine_result_t
engine_set_option(engine_handle_t handle, const engine_option_t *option);

/*
 * Sets logical render surface size in pixels.
 * width and height must be greater than zero.
 */
ENGINE_API_EXPORT engine_result_t engine_set_surface_size(
    engine_handle_t handle, uint32_t width, uint32_t height);

/*
 * Gets current frame descriptor.
 * out_frame_desc->struct_size must be initialized by caller.
 * The descriptor and readback buffer may differ after a surface resize; callers
 * should use the latest descriptor before reading pixels.
 * surface resize
 * 后描述信息和回读缓冲区可能短暂不一致；读取像素前应重新获取描述信息。
 */
ENGINE_API_EXPORT engine_result_t engine_get_frame_desc(
    engine_handle_t handle, engine_frame_desc_t *out_frame_desc);

/*
 * Reads current frame into caller-provided RGBA8888 buffer.
 * out_pixels_size must be >= stride_bytes * height from engine_get_frame_desc.
 * The returned error string is owned by the handle and may change on the next
 * API call; copy it if it must outlive that call.
 * 错误字符串由 handle 持有，下一次 API 调用可能改变；需要长期保存时请自行复制。
 */
ENGINE_API_EXPORT engine_result_t engine_read_frame_rgba(
    engine_handle_t handle, void *out_pixels, size_t out_pixels_size);

/*
 * Gets host-native render window handle.
 * On macOS runtime build this is NSWindow*.
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 */
ENGINE_API_EXPORT engine_result_t
engine_get_host_native_window(engine_handle_t handle, void **out_window_handle);

/*
 * Gets host-native render view handle.
 * On macOS runtime build this is NSView* (typically the GLFW content view).
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 */
ENGINE_API_EXPORT engine_result_t
engine_get_host_native_view(engine_handle_t handle, void **out_view_handle);

/*
 * Sends one input event to the runtime.
 * event->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t
engine_send_input(engine_handle_t handle, const engine_input_event_t *event);

/*
 * Sets an IOSurface as the render target for the engine.
 * When set, engine_tick renders directly to this IOSurface (zero-copy),
 * bypassing the glReadPixels path used by engine_read_frame_rgba.
 *
 * iosurface_id: The IOSurfaceID obtained from IOSurfaceGetID().
 *               Pass 0 to detach and revert to the default Pbuffer mode.
 * width/height: Dimensions of the IOSurface in pixels.
 *
 * Platform: macOS only. Returns ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 */
ENGINE_API_EXPORT engine_result_t engine_set_render_target_iosurface(
    engine_handle_t handle, uint32_t iosurface_id, uint32_t width,
    uint32_t height);

/*
 * Sets an Android Surface (from SurfaceTexture) as the render target.
 * When set, engine_tick renders to an EGL WindowSurface created from the
 * ANativeWindow. eglSwapBuffers() delivers frames directly to the host's
 * SurfaceTexture (GPU zero-copy).
 *
 * native_window: ANativeWindow* obtained from ANativeWindow_fromSurface().
 *                Pass NULL to detach and revert to the default Pbuffer mode.
 * width/height: Dimensions in pixels.
 *
 * Platform: Android only. Returns ENGINE_RESULT_NOT_SUPPORTED on other
 * platforms.
 */
ENGINE_API_EXPORT engine_result_t
engine_set_render_target_surface(engine_handle_t handle, void *native_window,
                                 uint32_t width, uint32_t height);

/*
 * Queries whether the last engine_tick produced a new rendered frame.
 * out_
 *
 * out_
 *   - 0: no new frame since last query
 *   - 1: a new frame was rendered
 *
 * This is useful in IOSurface mode to know when to call
 * textureFrameAvailable() on the host-shell side.
 */
ENGINE_API_EXPORT engine_result_t
engine_get_frame_rendered_flag(engine_handle_t handle, uint32_t *out_);

/*
 * Queries the graphics renderer information string.
 * Writes a null-terminated UTF-8 string into out_buffer describing
 * the active graphics backend (e.g. "Metal", "OpenGL ES", "D3D11").
 *
 * out_buffer and buffer_size must be non-null / > 0.
 * If the buffer is too small the string is truncated.
 * Returns ENGINE_RESULT_INVALID_STATE if the runtime is not active.
 */
ENGINE_API_EXPORT engine_result_t engine_get_renderer_info(
    engine_handle_t handle, char *out_buffer, uint32_t buffer_size);

/**
 * 读回**当前生效**的游戏兼容档，写成 `<profile> <mode>`（空格分隔，例如
 * `aetherkiri alias`），供壳把"这个游戏按哪条血脉跑"显示在性能叠加层上。
 *
 * 解析在 engine_set_option 里完成（见 engine_options.h 的
 * ENGINE_OPTION_GAME_COMPAT_PROFILE），所以本函数只是读结果：
 * 还没解析过时写入空串。**不需要 handle，可从任意线程调用。**
 * 缓冲区不足时截断；成功返回 ENGINE_RESULT_OK。
 */
ENGINE_API_EXPORT engine_result_t engine_get_compat_profile(
    char *out_buffer, uint32_t buffer_size);

/*
 * Gets runtime memory/cache statistics snapshot.
 * out_stats->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_get_memory_stats(
    engine_handle_t handle, engine_memory_stats_t *out_stats);

/*
 * Lists the window menu items registered by the game (KiriKiri's
 * tTVPMenuItem / Window.menu — the menu bar Windows builds show under the
 * title bar). Android has no OS menu bar, so the host renders them itself.
 *
 * The menu tree is serialized as text into out_buffer, one item per line,
 * fields separated by '\t':
 *
 *     depth <TAB> checked <TAB> enabled <TAB> id <TAB> title
 *
 * Lines end with '\n'. `id` is the item's path (top level "0", "1", …, a
 * submenu child "0.2", …) and is what engine_invoke_window_menu() takes.
 * `checked`/`enabled` are '0' or '1'. Titles have tabs/newlines replaced by
 * spaces. Items with visible == false are omitted.
 *
 * Returns the number of bytes written (excluding the NUL terminator) in
 * out_bytes_written; an empty menu yields 0. The buffer is truncated on a
 * line boundary if it is too small.
 *
 * Safe to call from any thread: the engine keeps a snapshot refreshed on its
 * owner thread (the menu tree is only mutated there).
 *
 * 列出游戏注册的窗口菜单项（KiriKiri 的 tTVPMenuItem / Window.menu，即 Windows
 * 版标题栏下方菜单栏那一套）。Android 没有系统菜单栏，所以由宿主自己渲染。
 * 序列化格式与线程约束见上（任意线程可调；引擎在 owner 线程维护快照）。
 */
ENGINE_API_EXPORT engine_result_t
engine_list_window_menu(char *out_buffer, uint32_t buffer_size,
                        uint32_t *out_bytes_written);

/*
 * Invokes a window menu item by id (see engine_list_window_menu).
 *
 * Only enqueues the request: the actual invocation runs on the engine's owner
 * thread during engine_tick, because the menu objects belong to the TJS
 * runtime. Callable from any thread. Returns ENGINE_RESULT_INVALID_ARGUMENT
 * for a null/empty id (an unknown id is dropped silently at dispatch time).
 *
 * 按 id 触发窗口菜单项。**只入队**，真正触发在 engine_tick（引擎 owner 线程）
 * 上做（菜单对象属于 TJS 运行时），因此可从任意线程调用。
 */
ENGINE_API_EXPORT engine_result_t
engine_invoke_window_menu(const char *item_id_utf8);

/*
 * Returns last error message as UTF-8 null-terminated string.
 * The returned pointer remains valid until next API call on the same handle.
 * Returns empty string when no error is recorded.
 */
ENGINE_API_EXPORT const char *engine_get_last_error(engine_handle_t handle);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* KRKR2_ENGINE_API_H_ */
