package org.dpdns.clevebitr.ui.preview

import org.dpdns.clevebitr.core.EngineOverride
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GameMetadata
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.OverlayConfig
import org.dpdns.clevebitr.core.RunMode

/*
 * ═══════════════════════════════════════════════════════════════════════════
 * UI 预览层（src/debug/kotlin，**只属于 debug 变体**）
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * 这里放"只在 Android Studio 预览器里跑"的代码，用来改 UI 布局时即时看效果。
 * 放在 src/debug 而不是 src/main，是为了让预览**完全不参与 release 构建**：
 * release 变体根本不会编译这个目录，预览里写错字段、引用了已删除的组件，
 * 都不会挡住 assembleRelease。反过来也成立：src/main 里不要再写 @Preview，
 * 否则 release 又要把 ui-tooling 拖进来。
 *
 * 约定：
 * - 一个页面一个 `*Previews.kt`，文件里只有 `@Preview` 函数，不写业务逻辑；
 * - 假数据统一放本文件，别每页各造一份——改一个字段时只改一处；
 * - `coversDir` 一律指向不存在的目录：`CoverImage` 会退化成占位封面，
 *   预览因此既不依赖真实文件，也不联网；
 * - 需要看深色/平板时，直接叠第二个 `@Preview`，别改主源码。
 *
 * 构建命令（无 wrapper 脚本时用本机 gradle 发行版）：
 *   gradle :app:compileDebugKotlin      # 预览层参与编译，改预览后跑这个
 *   gradle :app:compileReleaseKotlin    # 预览层不参与，用来确认没拖累正式构建
 */

/** 预览用封面目录：故意不存在，`CoverImage` 会画占位封面。 */
internal const val PREVIEW_COVERS_DIR = "/tmp/preview-covers"

/** 预览用的日志目录：设置页只在标题里显示这个字符串，不会真去读。 */
internal const val PREVIEW_LOG_DIR = "/data/user/0/org.dpdns.clevebitr/files/logs"

/**
 * 一条"刮削过"的库记录：带厂商/发售日/标签/长简介/备注/收藏/分组。
 * 详情页预览的主数据，也是库页预览里的第一条。
 */
internal val previewGame = LibraryGame(
    id = "preview",
    path = "/storage/emulated/0/Games/サンプルゲーム",
    title = "サンプルゲーム -Sample Game-",
    developer = "Preview Works / 汉化组",
    vndbId = "v12345",
    released = "2019-03-29",
    tags = listOf("学园", "恋爱", "ADV", "汉化", "纯爱", "多结局"),
    description = "这是一个用于 Compose 预览的示例简介。" +
        "它故意写得长一些，好让折叠/展开简介的分支都能看到效果。" +
        "正文超过五行后默认收起，点击「展开简介」可以看全文。" +
        "后面的内容纯粹是为了凑长度，随便念一段：黄昏的教室里只剩下两个人的呼吸声，" +
        "窗外的蝉鸣把时间拉得很长，谁都没有先开口。",
    coverFile = "", // 留空 → 走占位封面，避免预览时依赖真实文件与 Coil
    notes = "v1.02 汉化版，启动正常。",
    favorite = true,
    group = "待玩",
    addedAt = 1_700_000_000_000L,
    lastPlayedAt = 1_725_000_000_000L,
    playCount = 3,
)

/**
 * 库页预览的数据：故意覆盖四种情况——**超长标题 + 有封面文件、未分组、没刮削过、
 * 收藏置顶**。改库页卡片布局时，这四种就是最容易崩的边界。
 */
internal val previewGames = listOf(
    previewGame,
    LibraryGame(
        id = "preview-2",
        path = "/storage/emulated/0/Games/NEKOPARA Vol.4",
        title = "NEKOPARA Vol.4 ～ネコとパティシエのノエル～ 完全版",
        developer = "NEKO WORKs",
        released = "2020-11-26",
        tags = listOf("猫娘", "纯爱", "喜剧"),
        // 只写文件名不写真实文件：Coil 加载失败会走占位封面，预览仍然不联网
        coverFile = "preview-2.jpg",
        favorite = false,
        group = "",
        addedAt = 1_710_000_000_000L,
        lastPlayedAt = 1_726_000_000_000L,
        playCount = 12,
    ),
    LibraryGame(
        id = "preview-3",
        path = "/storage/emulated/0/Games/千恋万花",
        title = "千恋万花",
        released = "2016-07-29",
        group = "已通关",
        addedAt = 1_690_000_000_000L,
    ),
)

/** 详情页预览的每游戏配置：运行模式/帧率/叠加层都"独立配置"，好看到覆盖态的样子。 */
internal val previewConfig = GameConfig(
    engine = EngineOverride(
        runMode = RunMode.GPU.key,
        compatProfile = RunMode.GPU.compatProfile,
        oglDrawDeviceCompat = RunMode.GPU.oglDrawDeviceCompat,
        fpsLimit = 30,
    ),
    metadata = GameMetadata(
        title = previewGame.title,
        developer = previewGame.developer,
        vndbId = previewGame.vndbId,
        released = previewGame.released,
        tags = previewGame.tags,
        description = previewGame.description,
    ),
    notes = previewGame.notes,
)

/** 全局默认：设置页/游戏设置页预览里的"继承全局"文案就来自它。 */
internal val previewGlobalDefaults = GlobalDefaults(
    compatProfile = RunMode.AUTO.compatProfile,
    oglDrawDeviceCompat = RunMode.AUTO.oglDrawDeviceCompat,
    fpsLimit = 60,
    fontFallbackMode = "auto",
    overlay = OverlayConfig(),
)
