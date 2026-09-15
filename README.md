# KrKr2-Next-Compose

面向 **Android** 的 KiriKiri2（吉里吉里2）运行环境：原生 Kotlin/Compose 宿主壳 +
C++ 引擎核心，直连平台 EGL/GLES3 渲染，通过零拷贝 SurfaceTexture 把引擎画面交给宿主。

不使用 ANGLE（只面向 Android，不需要 GLES→Metal 翻译层），不使用 Flutter（宿主只有
一个平台，换不来收益）。仅 `arm64-v8a`，`minSdk 24`。

## 构建

引擎在 Gradle **之外**独立构建：根 `CMakeLists.txt` 会设置 vcpkg 的
`CMAKE_TOOLCHAIN_FILE`，与 Gradle 传入的 NDK toolchain file 冲突。

```bash
./build.sh debug                    # 引擎 + APK
./build.sh release
./build.sh release --engine-only    # 只构建 libengine_api.so 并投放进 jniLibs
./build.sh debug --apk-only         # 只跑 Gradle（引擎产物已就位）
JOBS=16 ./build.sh release

# Linux 宿主验证：不需要 NDK，只验引擎核心能编译、测试能跑
cmake --preset "Linux Debug Config" && cmake --build --preset "Linux Debug Build"
```

| 前置 | 要求 |
|---|---|
| Android NDK | **27.0.12077973**（NDK 29 会让 `libffi:arm64-android` 交叉编译失败） |
| JDK / CMake / Ninja | 17 / ≥ 3.28 / 任意 |
| vcpkg | 可选；未设 `VCPKG_ROOT` 时自举到 `.devtools/vcpkg` 并钉在固定 commit |
| Linux 宿主验证 | 额外需要 `bison`、`libegl1-mesa-dev`、`libgles2-mesa-dev` |

产物：`out/android/<type>/bridge/engine_api/libengine_api.so`
→ `app/app/src/main/jniLibs/arm64-v8a/`（已 gitignore）→ APK。该 `.so` 是自包含的，
引擎核心、插件和 vcpkg 静态依赖全部链在里面；`build_engine_android.sh` 会同时把 NDK
运行时依赖（`libomp.so` / `libc++_shared.so`）拷进 `jniLibs/`，漏拷会导致运行时
`dlopen` 失败、界面一直转圈。

## 校验

```bash
bash scripts/check_static.sh          # 无需 NDK/vcpkg/cmake，Termux 与 CI 都能跑
python3 scripts/check_gl_symbols.py   # 需平台 GL 库，按平台单独跑
scripts/verify_engine_so.sh out/android/debug/bridge/engine_api/libengine_api.so "$ANDROID_NDK_HOME"
```

CI：`.github/workflows/android_build.yml`（引擎构建 → `.so` 符号断言 → Gradle 打包 →
校验 APK 内含引擎库）。

## 硬约束

改代码前必读。这些是不变量，不是建议。

1. **不使用 ANGLE**。Android 直连原生 EGL + GLES3；不得引入 ANGLE 的 display 获取路径、vcpkg `angle` 端口或 `EGL_PLATFORM_ANGLE_*` 常量。
2. **EGL context 是线程绑定的**。`eglMakeCurrent` 之后，创建 / tick / 销毁必须在同一线程；壳用单一渲染线程持有 EGL，UI 线程只调 `nativeSetSurface`，绝不触碰 EGL。context 重建后不得复用旧纹理、FBO、shader 或扩展状态，GPU 对象绑定 context generation。
3. **输入**。`key_code` 是 **Windows VK 码**（取值见 `cpp/core/environ/vkdefine.h`），不是 Android `KEYCODE_*`，必须经 `VkCodes.fromAndroid` 显式映射；指针坐标是**物理像素**，不要乘 density；返回键必须发 keyDown + BACK + keyUp 三个事件。
4. **诊断探针**统一 `KRKR_RENDER_PROBE`（`-DENABLE_RENDER_PROBE=ON`）且默认关闭；高频日志必须采样 / 限频 / 去重 / 仅边沿；探针不得改变结果、时序、生命周期或性能，且不得记录完整用户文本、个人路径或设备标识。
5. **链接**。插件源码经 CMake 目标源（`INTERFACE_SOURCES`）以普通链接进入共享库；**禁止 `--whole-archive`**（psbfile / motionplayer 对象重复，`ld.lld` 报重复符号）。
6. **Cubism SDK 可选且不入库**。`cpp/plugins/cubism/{Framework,Core/lib}` 已 gitignore，从 live2d.com 自行获取；缺失时自动禁用，不得阻断核心构建。CI 上没有它，所以要在 CI 里产出带 Live2D 的 APK，得把 SDK 放到**本仓库之外**再让 CI 还原：建一个私有仓库，把 SDK 压成 zip 传成 release asset，设置 `CUBISM_SDK_GH`（`owner/repo`）与 `CUBISM_SDK_TOKEN`（该仓库只读 contents 的 fine-grained PAT）两个 secret，`.github/workflows/android_build.yml` 里的「还原 Live2D Cubism SDK」步骤会调 `scripts/restore_cubism_sdk.sh` 拉下来。该脚本区分两件事：没配来源就警告后继续；配了却拉不到/布局不对就**硬失败**，并且还原成功的轮次里 `verify_engine_so.sh` 会断言二进制确实含 Live2D 插件——避免"以为有 Live2D、其实 CMake 静默跳过"。
7. **JNI 符号名编码包名与类名**。Kotlin 的 `external` 方法与 C++ JNI 声明必须同步，不一致只在运行时以 `UnsatisfiedLinkError` 暴露；改动公共 C ABI 时同步检查 Kotlin 声明、生命周期和版本约束。
8. **平台守卫不得按目录名判断**。`sound/win32/`、`utils/win32/`、`environ/win32/` 中有部分实现跨平台共享，不能当 Windows 专属删除。
9. **像素混合基准**。`cpp/core/visual/tvpgl.cpp` 的标量实现是基准；SIMD 改动须逐像素覆盖透明度、边界、溢出和负值路径，未验证的 PS 混合保持标量回退。

## 许可

GPL-3.0，继承自 [PocketKrKr](https://github.com/FiresonZ/PocketKrKr)。第三方组件与授权见各依赖自带许可。
