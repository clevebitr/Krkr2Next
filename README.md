# KrKr2-Next-Compose

## 注意
### 项目正在重构！可能会出现大量错误以及兼容性问题  


---
面向 **Android** 的 KiriKiri2（吉里吉里2）运行环境：原生 Kotlin/Compose 宿主壳 +
C++ 引擎核心，直连平台 EGL/GLES3 渲染，通过零拷贝 SurfaceTexture 把引擎画面交给宿主。

不使用 ANGLE；仅 `arm64-v8a`，`minSdk 24`。

## 构建

引擎在 Gradle **之外**独立构建：根 `CMakeLists.txt` 会设置 vcpkg 的
`CMAKE_TOOLCHAIN_FILE`，与 Gradle 传入的 NDK toolchain file 冲突。

```bash
./build.sh debug                    # 引擎 + APK
./build.sh release
./build.sh release --engine-only    # 只构建 libengine_api.so 并投放进 jniLibs
./build.sh debug --apk-only         # 只跑 Gradle（引擎产物已就位）
JOBS=16 ./build.sh release

# Linux 宿主验证
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

CI：`.github/workflows/android_build.yml`

## 许可

GPL-3.0，继承自 [PocketKrKr](https://github.com/FiresonZ/PocketKrKr)。第三方组件与授权见各依赖自带许可。
