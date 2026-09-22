package org.dpdns.clevebitr.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.automirrored.filled.Backspace
import androidx.compose.material.icons.automirrored.filled.Chat
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.automirrored.filled.KeyboardReturn
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.automirrored.filled.VolumeDown
import androidx.compose.material.icons.automirrored.filled.VolumeOff
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.DoubleArrow
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.FastRewind
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Fullscreen
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Keyboard
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.KeyboardCapslock
import androidx.compose.material.icons.filled.KeyboardCommandKey
import androidx.compose.material.icons.filled.KeyboardOptionKey
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.Mouse
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.PlayCircle
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.SpaceBar
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Tab
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material.icons.filled.ZoomIn
import androidx.compose.material.icons.filled.ZoomOut
import androidx.compose.ui.graphics.vector.ImageVector

/**
 * 自定义按键可用的 MD3 图标登记表。
 *
 * **落盘的是这里的字符串键，不是 `ImageVector` 的名字**：Compose 的图标属性名
 * 随依赖版本变动，把它写进配置文件会在升级后静默失效。键名一旦发布不能改
 * （与 [org.dpdns.clevebitr.core.KeyPadConfig] 的落盘纪律一致）。
 */
object KeyPadIcons {

    /** `null` = 不显示图标（只显示文字）。 */
    val NONE: String? = null

    /** 键名 → 图标。顺序就是选择器里的展示顺序（常用在前）。 */
    val registry: List<Pair<String, ImageVector>> = listOf(
        "arrowLeft" to Icons.AutoMirrored.Filled.ArrowBack,
        "arrowRight" to Icons.AutoMirrored.Filled.ArrowForward,
        "arrowUp" to Icons.Filled.ArrowUpward,
        "arrowDown" to Icons.Filled.ArrowDownward,
        "keyboardUp" to Icons.Filled.KeyboardArrowUp,
        "keyboardDown" to Icons.Filled.KeyboardArrowDown,
        "enter" to Icons.AutoMirrored.Filled.KeyboardReturn,
        "space" to Icons.Filled.SpaceBar,
        "backspace" to Icons.AutoMirrored.Filled.Backspace,
        "tab" to Icons.Filled.Tab,
        "shift" to Icons.Filled.KeyboardCapslock,
        "ctrl" to Icons.Filled.KeyboardCommandKey,
        "alt" to Icons.Filled.KeyboardOptionKey,
        "play" to Icons.Filled.PlayArrow,
        "pause" to Icons.Filled.Pause,
        "next" to Icons.Filled.SkipNext,
        "previous" to Icons.Filled.SkipPrevious,
        "fastForward" to Icons.Filled.FastForward,
        "rewind" to Icons.Filled.FastRewind,
        "volumeUp" to Icons.AutoMirrored.Filled.VolumeUp,
        "volumeDown" to Icons.AutoMirrored.Filled.VolumeDown,
        "volumeOff" to Icons.AutoMirrored.Filled.VolumeOff,
        "speed" to Icons.Filled.Speed,
        "skip" to Icons.Filled.DoubleArrow,
        "mouse" to Icons.Filled.Mouse,
        "touch" to Icons.Filled.TouchApp,
        "keyboard" to Icons.Filled.Keyboard,
        "star" to Icons.Filled.Star,
        "favorite" to Icons.Filled.Favorite,
        "check" to Icons.Filled.Check,
        "close" to Icons.Filled.Close,
        "menu" to Icons.Filled.Menu,
        "settings" to Icons.Filled.Settings,
        "fullscreen" to Icons.Filled.Fullscreen,
        "zoomIn" to Icons.Filled.ZoomIn,
        "zoomOut" to Icons.Filled.ZoomOut,
        "refresh" to Icons.Filled.Refresh,
        "save" to Icons.Filled.Save,
        "undo" to Icons.AutoMirrored.Filled.Undo,
        "redo" to Icons.AutoMirrored.Filled.Redo,
        "chat" to Icons.AutoMirrored.Filled.Chat,
        "send" to Icons.AutoMirrored.Filled.Send,
        "home" to Icons.Filled.Home,
        "search" to Icons.Filled.Search,
        "info" to Icons.Filled.Info,
        "help" to Icons.AutoMirrored.Filled.Help,
        "edit" to Icons.Filled.Edit,
        "add" to Icons.Filled.Add,
        "remove" to Icons.Filled.Remove,
        "delete" to Icons.Filled.Delete,
        "playCircle" to Icons.Filled.PlayCircle,
    )

    private val byKey: Map<String, ImageVector> = registry.toMap()

    /** 键名查图标；未知键返回 null（旧配置里的键被删掉时按"无图标"处理）。 */
    fun resolve(key: String?): ImageVector? = key?.let { byKey[it] }

    /** 供选择器展示的键名列表（含"无"）。 */
    val selectableKeys: List<String?> = listOf(NONE) + registry.map { it.first }
}
