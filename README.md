# KrKr2-Next-Compose

面向 **Android** 的 KiriKiri2（吉里吉里2）运行环境。

原生 Kotlin/Compose 宿主壳 + C++ 引擎核心，直连平台自带的 EGL/GLES3 渲染，
通过零拷贝 SurfaceTexture 把引擎画面交给宿主显示。

> 项目从 [PocketKrKr](https://github.com/FiresonZ/PocketKrKr) fork 而来（保留完整
> 历史，基线标签 `pocketkrkr-base`），在其 C++ 引擎核心上演进：移除 Flutter 壳、
> 移除 ANGLE 翻译层、改为仅面向 Android。
>
> English README 待补。

## 状态

**内核里程碑（M1）**：打通「选目录 → 启动游戏 → 运行 → 退出」。
游戏库、ROM 刮削、逐游戏兼容性档案属于后续阶段，尚未实现。

引擎侧仍继承 PocketKrKr 的已知未完成项：Z-compat 主渲染路径的目标绑定、
视频合成落屏、Layer 特效像素实现、复杂 M2 motion。详见
[开发文档](docs/dev/README.md)。

## 架构

```
┌──────────────────────────────────────────────┐
│ Kotlin / Compose 壳            app/          │
│  LauncherScreen · GameScreen · EngineSession │
└──────────────┬───────────────────────────────┘
               │ JNI（System.loadLibrary + 符号绑定）
┌──────────────▼───────────────────────────────┐
│ bridge/engine_api    C ABI 0x01000000         │
└──────────────┬───────────────────────────────┘
               │ 链接 krkr2core + krkr2plugin
┌──────────────▼───────────────────────────────┐
│ cpp/core   TJS2 VM · XP3 · sound · movie      │
│  iTVPRenderManager  ← 渲染接缝                │
│    └ tTVPRenderManager_OpenGL（GLSL）         │
│       └ 原生 EGL + GLES3                      │
│          └ SurfaceTexture 零拷贝              │
└──────────────────────────────────────────────┘
```

**为什么不用 ANGLE**：ANGLE 的价值在于把 GLES 翻译到 Metal 以复用到 iOS。
只面向 Android 时它是多余的一层翻译 + APK 膨胀 + 需要长期维护的本地补丁，
因此改为直连平台自带的 EGL/GLES3。

**为什么不用 Flutter 壳**：同上——壳只有一个平台时，Flutter 运行时（约
15-20MB）换不来任何好处，反而多一层 Dart FFI。

## 构建

引擎在 Gradle **之外**独立构建（根 `CMakeLists.txt` 会设置 vcpkg 的
`CMAKE_TOOLCHAIN_FILE`，与 Gradle 传入的 NDK toolchain file 冲突）。

```bash
./build.sh debug                    # 引擎 + APK
./build.sh release
./build.sh release --engine-only    # 只出 libengine_api.so 并投放进 jniLibs
./build.sh debug --apk-only         # 只跑 Gradle

# Linux 宿主验证（无需 NDK，校验引擎核心可编译）
cmake --preset "Linux Debug Config" && cmake --build --preset "Linux Debug Build"
```

**前置要求**

| 项 | 版本 / 说明 |
|---|---|
| 主机 | Linux / macOS / Windows（iOS 已移除，不需要 macOS） |
| Android NDK | **27.0.12077973**（NDK 29 会破坏 `libffi:arm64-android`） |
| JDK | 17 |
| CMake / Ninja | ≥ 3.28 / 任意 |
| vcpkg | 未设置 `VCPKG_ROOT` 时自动自举到 `.devtools/vcpkg`（钉在固定 commit） |
| ABI | 仅 `arm64-v8a`；`minSdk 24` |

无需 Flutter SDK。

**产物**

| 目标 | 路径 |
|---|---|
| 引擎共享库 | `out/android/<type>/bridge/engine_api/libengine_api.so` |
| 投放位置（已 gitignore） | `app/app/src/main/jniLibs/arm64-v8a/libengine_api.so` |
| APK | `app/app/build/outputs/apk/**/*.apk` |

## 校验

无 NDK 环境下也能跑：

```bash
python3 scripts/check_jni_symbols.py     # Kotlin external ↔ C++ JNI 符号一致性
```

有构建产物时：

```bash
scripts/verify_engine_so.sh out/android/debug/bridge/engine_api/libengine_api.so "$ANDROID_NDK_HOME"
```

CI（`.github/workflows/android_build.yml`）跑完整链路：引擎构建 → `.so` 符号/依赖
断言 → Gradle 打包 → 校验 APK 内含引擎库。Linux 宿主验证见
`engine_verify.yml`。

## 权限

引擎消费**裸文件系统路径**（`engine_open_game(path)`），SAF 的 `content://` URI
无法直接解析为真实路径，因此需要完整的文件访问权限：API 30+ 引导用户授予
`MANAGE_EXTERNAL_STORAGE`，API 24-28 走 `READ_EXTERNAL_STORAGE`。

## 许可

GPL-3.0，继承自 PocketKrKr。第三方组件与授权见
[THIRD_PARTY](docs/dev/krkrz-compat.md) 与各依赖自带许可。
