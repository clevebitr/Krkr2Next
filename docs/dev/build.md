# 构建

Android-only。宿主壳是原生 Kotlin/Compose 应用，引擎是 C++ 共享库。

## 为什么引擎在 Gradle 之外构建

根 `CMakeLists.txt` 会设置 vcpkg 的 `CMAKE_TOOLCHAIN_FILE`；Gradle 的
`externalNativeBuild` 会传入 NDK 自己的 toolchain file。两者同时生效会被 CMake
判定为 toolchain 重定义而行为异常。

因此流程是：**CMake 出 `.so` → 投放进 `jniLibs/` → Gradle 打包**。
`build.sh` 把这三步串起来，也可以分开跑（CI 就是分开跑的）。

（`CMakeLists.txt` 里的 toolchain 设置带 `if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)`
守卫，所以将来要接 `externalNativeBuild` 时不会被挡住。）

## 命令

```bash
./build.sh debug                     # 引擎 + APK
./build.sh release
./build.sh release --engine-only     # 只构建并可投放 libengine_api.so
./build.sh debug --apk-only          # 只跑 Gradle（引擎产物已就位）

JOBS=16 ./build.sh release           # 并行度

# Linux 宿主验证：不需要 NDK，只验引擎核心能编译、测试能跑
cmake --preset "Linux Debug Config" && cmake --build --preset "Linux Debug Build"
```

`build.sh` 是薄封装，实际逻辑在 `scripts/build_engine_android.sh`。

## 前置要求

| 项 | 要求 |
|---|---|
| Android NDK | **27.0.12077973**。NDK 29 会让 `libffi:arm64-android` 交叉编译失败。 |
| JDK | 17（AGP 8.x 要求） |
| CMake | ≥ 3.28 |
| Ninja | 任意版本 |
| vcpkg | 可选。未设 `VCPKG_ROOT` 时脚本自举到 `.devtools/vcpkg` 并钉在固定 commit。 |
| Linux 宿主验证 | 额外需要 `bison`、`libegl1-mesa-dev`、`libgles2-mesa-dev` |

不需要 Flutter SDK。

**NDK 定位顺序**：`ANDROID_NDK_HOME` → `ANDROID_NDK_ROOT` → `$ANDROID_HOME/ndk/*`
取最新。因为第三条会误选托管 runner 上预装的更新版本，CI 里显式安装并指定
27.0.12077973，不依赖"取最新"。

## 为什么 vcpkg 要钉 commit

`.devtools/vcpkg` 固定到 `scripts/build_engine_android.sh` 里的
`VCPKG_PINNED_COMMIT`。跟随滚动 tip 会让 vcpkg 的 ABI 版本逐次漂移，二进制缓存
全部失效，所有依赖重编（约 40 分钟）。钉住后 ABI 稳定，缓存才真正复用。

## 产物

| 目标 | 路径 |
|---|---|
| 引擎共享库 | `out/android/<type>/bridge/engine_api/libengine_api.so` |
| 投放位置（已 gitignore） | `app/app/src/main/jniLibs/arm64-v8a/libengine_api.so` |
| APK | `app/app/build/outputs/apk/**/*.apk` |

`libengine_api.so` 是**自包含**的：引擎核心、插件、vcpkg 静态依赖全部链进去，
APK 只需要带这一个原生库。

### NDK 运行时依赖

`build_engine_android.sh` 会读 `.so` 的 `DT_NEEDED`，把 NDK 运行时库
（典型是 `libomp.so` / `libc++_shared.so`）一并拷进 `jniLibs/`。漏拷的话运行时
`dlopen` 报 `library ... not found needed by libengine_api.so`，表现为引擎加载失败、
界面一直转圈。真机日志里看 `nativelogger` 能看到这条。

### Android 链接约定

插件源码通过 CMake 目标源（`INTERFACE_SOURCES`）传播进共享库，用普通链接。
**禁止 `--whole-archive`**——会让 psbfile/motionplayer 的对象重复，触发 `ld.lld`
重复符号错误。

## 校验

### 本地静态检查（不需要 NDK / vcpkg / cmake）

```bash
bash scripts/check_static.sh
```

聚合入口，依次跑三项：

