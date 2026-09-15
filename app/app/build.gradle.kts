plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "org.dpdns.clevebitr"
    // compose 1.11.4 与 material3 1.5.0-alpha18 只要求 compileSdk 35；这里取 36
    // （CI 已装 platforms;android-36）。
    //
    // 不要为了"用更新的东西"升到 37：compileSdk 37 与 AGP 9.1.0 是同一条门槛上的
    // 两半，见下面 dependencies 里的版本表。
    compileSdk = 36

    defaultConfig {
        applicationId = "org.dpdns.clevebitr"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        // 只构建 arm64-v8a。引擎（vcpkg triplet arm64-android）与 NDK 运行时
        // 都按此 ABI 配置，32 位/模拟器 ABI 不在支持范围。
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    // 引擎共享库在 Gradle 之外构建（scripts/build_engine_android.sh）后投放到
    // src/main/jniLibs/arm64-v8a/libengine_api.so，Gradle 自动打包进 APK。
    //
    // 这里刻意不使用 externalNativeBuild：根 CMakeLists.txt 会设置 vcpkg 的
    // CMAKE_TOOLCHAIN_FILE，与 Gradle 传入的 NDK toolchain file 冲突。

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions {
            jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        }
    }

    buildFeatures {
        compose = true
    }

    sourceSets {
        getByName("main") {
            kotlin.srcDirs("src/main/kotlin")
        }
    }
}

dependencies {
    // ── 全部走稳定版，不引入任何 alpha ──────────────────────────────────────
    // BOM 2026.06.01 锁 compose ui/foundation/runtime = 1.11.4、material3 = 1.4.0。
    // 这是**最后一批配 AGP 8.x 的稳定组合**：compose 1.12.0-alpha02 起与 material3
    // 1.5.0-alpha19 起都把 minAndroidGradlePluginVersion 抬到 9.1.0、minCompileSdk
    // 抬到 37，而本工程是 AGP 8.13.2 / compileSdk 36——越过去 Gradle 的 AAR 元数据
    // 检查会直接失败（报 "requires Android Gradle plugin 9.1.0 or higher" 之类，
    // 不是编译错误）。
    //
    // 之所以放弃 Material 3 Expressive：它的 MaterialExpressiveTheme / MotionScheme
    // / ToggleButton 只在 material3 1.5.0-alpha 里是 public（稳定版里是
    // `internal fun`，调不到），而 alpha 又被上面那条 AGP 9 门槛卡住。为三个 API 把
    // AGP 连跳大版本、连带 Gradle 与 JDK 一起动不划算，UI 已改用标准 MD3 的等价物
    // （MaterialTheme / SegmentedButton）。
    //
    // 要动这些版本：先把 AAR 里的 META-INF/com/android/build/gradle/
    // aar-metadata.properties 拉下来看一眼 minCompileSdk 与
    // minAndroidGradlePluginVersion（0.1 MB 的 material3-ripple AAR 就够，门槛与
    // material3 本体一致）。
    implementation(platform("androidx.compose:compose-bom:2026.06.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    // 图标必须留 extended：本 App 用的 ArrowBack / Close / MoreVert / PlayArrow /
    // Settings 都在 core 里，但 **Folder 不在**——core 只有 49 个 filled 图标，目录
    // 列表那一行的文件夹图标就靠它。删掉 extended 会让 Folder 变成 unresolved
    // reference（只在 Kotlin 编译期暴露，Gradle 依赖检查看不出来）。
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
