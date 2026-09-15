package org.dpdns.clevebitr.ui

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
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import java.io.File

private const val TAG = "KrKr2Next/Launcher"

/** 内置的目录浏览器。选定一个目录即作为游戏根目录启动。 */
@Composable
fun LauncherScreen(
    onLaunchGame: (String) -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var hasPermission by remember { mutableStateOf(hasStoragePermission(context)) }

    // 全文件访问（API 30+）走系统设置页；老版本走运行时权限
    val settingsLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { hasPermission = hasStoragePermission(context) }

    // 游戏目录读取要 READ，日志写 Android/media/<包名> 还要 WRITE（API 24-29 上
    // 那个目录并不免权限，只有 API 30+ 才免）。两者同属存储权限组，一次申请即可。
    val legacyPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        hasPermission = hasStoragePermission(context)
        if (!granted.values.all { it }) {
            // 只读被拒不影响启动游戏（引擎仍能读目录），只是日志会落到应用私有目录
            AppLog.w(TAG, "storage permissions not fully granted: $granted")
        }
    }

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
                    legacyPermissionLauncher.launch(
                        arrayOf(
                            Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        ),
                    )
                }
            },
        )
        return
    }

    DirectoryBrowser(
        onLaunchGame = onLaunchGame,
        onOpenSettings = onOpenSettings,
        modifier = modifier,
    )
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
private fun DirectoryBrowser(
    onLaunchGame: (String) -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier,
) {
    val context = LocalContext.current
    val storageRoot = remember { Environment.getExternalStorageDirectory().absolutePath }

    // 上次浏览到的目录优先，但它可能已经被删掉/卸载了存储，所以必须复核
    var currentPath by remember {
        mutableStateOf(AppPrefs.lastDir(context)?.takeIf { File(it).isDirectory } ?: storageRoot)
    }
    var showJump by remember { mutableStateOf(false) }

    LaunchedEffect(currentPath) { AppPrefs.setLastDir(context, currentPath) }

    val currentDir = remember(currentPath) { File(currentPath) }
    val parentPath = remember(currentPath) { currentDir.parentFile?.absolutePath }
    val subDirs = remember(currentPath) {
        currentDir.listFiles()
            ?.filter { it.isDirectory && !it.name.startsWith('.') }
            ?.sortedBy { it.name.lowercase() }
            ?: emptyList()
    }
    val looksLikeGame = remember(currentPath) { detectGameRoot(currentDir) }

    Column(modifier = modifier.fillMaxSize()) {
        // ── 路径与导航 ──
        // 显示完整绝对路径（而不是相对存储根目录的那一段）：出问题时用户要能
        // 原样把路径念出来，也要能直接跳过去。
        Row(
            modifier = Modifier.fillMaxWidth().padding(start = 4.dp, end = 4.dp, top = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TextButton(
                enabled = parentPath != null,
                onClick = { parentPath?.let { currentPath = it } },
            ) {
                Icon(Icons.Filled.ArrowBack, contentDescription = null)
                Text("上一级", modifier = Modifier.padding(start = 4.dp))
            }

            Column(
                modifier = Modifier
                    .weight(1f)
                    .clickable { showJump = true }
                    .padding(horizontal = 4.dp),
            ) {
                Text(
                    text = currentPath,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = "点此跳转到其他目录",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            IconButton(onClick = onOpenSettings) {
                Icon(Icons.Filled.Settings, contentDescription = "设置")
            }
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
            if (subDirs.isEmpty()) {
                item(key = "__empty__") {
                    Text(
                        text = "此目录下没有子目录。",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                    )
                }
            }

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

    if (showJump) {
        PathJumpDialog(
            initialPath = currentPath,
            rootPath = storageRoot,
            onDismiss = { showJump = false },
            onJump = { target ->
                AppLog.i(TAG, "jump to $target")
                currentPath = target
            },
        )
    }
}

/**
 * 手动跳转。存在的意义是**回到存储根目录之外**：逐级返回只能向上走，一旦用户在
 * 深层目录里迷路，或者要去的路径不在当前这棵子树里，就只能靠输入。
 */
@Composable
private fun PathJumpDialog(
    initialPath: String,
    rootPath: String,
    onDismiss: () -> Unit,
    onJump: (String) -> Unit,
) {
    var text by remember { mutableStateOf(initialPath) }
    var error by remember { mutableStateOf<String?>(null) }

    fun submit(path: String) {
        val trimmed = path.trim()
        if (trimmed.isEmpty()) {
            error = "路径不能为空"
            return
        }
        val dir = File(trimmed)
        if (!dir.isDirectory) {
            error = "不是可读目录：$trimmed"
            return
        }
        onJump(dir.absolutePath)
        onDismiss()
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("跳转到目录") },
        text = {
            Column {
                OutlinedTextField(
                    value = text,
                    onValueChange = {
                        text = it
                        error = null
                    },
                    label = { Text("绝对路径") },
                    singleLine = false,
                    maxLines = 3,
                    isError = error != null,
                    modifier = Modifier.fillMaxWidth(),
                )
                error?.let {
                    Text(
                        text = it,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
            }
        },
        confirmButton = { TextButton(onClick = { submit(text) }) { Text("跳转") } },
        dismissButton = {
            Row {
                TextButton(onClick = { submit(rootPath) }) { Text("存储根目录") }
                TextButton(onClick = onDismiss) { Text("取消") }
            }
        },
    )
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