| 检查 | 脚本 | 拦什么 |
|---|---|---|
| JNI 符号一致性 | `scripts/check_jni_symbols.py` | Kotlin 的 `external` 方法与 C++ JNI 符号一一对应。符号名编码包名与类名，不一致只在运行时抛 `UnsatisfiedLinkError`，编译期不报错 |
| 移植溯源 | `scripts/check_port_drift.py` | 与 AetherKiri 共享的文件无未经承认的漂移（见 `compat/upstream/aetherkiri_ports.json`） |
| 本地语法检查 | `scripts/check_syntax.sh` | 对头文件依赖少的源文件跑 `clang -fsyntax-only`（Catch2 用语法垫片替代） |

**关于本地能做什么**：Termux 里没有 cmake / ninja / NDK / vcpkg，**完整引擎构建
不可能**。但 Termux 自带 clang，因此**只依赖少量头文件的源文件是可以先编译一遍的**
——`check_syntax.sh` 就利用这一点，把"拼错符号名 / 缺 include / 断言写错 / 结构体
偏移算错"这类问题在推送前拦下来，而不是等几分钟的 CI。

它的边界：只证明**能编译**，不证明行为正确；依赖 spdlog / ffmpeg / vcpkg 三方库的
源文件加不进去，那类只能靠 CI。

改了移植文件后：

```bash
python3 scripts/check_port_drift.py --update   # 刷新哈希，并改 modifications 字段
```

### 有构建产物时

```bash
scripts/verify_engine_so.sh out/android/debug/bridge/engine_api/libengine_api.so "$ANDROID_NDK_HOME"
```

断言：24 个 `engine_*` C ABI 符号齐全、`NativeEngine` 的三个 JNI 符号存在、
动态依赖是平台原生 `libEGL.so`/`libGLESv2.so`、二进制中不含 ANGLE 平台常量。

## CI

两条流水线，都在 push/PR 上自动跑：

**`.github/workflows/engine_verify.yml` — 引擎行为验证（Linux 宿主）**

最快的正确性信号：不需要 NDK，5-10 分钟出结果。先跑 `check_static.sh`，再编译
引擎核心与工具，最后 `ctest --no-tests=error` 跑 `tests/` 下的用例
（Catch2 单元测试 + `tvpgl_simd_compare` 的 SIMD 逐像素比对）。

`--no-tests=error` 是刻意的：测试目录存在却没注册到任何用例属于配置错误
（例如 `catch_discover_tests` 漏调），必须失败而不是静默通过——否则测试被悄悄
摘掉也不会有人发现。

**`.github/workflows/android_build.yml` — Android 交叉编译与打包**

1. **engine-build** — `check_static.sh` → 装 NDK 27 → 构建 `libengine_api.so` →
   `verify_engine_so.sh` 符号断言 → 上传产物。独立成 job 是为了让引擎侧问题在
   Gradle 之前就失败。
2. **android-package** — 下载引擎产物放进 `jniLibs/`，Gradle 打包，校验 APK 内含
   `lib/arm64-v8a/libengine_api.so`，上传 APK。

**`gradle-wrapper.jar` 未入库**（二进制）。CI 用 `gradle/actions/setup-gradle`
直接提供 gradle，不走 `./gradlew`。本地构建前需要生成一次：

```bash
cd app && gradle wrapper --gradle-version 8.11.1
```

## 常见问题

- **找不到 bison**：TJS2 parser 需要它。Linux 宿主验证构建必须装。
- **vcpkg 首次构建很慢**：要交叉编译 `arm64-android` 全部依赖。用
  `VCPKG_BINARY_SOURCES` 开二进制缓存，并在 CI 里缓存 `~/.cache/vcpkg`。
- **链接期报 `EGL`/`GLESv2` 找不到**（Linux 宿主）：装
  `libegl1-mesa-dev libgles2-mesa-dev`。Android 侧由 NDK sysroot 提供，不需要额外操作。
- **运行期 `UnsatisfiedLinkError`**：JNI 符号名与 Kotlin 侧不一致。跑
  `scripts/check_jni_symbols.py` 定位。
- **启动后一直转圈**：先看 `nativelogger`/`dlopen` 报错（缺 NDK 运行时依赖），
  再看 `KiriNext/Engine` 标签的日志。
