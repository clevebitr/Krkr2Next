plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "org.dpdns.clevebitr"
    compileSdk = 35

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
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
