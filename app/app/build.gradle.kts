import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}
fun gitShortHash(dir: File): String = try {
    val process = ProcessBuilder("git", "rev-parse", "--short=6", "HEAD")
        .directory(dir)
        .redirectErrorStream(true)
        .start()
    val output = process.inputStream.bufferedReader().use { it.readText() }.trim()
    if (process.waitFor() == 0 && output.isNotEmpty()) output else "nogit"
} catch (_: Exception) {
    "nogit"
}

val stampedVersionName: String = run {
    val base = "0.1.0"
    val date = SimpleDateFormat("yyMMdd", Locale.US).format(Date())
    "v$base-${gitShortHash(rootProject.projectDir)}-$date"
}

android {
    namespace = "org.dpdns.clevebitr"
    compileSdk = 36

    defaultConfig {
        applicationId = "org.dpdns.clevebitr"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        // 形如 v0.1.0-7a7579-260922
        versionName = stampedVersionName

        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            // 自用工程：release 也用 debug 签名。不加这一行 `assembleRelease` 出的是
            // 未签名 APK，装不上——而"引擎性能"这类问题必须在 release 引擎上量：
            // debug 构建的 -O0 与渲染探针会把数字带偏几倍。真要对外发布时换成自己的
            // keystore。
            signingConfig = signingConfigs.getByName("debug")
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
    implementation(platform("androidx.compose:compose-bom:2026.06.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    // @Preview 注解（编译期需要；渲染由下面的 debugImplementation ui-tooling 提供）
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    // ── 导航 ────────────────────────────────────────────────────────────────
    implementation("androidx.navigation:navigation-compose:2.9.8")

    // ── 图片加载（封面）与网络 ──────────────────────────────────────────────
    implementation("io.coil-kt.coil3:coil-compose:3.5.0")
    implementation("io.coil-kt.coil3:coil-network-okhttp:3.5.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
