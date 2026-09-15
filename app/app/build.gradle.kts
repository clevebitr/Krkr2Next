plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "org.dpdns.clevebitr"
    // 36 是 androidx compose 1.12 / foundation 1.13 的硬要求（低于它 AGP 会直接报
    // "dependency requires compileSdk 36"）。本机 SDK 需先装 platforms;android-36。
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
    // Material 3 Expressive（MD3E）只在 material3 1.5.0-alpha 里是 public：稳定版
    // 1.4.0 里 MaterialExpressiveTheme 是 internal，ButtonGroup /
    // FloatingActionButtonMenu / LoadingIndicator / 浮动工具栏 / MaterialShapes 根本
    // 不存在。所以 BOM 锁稳定的 compose 1.12.1，material3 单独显式升到
    // 1.5.0-alpha28（其传递依赖会顺带把 foundation 抬到 1.13.0-alpha）。
    implementation(platform("androidx.compose:compose-bom:2026.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3:1.5.0-alpha28")
    // 不再依赖 material-icons-extended：本 App 只用到 core 图标（Close /
    // ArrowBack / Settings / PlayArrow / MoreVert / Folder），而该构件在新 BOM 下
    // 已冻结在 1.7.8。
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
