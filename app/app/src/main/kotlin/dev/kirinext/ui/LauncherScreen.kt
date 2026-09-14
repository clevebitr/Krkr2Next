package dev.kirinext.ui

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import java.io.File

/** 内置的极简目录浏览器。选定一个目录即作为游戏根目录启动。 */
@Composable
fun LauncherScreen(
    onLaunchGame: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var hasPermission by remember { mutableStateOf(hasStoragePermission(context)) }

    // 全文件访问（API 30+）走系统设置页；老版本走运行时权限
    val settingsLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { hasPermission = hasStoragePermission(context) }

    val legacyPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted -> hasPermission = granted }

    if (!hasPermission) {
        PermissionRequest(
            modifier = modifier,
            onRequest = {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    settingsLauncher.launch(
                        Intent(
                            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:${context.packageName}"),
                        ),
                    )
                } else {
                    legacyPermissionLauncher.launch(Manifest.permission.READ_EXTERNAL_STORAGE)
                }
            },
        )
        return
    }

    DirectoryBrowser(onLaunchGame = onLaunchGame, modifier = modifier)
}

@Composable
private fun PermissionRequest(modifier: Modifier, onRequest: () -> Unit) {
    Column(
        modifier = modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "需要存储访问权限",
            style = MaterialTheme.typography.titleLarge,
        )
        Text(
            text = "引擎需要直接读取游戏的目录路径，因此需要完整的文件访问权限。",
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(top = 8.dp, bottom = 24.dp),
        )
        Button(onClick = onRequest) {
            Text("授予权限")
        }
    }
}

@Composable
private fun DirectoryBrowser(onLaunchGame: (String) -> Unit, modifier: Modifier) {
    val storageRoot = remember { Environment.getExternalStorageDirectory().absolutePath }
    var currentPath by remember { mutableStateOf(storageRoot) }

    val currentDir = remember(currentPath) { File(currentPath) }
    val subDirs = remember(currentPath) {
        currentDir.listFiles()
            ?.filter { it.isDirectory && !it.name.startsWith('.') }
            ?.sortedBy { it.name.lowercase() }
            ?: emptyList()
    }
    val looksLikeGame = remember(currentPath) { detectGameRoot(currentDir) }

    Column(modifier = modifier.fillMaxSize()) {
        // ── 路径与导航 ──
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(
                enabled = currentPath != storageRoot,
                onClick = { currentDir.parentFile?.let { currentPath = it.absolutePath } },
            ) {
                Icon(Icons.Filled.ArrowBack, contentDescription = "上一级")
            }
            Text(
                text = currentPath.removePrefix(storageRoot).ifEmpty { "/" },
                style = MaterialTheme.typography.bodyMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
            )
        }

        // ── 启动按钮 ──
        Button(
            onClick = { onLaunchGame(currentPath) },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            Icon(Icons.Filled.PlayArrow, contentDescription = null)
            Text(
                text = if (looksLikeGame) "启动此目录的游戏" else "启动此目录",
                modifier = Modifier.padding(start = 8.dp),
            )
        }

        if (!looksLikeGame) {
            Text(
                text = "未在此目录发现 .xp3 或 startup.tjs —— 若游戏在上层目录，请先返回。",
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }

        // ── 子目录 ──
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(subDirs, key = { it.absolutePath }) { dir ->
                ListItem(
                    headlineContent = {
                        Text(dir.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    },
                    supportingContent = if (detectGameRoot(dir)) {
                        { Text("可能是游戏目录") }
                    } else {
                        null
                    },
                    leadingContent = { Icon(Icons.Filled.Folder, contentDescription = null) },
                    modifier = Modifier.clickable { currentPath = dir.absolutePath },
                )
            }
        }
    }
}

/** 目录里是否有 KiriKiri 游戏特征文件。 */
private fun detectGameRoot(dir: File): Boolean {
    val children = dir.list() ?: return false
    return children.any { name ->
        val lower = name.lowercase()
        lower.endsWith(".xp3") || lower == "startup.tjs"
    }
}

private fun hasStoragePermission(context: Context): Boolean =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        Environment.isExternalStorageManager()
    } else {
        ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.READ_EXTERNAL_STORAGE,
        ) == PackageManager.PERMISSION_GRANTED
    }
