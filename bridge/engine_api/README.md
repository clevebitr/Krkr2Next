```
engine_api_internal.h        // 共享声明 + engine_handle_s 定义（内部头，仅本目录使用）
engine_api_state.cpp         // 全局状态 + 句柄校验等基础辅助
engine_api_logging.cpp       // 日志、崩溃处理、文件 sink、帧耗时统计
engine_api_compat.cpp        // 兼容档解析
engine_api_input.cpp         // 输入事件队列与派发
engine_api_menu.cpp          // 窗口菜单快照/触发
engine_api_lifecycle.cpp     // create/destroy/open/tick/pause/resume/startup
engine_api_frame.cpp         // surface、帧回读、renderer/memory 信息
engine_api_options.cpp       // engine_set_option
engine_api_misc.cpp          // 版本、错误串、rendered flag、modal 状态
engine_api_stub.cpp          // 无运行时构建的桩实现
engine_api_android_jni.cpp   // Android JNI 入口（krkr_GetNativeWindow 等，仅 ANDROID）
android/omp_env_compat.c     // 翻译环境 libomp 垫片，由 build_engine_android.sh 单独编译
```
