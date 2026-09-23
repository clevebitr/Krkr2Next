package org.dpdns.clevebitr.ui

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.BuildInfo

private const val TAG = "AboutScreen"

private const val REPO_URL = "https://github.com/clevebitr/Krkr2Next"
private const val KIRIKIROID2_URL = "https://github.com/zeas2/Kirikiroid2"
private const val KRKRZ_URL = "https://github.com/krkrz/krkrz"
private const val AETHERKIRI_URL = "https://github.com/AetherKiri/AetherKiri"

/**
 * 关于页
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AboutScreen(onBack: () -> Unit, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val version = BuildInfo.appVersion(context)

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("关于") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
            )
        },
    ) { scaffoldPadding ->
        Column(
            modifier = Modifier
                .padding(scaffoldPadding)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
        ) {
            AboutSection("应用") {
                AboutItem("名称", "KrKr2Next")
                AboutItem("版本", version)
                AboutItem("作者", "clevebitr")
            }

            AboutSection("运行环境") {
                AboutItem("包名", context.packageName)
                AboutItem(
                    "系统",
                    "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})",
                )
                AboutItem("ABI", Build.SUPPORTED_ABIS.joinToString())
                AboutItem("机型", "${Build.MANUFACTURER} ${Build.MODEL}")
            }

            AboutSection("技术栈") {
                AboutItem("壳", "Kotlin + Jetpack Compose（Material 3）")
                AboutItem("引擎", "C++17 / TVP(KiriKiri2) 核心：TJS2 脚本、存储与归档、GLES3 渲染")
                AboutItem("渲染", "原生 EGL + OpenGL ES 3（不使用 ANGLE）")
                AboutItem("插件", "PSB / PSD / motionplayer(E-mote) / LayerEx / KAGParserEx / Cubism(可选)")
                AboutItem("媒体", "ffmpeg（影片与音频解码）")
            }

            AboutSection("相关链接") {
                AboutLink("项目仓库", REPO_URL)
                AboutLink("参考实现：Kirikiroid2", KIRIKIROID2_URL)
                AboutLink("参考实现：krkrz（吉里吉里Z）", KRKRZ_URL)
                AboutLink("参考实现：AetherKiri", AETHERKIRI_URL)
            }

            AboutSection("开源协议与致谢") {
                AboutItem(
                    "原版引擎",
                    "KiriKiri2 / TVP 由 W.Dee 及贡献者开发，按原项目许可发布。",
                )
                AboutItem(
                    "Kirikiroid2",
                    "Android 移植路线的重要参考（存储、插件桩与运行环境设计）。",
                )
                AboutItem(
                    "AetherKiri / krkrz",
                    "krkrz 兼容层、插件模拟与脚本层行为对齐的参考实现。",
                )
                AboutItem(
                    "第三方库",
                    "spdlog、fmt、boost、zlib、libpng、libjpeg-turbo、libjxrlib、freetype、" +
                        "SDL2、libarchive、minizip、unrar、uchardet、zstd、oniguruma、sqlite、" +
                        "ffmpeg —— 各自遵循其原许可。",
                )
                AboutItem(
                    "Live2D Cubism SDK",
                    "可选依赖：未随本仓库分发，缺失时自动禁用相关功能。",
                )
            }

            AboutSection("说明") {
                AboutItem(
                    "本应用",
                    "面向 Android 的 KiriKiri2 运行环境，用于运行用户自行准备的作品；" +
                        "不附带任何游戏内容。",
                )
            }
        }
    }
}

@Composable
private fun AboutSection(title: String, content: @Composable () -> Unit) {
    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(vertical = 6.dp),
        )
        content()
        HorizontalDivider(modifier = Modifier.padding(top = 8.dp))
    }
}

@Composable
private fun AboutItem(label: String, value: String) {
    Column(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        Text(text = label, style = MaterialTheme.typography.labelLarge)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            overflow = TextOverflow.Clip,
        )
    }
}

@Composable
private fun AboutLink(label: String, url: String) {
    val context = LocalContext.current
    OutlinedButton(
        onClick = { openInBrowser(context, url) },
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
    ) {
        Icon(Icons.AutoMirrored.Filled.OpenInNew, contentDescription = null)
        Text(text = label, modifier = Modifier.padding(start = 8.dp))
    }
}

private fun openInBrowser(context: Context, url: String) {
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
    } catch (e: ActivityNotFoundException) {
        // 精简 ROM 或把浏览器停用过的机器上真会这样：没有就提示一句，
        // 不处理的话点一下按钮直接崩。
        AppLog.w(TAG, "没有可用的浏览器：$url", e)
        Toast.makeText(context, "没有可用的浏览器", Toast.LENGTH_SHORT).show()
    }
}
