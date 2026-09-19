package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import org.dpdns.clevebitr.core.BuildInfo

/**
 * 关于页：作者、仓库、技术栈、开源协议与版本号。
 *
 * 为什么单开一页而不是塞进设置：这些是**只读信息**，与"改一项就生效"的设置项放在一起
 * 会让人以为它们也能调；单独一页也方便截图/分享（排查问题时对方第一句通常就是"哪个版本"）。
 */
@Composable
fun AboutScreen(onBack: () -> Unit, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val version = BuildInfo.appVersion(context)

    Column(modifier = modifier.fillMaxSize()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.Filled.ArrowBack, contentDescription = "返回")
            }
            Text(text = "关于", style = MaterialTheme.typography.titleLarge)
        }

        Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
        ) {
            AboutSection("应用") {
                AboutItem("名称", "KrKr2Next")
                AboutItem("版本", version)
                AboutItem("作者", "clevebitr")
                AboutItem("仓库", REPO_URL)
            }

            AboutSection("技术栈") {
                AboutItem("壳", "Kotlin + Jetpack Compose（Material 3）")
                AboutItem("引擎", "C++17 / TVP(KiriKiri2) 核心：TJS2 脚本、存储与归档、GLES3 渲染")
                AboutItem("渲染", "原生 EGL + OpenGL ES 3（不使用 ANGLE）")
                AboutItem("插件", "PSB / PSD / motionplayer(E-mote) / LayerEx / KAGParserEx / Cubism(可选)")
                AboutItem("媒体", "ffmpeg（影片与音频解码）")
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

private const val REPO_URL = "https://github.com/clevebitr/Krkr2Next"

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
