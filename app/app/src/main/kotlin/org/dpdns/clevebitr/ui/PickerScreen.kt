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
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.EntryVerdict
import org.dpdns.clevebitr.core.GameEntry
import org.dpdns.clevebitr.core.GameScanner

private const val TAG = "KrKr2Next/Picker"

/**
 * 「添加游戏」用的目录浏览器。
 *
 * 与旧启动器（`LauncherScreen`）的差别：**它不再是主入口**，而是库里那个"+"的下游。
 * 因此它多做了三件事：每个子目录可以直接一键入库、当前目录可以"扫描整棵树"、
 * 已经在库里的目录会标出来（避免用户重复添加后才发现列表里有一条一模一样的）。
 *
 * 保留旧启动器的两条关键设计：入口判定用 [GameEntry]（引擎只在选中目录本层找
 * `startup.tjs` / `.xp3`，选错一层就只是"打不开"），以及手动跳转对话框（逐级返回
 * 只能向上，用户在深层目录迷路时要能直接输路径）。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PickerScreen(
    inLibrary: (File) -> Boolean,
    onAddToLibrary: (File) -> Boolean,
    onScanFinished: (added: Int, skipped: Int) -> Unit,
    onLaunchPath: (File) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var hasPermission by remember { mutableStateOf(hasStoragePermission(context)) }

    val settingsLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { hasPermission = hasStoragePermission(context) }

    val legacyPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        hasPermission = hasStoragePermission(context)
        if (!granted.values.all { it }) {
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

    BrowserBody(
        inLibrary = inLibrary,
        onAddToLibrary = onAddToLibrary,
        onScanFinished = onScanFinished,
        onLaunchPath = onLaunchPath,
        onBack = onBack,
        modifier = modifier,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BrowserBody(
    inLibrary: (File) -> Boolean,
    onAddToLibrary: (File) -> Boolean,
    onScanFinished: (added: Int, skipped: Int) -> Unit,
    onLaunchPath: (File) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier,
) {
    val context = LocalContext.current
    val storageRoot = remember { Environment.getExternalStorageDirectory().absolutePath }
    val scope = rememberCoroutineScope()

    var currentPath by remember {
        mutableStateOf(AppPrefs.lastDir(context)?.takeIf { File(it).isDirectory } ?: storageRoot)
    }
    var showJump by remember { mutableStateOf(false) }

    // 扫描状态。scanned/cancelRequested 用 remember 而不是 State：它们只在协程里读写，
    // 每改一次都触发重组没有必要；进度显示只依赖 scannedDirs。
    var scanning by remember { mutableStateOf(false) }
    var scannedDirs by remember { mutableStateOf(0) }
    var scanCurrent by remember { mutableStateOf("") }
    var lastResult by remember { mutableStateOf<GameScanner.Result?>(null) }
    var addedCount by remember { mutableStateOf(0) }
    var skippedCount by remember { mutableStateOf(0) }
    val cancelFlag = remember { java.util.concurrent.atomic.AtomicBoolean(false) }

    LaunchedEffect(currentPath) { AppPrefs.setLastDir(context, currentPath) }

    val currentDir = remember(currentPath) { File(currentPath) }
    val parentPath = remember(currentPath) { currentDir.parentFile?.absolutePath }
    val subDirs = remember(currentPath) {
        currentDir.listFiles()
            ?.filter { it.isDirectory && !it.name.startsWith('.') }
            ?.sortedBy { it.name.lowercase() }
            ?: emptyList()
    }
    val verdict = remember(currentPath) { GameEntry.inspect(currentDir) }
    val childVerdicts = remember(currentPath) { subDirs.associateWith { GameEntry.inspect(it) } }
    val childInLibrary = remember(currentPath) { subDirs.associateWith { inLibrary(it) } }

    Scaffold(
        modifier = modifier,
        // 只留**一个**顶栏：返回 / 可点路径（跳转）/ 上一级 / 已在库标记。
        // 旧实现是"TopAppBar 写标题 + 内容区再插一行路径"，两行都在讲位置，信息重复又占高度。
        topBar = {
            TopAppBar(
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
                title = {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { showJump = true }
                            .padding(vertical = 4.dp),
                    ) {
                        Text(
                            text = currentDir.name.ifEmpty { "存储根目录" },
                            style = MaterialTheme.typography.titleMedium,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            text = currentPath,
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                },
                actions = {
                    if (inLibrary(currentDir)) {
                        Icon(Icons.Filled.Check, contentDescription = "已在库中")
                    }
                    IconButton(
                        enabled = parentPath != null,
                        onClick = { parentPath?.let { currentPath = it } },
                    ) {
                        Icon(Icons.Filled.ArrowUpward, contentDescription = "上一级")
                    }
                },
            )
        },
        bottomBar = {
            Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { onAddToLibrary(currentDir) },
                        modifier = Modifier.weight(1f),
                    ) {
                        Icon(Icons.Filled.Add, contentDescription = null)
                        Text(
                            text = if (verdict is EntryVerdict.Entry) "加入库" else "仍要加入",
                            modifier = Modifier.padding(start = 6.dp),
                        )
                    }
                    OutlinedButton(
                        onClick = { onLaunchPath(currentDir) },
                        modifier = Modifier.weight(1f),
                    ) {
                        Icon(Icons.Filled.PlayArrow, contentDescription = null)
                        Text("直接启动", modifier = Modifier.padding(start = 6.dp))
                    }
                }
                OutlinedButton(
                    onClick = {
                        if (scanning) return@OutlinedButton
                        scanning = true
                        scannedDirs = 0
                        scanCurrent = ""
                        cancelFlag.set(false)
                        addedCount = 0
                        skippedCount = 0
                        scope.launch {
                            val root = File(currentPath)
                            var added = 0
                            var skipped = 0
                            // 目录遍历是 IO：放在 IO 线程，UI 只收进度
                            val result = withContext(Dispatchers.IO) {
                                GameScanner.scan(
                                    root = root,
                                    shouldCancel = { cancelFlag.get() },
                                    onProgress = { dir ->
                                        scannedDirs += 1
                                        // 每 8 个目录更新一次界面文字：再密只会让
                                        // 组合跟不上，进度看起来反而卡
                                        if (scannedDirs % 8 == 0) scanCurrent = dir.name
                                    },
                                )
                            }
                            // 统计以**库的答复**为准（`add` 会去重），不用扫描前抓到的
                            // 快照判断：一次扫几十个目录时快照必然是旧的
                            for (entry in result.found) {
                                if (onAddToLibrary(entry)) added += 1 else skipped += 1
                            }
                            scanning = false
                            lastResult = result
                            addedCount = added
                            skippedCount = skipped
                            onScanFinished(added, skipped)
                        }
                    },
                    enabled = !scanning,
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                ) {
                    Icon(Icons.Filled.Search, contentDescription = null)
                    Text(
                        text = if (scanning) "正在扫描…" else "扫描此目录下所有游戏",
                        modifier = Modifier.padding(start = 6.dp),
                    )
                }
            }
        },
    ) { padding ->
        Column(modifier = Modifier.padding(padding).fillMaxSize()) {
            GameEntry.hint(verdict)?.let { text ->
                Text(
                    text = text,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }

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
                    val badge = GameEntry.badge(childVerdicts[dir] ?: EntryVerdict.None)
                    val already = childInLibrary[dir] == true
                    ListItem(
                        headlineContent = {
                            Text(dir.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        },
                        supportingContent = when {
                            already -> {
                                { Text("已在库中") }
                            }

                            badge != null -> {
                                { Text(badge) }
                            }

                            else -> null
                        },
                        leadingContent = { Icon(Icons.Filled.Folder, contentDescription = null) },
                        trailingContent = {
                            if (!already) {
                                IconButton(onClick = { onAddToLibrary(dir) }) {
                                    Icon(Icons.Filled.Add, contentDescription = "加入库")
                                }
                            }
                        },
                        modifier = Modifier.clickable { currentPath = dir.absolutePath },
                    )
                }
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

    if (scanning) {
        AlertDialog(
            onDismissRequest = { /* 扫描中不允许点外部关闭：取消要走取消按钮 */ },
            title = { Text("正在扫描") },
            text = {
                Column {
                    Text("已看 $scannedDirs 个目录")
                    if (scanCurrent.isNotBlank()) {
                        Text(
                            text = scanCurrent,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    LinearProgressIndicator(
                        modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = { cancelFlag.set(true) }) { Text("停止") }
            },
        )
    }

    lastResult?.let { result ->
        AlertDialog(
            onDismissRequest = { lastResult = null },
            title = { Text(if (result.cancelled) "扫描已停止" else "扫描完成") },
            text = {
                Column {
                    Text("看过 ${result.scanned} 个目录，找到 ${result.found.size} 个入口。")
                    Text("新加入 $addedCount 个，已在库中 $skippedCount 个。")
                    if (result.truncated) {
                        Text(
                            text = "达到单次上限（${GameScanner.DEFAULT_LIMIT} 个），" +
                                "剩余的请换子目录再扫一次。",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { lastResult = null }) { Text("好") }
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

internal fun hasStoragePermission(context: Context): Boolean =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        Environment.isExternalStorageManager()
    } else {
        ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.READ_EXTERNAL_STORAGE,
        ) == PackageManager.PERMISSION_GRANTED
    }
