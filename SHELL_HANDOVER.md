# 壳功能交接文档（KrKr2Next / KiriNext — app/）

> 用途：把**壳（Kotlin/Compose）还没做完的功能**交给下一个上下文。读这份之前先读
> 根目录 `AGENTS.md`（项目级规则）与 `HANDOFF.md`（引擎/兼容层现状）。本文件只写
> **壳侧**的现状、目标行为、落点、约束与验收标准，不写推理过程。

---

## 0. 一句话现状

壳的原有九项改造已全部落地（见 `HANDOFF.md §3`）。2026-09-22 新增：

- **详情页改版（Steam 大屏式左右分栏）→ 已完成**（`ui/GameDetailScreen.kt`）。
- **壳的 Kotlin 编译已能在 Termux 本地跑通** → 见 §1，改壳必须先本地编译过再推。

2026-09-23 新增（均已真机确认）：

- **自定义按键浮层**（§3）、**引擎菜单侧边栏**（§4）→ 已完成；
- **光标触控板模式**（`ui/Touchpad.kt`，每游戏可覆盖 + 悬浮菜单可切）→ 已完成；
- **按键自动对齐参考线**（`ui/KeyPadOverlay.kt` 的 `snapPosition`）→ 已完成；
- **右下角宽矩形按钮抽屉** + **「更多」菜单收窄**（`ui/GameScreen.kt`）→ 已完成；
- **退出/强制退出（error 色 + 二次确认）** + **无响应看门狗 / 进程重启**（`HANDOFF.md §1.8`）→ 已完成；
- **详情页右侧改为 标签 → 简介（可折叠）；主按钮另起一行全宽横排**（`ui/GameDetailScreen.kt`）→ 已完成；
- **触控板光标不显示** → 已修（`HANDOFF.md §1.9.1`）。

**2026-09-23 第三批（已落地，待真机回归）**：

- **加载期自动显示日志浮层**（§7）→ 已完成。`AppPrefs` 新键 `debug.auto_log_on_launch`
  （缺省 false）+ 每游戏覆盖 `GameConfig.autoLogOnLaunch`；`GameScreen` 新增参数
  `autoShowLogs`，`startup state 2` 时自动关。

**壳侧已无未完成项**（`HANDOFF.md §3` 的清单全部落地）。

---

## 1. 本地开发闭环（改壳必读）

```bash
bash scripts/build_shell_local.sh          # 只编译 Kotlin（类型检查，最快）
bash scripts/build_shell_local.sh --assemble   # 出 debug APK（需 jniLibs 有引擎库）
bash scripts/check_static.sh               # JNI 符号 / 移植清单 / 语法
git diff --check
```

`build_shell_local.sh` 的两个必要开关（写死在脚本里，别改成用系统 gradle）：

1. **Gradle 必须 8.x**：AGP 8.13 依赖 Gradle 9.6 移除的内部 API，系统自带的是 9.7.x，
   会在配置期就失败。脚本自动找 `~/.gradle/wrapper/dists/gradle-8.14.5-bin/*/…`
   （与 CI 同版本，已缓存）。
2. **aapt2 必须换成 termux 原生**：AGP 自带的 aapt2 是 linux-x86_64，在 arm64 上
   `AarResourcesCompilerTransform: Daemon startup failed`。用
   `-Pandroid.aapt2FromMavenOverride=$(command -v aapt2)` 覆盖。

依赖都在 `~/.gradle/caches`（离线可编译）。**本地 jniLibs 没有 `libengine_api.so`**，
所以 `--assemble` 出的 APK 不含引擎、只能看 UI；要真机跑游戏仍然用 CI 产物。

> 引擎侧（`cpp/`）改动**不能**用这条链路验证——`cpp/` 依赖 vcpkg 三方库，只能靠 CI。
> 但 `cpp/core/visual/LayerIntf.cpp` 这类可以通过 `clang++ -fsyntax-only` + 最小
> spdlog/boost/fmt/freetype 垫片做语法检查（本会话用过，垫片在
> `$TMPDIR/mp/shim`，可随时重建）。

### 1.1 UI 预览层（`src/debug/kotlin`，只属于 debug 变体）

改 UI 布局不必装 APK、不必真机：预览代码全在
`app/app/src/debug/kotlin/org/dpdns/clevebitr/ui/preview/`，Android Studio 里打开
对应 `*Previews.kt` 的 Split / Design 即可看效果。

| 文件 | 覆盖页面 |
| --- | --- |
| `PreviewFixtures.kt` | 共享假数据（`previewGame` / `previewGames` / `previewConfig` / `previewGlobalDefaults`） |
| `GameDetailPreviews.kt` | 详情页：411dp 浅色/深色 + 平板 800dp |
| `LibraryPreviews.kt` | 库页：三条数据（超长标题/无封面/未刮削）+ 空库 |
| `GameSettingsPreviews.kt` | 单游戏设置页（独立覆盖态） |
| `SettingsPreviews.kt` | 全局设置页 + 关于页 |
| `OverlayPreviews.kt` | 性能叠加层：默认 / 全字段 2.0x 右下 / 0.6x 半透明 |

规则（违反任一条，预览就从“零成本”变成“构建负担”）：

1. **一个页面一个 `*Previews.kt`，文件里只放 `@Preview` 函数**，不写业务逻辑；
   假数据统一放 `PreviewFixtures.kt`，改一个字段只改一处。
2. `coversDir` 一律指向不存在的目录 → `CoverImage` 退化成占位封面，
   预览因此既不依赖真实文件、也不联网。
3. **`src/main` 里不要再写 `@Preview`**。预览层靠 `app/app/build.gradle.kts` 里的
   `getByName("debug") { kotlin.srcDirs("src/debug/kotlin") }`（:82）+ 两行
   `debugImplementation("androidx.compose.ui:ui-tooling-preview")` /
   `ui-tooling`（:96–97）生效；主源码集一旦又冒出 `@Preview`，release 就重新需要 ui-tooling。
4. 验证：`bash scripts/build_shell_local.sh`（即 `:app:compileDebugKotlin`）通过；
   再跑一次 `:app:compileReleaseKotlin`，并在 `app/build/tmp/kotlin-classes/release`
   下确认搜不到 `ui/preview`（release 一行预览代码都不带）。

---

## 2. 关键文件与接口索引

| 东西 | 位置 | 说明 |
|---|---|---|
| 游戏库数据 | `core/GameLibrary.kt` | `LibraryGame`（含 `coverFile` / `group` / `favorite` / `addedAt` / `lastPlayedAt`） |
| 每游戏配置 | `core/GameConfig.kt` | `GameConfig`（`krkr2next.json` 的模型）、`GameConfigStore`、`GlobalDefaults`、`GamePaths` |
| 每游戏覆盖数据的**范式** | `core/OverlayConfig.kt` | 性能叠加层的字段集合：`enum OverlayField(key, label)`，**key 落盘、不能改**，未知键忽略 |
| 叠加层编辑器 UI 范式 | `ui/OverlayConfigEditor.kt` | 「一组可勾选/可配置项」的编辑界面写法，可直接照抄给自定义按键 |
| 自定义按键浮层（渲染 + 编辑态） | `ui/KeyPadOverlay.kt` | 只按钮命中区消费事件，其余穿透；长按按系统 repeat 补发 down |
| 自定义按键配置模型 | `core/KeyPadConfig.kt` | `KeyButton` / `KeyPadProfile`，归一化坐标，JSON 落盘 |
| 自定义按键属性编辑器 | `ui/KeyPadConfigEditor.kt` | 全局设置页与游戏设置页共用；键位/图标/颜色/透明度/描边/位置大小 |
| MD3 图标登记表 | `ui/KeyPadIcons.kt` | 落盘的是稳定键名而非 `ImageVector` 名 |
| 游戏画面（浮层宿主） | `ui/GameScreen.kt` | 引擎 Surface 与壳侧浮层共存的地方；性能叠加层就挂在这里 |
| 性能叠加层绘制 | `ui/PerformanceOverlay.kt` | 壳侧浮层如何画在游戏之上、如何按帧取 `PerfSnapshot` |
| 引擎交互 | `core/NativeEngine.kt` | 壳↔引擎的唯一 Kotlin 入口（`external` 方法，22 个） |
| 引擎输入事件 | `core/VkCodes.kt` | **Windows VK 码**表；`engine_input_event_t.key_code` 用的就是它 |
| 引擎 C ABI | `bridge/engine_api/include/engine_api.h` | 稳定 ABI；改这里必须同步 JNI 与 Kotlin（AGENTS §6） |
| 导航 | `ui/Nav.kt` | `Routes`（`DETAIL` / `SETTINGS` / `GAME_SETTINGS` …）与 NavHost |
| 每游戏日志 | `core/LogFiles.kt`、`core/EngineSession.kt` | 日志按游戏分目录 |
| 版本号（含 git 短哈希） | `app/app/build.gradle.kts` | `v0.1.0-<hash6>-<YYMMDD>`，出现在关于页/设置/app.log/崩溃报告 |

---

## 3. 已完成：自定义按键浮层

> 2026-09-23 落地；下面保留目标行为与验收标准，实现落点见表。

### 3.0 实现落点

- 模型/存储：`core/KeyPadConfig.kt`（`KeyButton` / `KeyPadProfile`，归一化坐标，JSON）；
  每游戏存 `krkr2next.json` 的 `keypad` 段（`GameConfig.keypad`），全局默认与具名模板存
  `AppPrefs`（`input.keypad_profile` / `input.keypad_templates`）。
  全局默认**未设置时返回 `KeyPadProfile.starter()`（默认开启）**：方向键 + 确认/返回。
- 渲染/交互：`ui/KeyPadOverlay.kt`。**只有按钮命中区消费事件**，其余穿透给引擎；
  编辑态由 `GameScreen` 的 SurfaceView 监听吞掉全部触摸（否则拖按钮会把 `POINTER_DOWN`
  送进游戏）。长按按系统 repeat 心跳补发 down（引擎不生成 repeat）。
- 编辑：游戏内悬浮菜单 →「编辑自定义按键」进入编辑态，顶部工具条
  **添加 / 属性 / 删除 / 完成**。选中按钮后可拖拽移动、右下角把手缩放；「属性」打开
  `ModalBottomSheet` 里的 `KeyPadConfigEditor`，**游戏内即可完整配置**
  （键位/文字/图标/颜色/大小/透明度/描边/位置大小），无需退出游戏。
- 拖拽/缩放为什么用"绝对目标"：`pointerInput` 的 block 不随 x/y/w/h 重建，
  若按"当前值 + 增量"累加会永远拿旧值（表现为拖不动、缩放弹回）。现改为
  手势起点快照 + 累计位移写绝对目标，并用 `rememberUpdatedState` 取最新回调；
  选择与拖拽合并在**同一个** `awaitEachGesture` 里（`detectTapGestures` 会在 down 上
  `consume()`，与同一节点的 `detectDragGestures` 冲突）。
- 落盘时机：编辑态内只改内存，**退出编辑态/切后台/退出游戏**时才写一次
  `krkr2next.json`（拖拽每帧都改数据，不能每帧落盘）。

### 3.1 目标行为

用户可在**游戏画面上**叠一组自定义按钮，点它 = 按键盘对应键；每个按钮的样式可配：
**位置、大小、文字、MD3 图标、文字颜色、文字大小、背景色、透明度、描边**。
配置**按游戏保存**，并可**存成全局模板**供别的游戏套用。

### 3.2 数据模型（建议）

放 `core/KeyPadConfig.kt`，照 `OverlayConfig.kt` 的纪律：**落盘的键名一旦发布不可改**、
未知字段忽略、新增只追加。

```kotlin
data class KeyButton(
    val id: String,              // uuid，落盘；按钮增删靠它
    val vk: Int,                 // Windows VK 码（见 core/VkCodes.kt）
    val label: String = "",      // 按钮文字（可为空，只显示图标）
    val iconKey: String? = null, // MD3 图标的稳定键名（不要落盘 ImageVector 名）
    val x: Float, val y: Float,  // 归一化 0..1（相对游戏画面），跨分辨率稳定
    val w: Float, val h: Float,  // 同上
    val textColor: Long = 0xFFFFFFFF,
    val textSizeSp: Float = 14f,
    val bgColor: Long = 0x66000000,
    val alpha: Float = 1f,       // 整按钮不透明度
    val strokeWidthDp: Float = 0f,
    val strokeColor: Long = 0xFFFFFFFF,
)

data class KeyPadProfile(val buttons: List<KeyButton>, val enabled: Boolean = false)
```

存储：每游戏一份进 `GameConfig`（或 `krkr2next.json` 的壳侧段），全局模板进 `AppPrefs`。
`GameConfigStore` 已有"读→合并→写"的现成写法，照抄即可。

### 3.3 落点与做法

1. **浮层宿主**：`ui/GameScreen.kt` 里已经有壳侧浮层（性能叠加层）与引擎 Surface 共存
   的结构，按键浮层加在同一层，且**必须在引擎 Surface 之上**。
2. **命中与穿透（最容易踩的坑）**：浮层容器默认会吃掉触摸事件，导致游戏收不到点击。
   只让**按钮自身的命中区**消费事件，其余区域要让事件落到引擎；Compose 侧用
   `Modifier.pointerInput` 精确命中，或把按钮以外的区域设成不接收输入。
3. **按键注入**：走 `core/NativeEngine.kt` 已有的输入通道（`engine_input_event_t`），
   `key_code` 用 **Windows VK 码**（AGENTS §11）。按下与抬起要**成对**发：
   ```
   down(vk)  -> engine_input_event_t(action=DOWN, key_code=vk)
   up(vk)    -> ...(action=UP, ...)
   ```
   长按要按 系统 repeat 的心跳补发 down（游戏里"按住方向键"是常态）。
4. **编辑态**：进入编辑态时浮层接管输入（此时**不要**把编辑手势透给游戏），
   支持拖拽定位、右下角把手缩放；退出编辑态回到"只有按钮命中区消费事件"。
5. **模板**：设置页加"保存为模板 / 从模板应用"，模板里只存 `buttons`（丢弃 id 重新分配）。

### 3.4 约束（来自 AGENTS 与已踩的坑）

- `key_code` 是 **VK 码**，不是 Android `KEYCODE_*`（`VkCodes.kt` 有转换表）。
- 指针坐标在壳里是**物理像素**，不要重复应用缩放（AGENTS §11）。
- 浮层不得改变引擎的输入时序：只在用户按下/抬起时发事件，不要每帧发。
- 不要为这个功能引入新库（AGENTS §2）。

### 3.5 验收标准（已实现，待真机确认）

- 关掉时**完全不拦截**触摸（游戏操作与之前逐帧一致）；
- 一个按钮能触发游戏里对应键的效果（例如方向键、Enter、Esc）；
- 改样式立刻生效且**存盘后重启仍在**；
- 换分辨率（手机横竖屏/平板）后按钮相对位置不跑偏；
- 模板能跨游戏套用。

---

## 4. 已完成：引擎游戏设置侧边栏（§4）

> 2026-09-23 落地（含引擎侧 C ABI）。

### 4.0 实现落点

引擎侧：
- `cpp/core/visual/impl/MenuItemImpl.{h,cpp}`：`TVPSerializeMainWindowMenu()` /
  `TVPInvokeMainWindowMenuItem()`。菜单树就是 KiriKiri 的 `tTVPMenuItem`
  （`Window.menu` 的返回值）；根菜单项用窗口的 `HWND` 属性值（**其实就是
  `tTJSNI_Window*` 本身**）到 `MenuItemImpl.cpp` 的 `MENU_LIST` 里查。
  `Window.menu` 能解析到根，靠的是 TJS2 的 "default member invocation"
  （`tjsObject.cpp` 的 `TJSDefaultPropGet`：成员值是对象时会以 `membername=nullptr`
  调它的 `PropGet`）。
- `bridge/engine_api`：新增 `engine_list_window_menu(out, size, written)` 与
  `engine_invoke_window_menu(id)`（三处同步：`engine_api.h` / 实现 / JNI）。
  菜单树只在 owner 线程变动 ⇒ **快照在 tick 里降频刷新（~15 帧）**，壳从任意
  线程加锁读；触发只入队，由 tick（或模态泵）在 owner 线程派发。
- 序列化格式：每行一项，`\t` 分隔 `depth checked enabled id title`；`id` 是路径
  （顶层 `0`、子项 `0.2`），供触发。不可见项不输出。

壳侧：
- `core/EngineMenu.kt`：模型 + 文本解析（坏行跳过）。
- `core/EngineSession.kt`：`windowMenu()` / `invokeWindowMenu(id)`。
- `ui/EngineMenuSidebar.kt`：右侧面板 + 可点击遮罩（**打开时暂停游戏输入**）+ 空态。
- `ui/GameScreen.kt`：右下角小 FAB（在悬浮菜单 FAB 上方）；可从悬浮菜单隐藏。
  开关存 `AppPrefs.engineMenuButton`（默认显示），设置页也有。

追加（2026-09-23 二轮）：
- 右下角改为**按钮抽屉**：收起时只剩贴右边缘的长条把手（箭头指示收起/展开），
  展开后露出「引擎菜单」与「更多」两个**同尺寸小按钮**；空闲 5s 自动收起。
  两个按钮都收进抽屉，不再常占游戏画面。
- 侧边栏菜单项支持**折叠/展开**（按 `depth` 还原成树，箭头带旋转动画）。
- 「退出游戏」与「强制退出」都改为 error 色且需二次确认。
- `engine_is_modal_active()`：模态对话框期间 `engine_tick` 阻塞在嵌套循环里，
  壳的"无响应"看门狗据此豁免，避免把开着的对话框误判成卡死。

### 4.1 目标行为

游戏中**右下角**一个按钮（仅在游戏画面之上、不挡操作），点击后从**右侧弹出悬浮侧边栏**，
列出**引擎注册的窗口菜单项**（即 Windows 版标题栏下方菜单栏那套：KiriKiri 的
`tTVPMenuItem`，游戏通过 `System.setArgument` / `Window.menu` 注册），点条目即触发该项。

### 4.2 需要新增的引擎侧工作（C ABI + JNI，跨模块）

**这是本功能的主体工作，壳侧只是壳。** 现状：`bridge/engine_api/include/engine_api.h`
里**没有任何菜单枚举接口**（只有窗口关闭通知）。需要：

1. 引擎侧找到菜单表：KiriKiri 的菜单在 `cpp/core/environ/**` 与
   `cpp/core/visual/WindowIntf.*` 一线（`tTVPMenuItem` / `tTVPMenuItemIntf`，
   `Window.menu` / `Window.setMenu`）。先确认本仓库这条链路是否完整、谁在填充菜单。
2. 新增 C ABI（建议）：
   ```c
   /* 列出当前窗口菜单项。写入调用方缓冲，返回条数；字段包含 id/标题/是否勾选/是否可用。 */
   size_t engine_list_window_menu(engine_menu_item_t *out, size_t cap);
   /* 触发某一项（按 id）。返回 0 成功。 */
   int engine_invoke_window_menu(const char *id);
   ```
   注意 `engine_api.h` 有 ABI 版本（`apiVersion=0x1000000`）与 Kotlin 侧 `external`
   声明，**三处必须同步**（AGENTS §6、`scripts/check_static.sh` 会查 JNI 符号）。
3. **线程与生命周期**：菜单回调原本跑在窗口消息线程（Windows）上。Android 壳里
   EGL/渲染是单一渲染线程，UI 线程不得触碰 GL（AGENTS §7）。触发菜单要交给
   引擎 tick 侧执行，不要在 JNI 调用里直接跑。
4. 菜单项可能带勾选/禁用状态（如"全屏"），ABI 里要带上，UI 才好显示。

### 4.3 壳侧

- `ui/GameScreen.kt`：右下角加一个 `FloatingActionButton`（小尺寸、半透明、可隐藏）。
- 侧边栏用 `ModalNavigationDrawer` 或 `androidx.compose.material3` 的
  `ModalBottomSheet`/自绘 `AnimatedVisibility` + `Surface`；**要点**：展开时不要
  全屏遮罩吃掉游戏输入，或明确"打开时暂停输入"。
- 列表数据来自 `core/NativeEngine.kt` 新增的 `external fun`（枚举/触发）。
- 空态：游戏没注册任何菜单项时，侧边栏显示"本游戏没有引擎菜单项"，不要显示空白。

## 4.4 验收标准（已实现，待真机确认）

- 侧边栏列出的条目与 Windows 版菜单一致（至少覆盖游戏实际注册的那几项）；
- 触发后行为与 Windows 版一致（例如"全屏/配置/关于"打开对应窗口）；
- 不改变引擎 tick 时序、不在 UI 线程碰 GL；
- 无菜单项的游戏不崩、不显示空白。

---

## 5. 已完成：详情页改版（记录做法与理由）

`ui/GameDetailScreen.kt` 原来是"封面在上、标题与按钮在下"的竖排，宽屏时**启动按钮会被
封面挤出首屏**。现已改为 **Steam 大屏式左右分栏**：

- 左：封面固定宽（宽屏 240dp / 窄屏 132dp），收藏星标压在封面左上角（与库页卡片一致）；
- 右：标题（限两行 + 省略号）、`厂商 · 发售日`、分组芯片，然后是主操作
  **启动游戏 / 游戏设置**；编辑态时表单就地替换右侧标题区，不再另占一行；
- 整页仍有 `verticalScroll`（下半部分的"当前生效/元数据"卡片保持原样）。

改动的理由：详情页的第一诉求是"开游戏"，主按钮必须和封面同屏可见；左右分栏在
平板/横屏上也能一屏放下"封面 + 标题 + 按钮"。

**2026-09-23 布局微调**：右侧改为自上而下 **标题 → 标签 → 简介（可折叠）**
（封面在左，标签/简介两块在封面右侧）；简介默认只显示 5 行，点"展开简介"
看全文——否则右侧两块很容易比封面高出一大截。

**2026-09-23 按钮落点修正**：主按钮从右栏移出，改成**封面/简介整块下面的全宽横排**
（`Row` + 两枚各 `weight(1f)`，左右各留 16dp）。原因是窄屏（411dp）右栏只剩约 220dp，
两枚"图标 + 文字"按钮并排必然换行或截断；提到整行宽度（约 380dp）后两枚各占一半，
竖屏/横屏、手机/平板都不变形，也不再需要 `FlowRow` 兜底换行。

**2026-09-23 库页分组便签定位**（`ui/LibraryScreen.kt`）：卡片上的分组芯片原来用
`align(BottomStart)` + `padding(bottom = 60.dp)` 贴在卡片底部上方，那个 `60.dp` 是照
“标题两行 + 有副标题”量出来的写死值，标题一行或没副标题时便签就飘离标题栏。
现在把**封面和便签放进同一个 `Box`**，便签对齐该 Box 底边并留
`GROUP_CHIP_TITLE_GAP`（5dp）——该 Box 的底边就是标题栏的顶边（两者上下相邻），
所以便签永远压在标题栏上方 5dp，与标题行数、有无副标题、字号都无关。

**2026-09-23 库页分组便签边框颜色**（`ui/LibraryScreen.kt`）：`SuggestionChip` 的
容器色默认是 `Transparent`（便签浮在封面上，边线是它唯一的“框”），而默认边线
取 `SuggestionChipTokens.FlatOutlineColor = OutlineVariant`。本项目
dark 配色里 `outlineVariant == surfaceVariant == 0xFF45464F`，便签压在封面上
完全看不出边界。现改为显式传入
`SuggestionChipDefaults.suggestionChipBorder(enabled = true, borderColor = colorScheme.outline)`
（`Theme.kt`，浅 `:94` `0xFF777589` / 深 `:61` `0xFF918EA4`）：颜色由配色方案给出，
随明暗主题自动切换，宽度仍沿用 token 的 1dp。

**2026-09-24 配色方案重做 + 关于页重组**：
1) `ui/Theme.kt` 里 `LightScheme` 沿用的是 M3 **基线紫调**中性色
   （`#FBF8FF`/`#F5F2FA`/`#E2E1EC`/`#C6C5D0`/`#767680` 全是紫的），而 `DarkScheme`
   是自调的蓝调灰（`#121318`/`#1A1B21`/`#45464F`/`#90909A`）——两套皮肤不同源，
   浅色是“紫白底 + 蓝主色”，叠起来看着很怪。现在两套**由同一个种子色推导**
   （种子 = 品牌蓝 `#6C7BFF`，色相 ≈ 295°，按 M3 的 tone 体系取值，每个色值尾行
   标了 `// P80` 这类 tone 注释）：P=种子色相（彩度上限 64），S=同色相彩度 20，
   T=色相 +60，N=彩度 4 的灰（所有 surface/background），NV=彩度 12（surfaceVariant/
   outline/分割线）；`error*` 有意不写（红是语义色，交给 `lightColorScheme`/
   `darkColorScheme` 兜底）。**底色不能换色相**是这次的教训。
   深色主色从 `#6C7BFF`(tone≈57) 改成 tone80 的浅紫 `#C4C0FF` —— 原先深色
   `primary` 比 `secondary` 还暗，把 M3 的主/次明度关系倒过来了；代价是深色下
   填充按钮从“深底白字”变成“浅底深字”（`FilledTonalButton`/`Button` 看起来更"亮"）。
   生成脚本在仓库外（CIELCh 近似推导 tone、裁 sRGB 色域、核对 WCAG），
   改色请连带核对对比度（现全部 ≥ 4.5:1）。
2) 设置页的那段“关于”（名称/版本/包名/系统/ABI/机型）**搬到 `ui/AboutScreen.kt`**，
   设置页删掉，只留一行注释指向关于页——只读信息不该和“改一项就生效”的设置项混在同一屏。
   `SettingsScreen.kt` 因此去掉了 `android.os.Build` 与 `core.BuildInfo` 两个 import。
3) `ui/AboutScreen.kt` 新增「相关链接」区：四个全宽 `OutlinedButton`（仓库 +
   Kirikiroid2 / krkrz / AetherKiri），点击用
   `Intent(ACTION_VIEW, Uri.parse(url))` 交给系统浏览器；捕获
   `ActivityNotFoundException` → `AppLog.w` + Toast（精简 ROM 真会没有浏览器）。
   不开内置 WebView：多一个要维护的组件，还会把人关在没有地址栏的窗口里。
4) `ui/AboutScreen.kt` 也换成 `Scaffold` + `TopAppBar`（原来是个裸 `Column` + 手写
   标题行）。原因：`MaterialTheme` 只传颜色值，**不画背景也不改文字默认色**
   （`LocalContentColor` 默认就是 `Color.Black`）；裸 `Column` 在应用里被
   `ShellScaffold` 的 Scaffold 兜底还能看，一旦单独渲染（IDE 预览）就是“宿主底色 +
   一片黑字”。现在全仓 7 个页面都是 `Scaffold` 起手，不再有例外。

**2026-09-24 自适应图标 + MD3 主题图标**（`res/`）：原 `drawable/ic_launcher.xml`
是一张把圆角方形底画死在里面的整图，被启动器遮罩套上去就成了“双圆角”，
而且没有单色层，Android 13 的主题图标用不了。现在拆成标准三层：

| 资源 | 作用 |
| --- | --- |
| `mipmap-anydpi-v26/ic_launcher.xml` | `<adaptive-icon>`：background + foreground + **monochrome** |
| `mipmap-anydpi/ic_launcher.xml` | API 24–25 用的旧版整图（自带圆角方形，矢量，不用切 PNG） |
| `drawable/ic_launcher_foreground.xml` | 前景层的 “K”，不带底 |
| `drawable/ic_launcher_monochrome.xml` | 单色层（形状与前景层对齐，颜色由系统重着色） |
| `values/colors.xml` | `ic_launcher_background` #1B1B2F、`ic_launcher_foreground` #6C7BFF |

要点：自适应图标的视图是 108×108，但只显示**中心 72×72**（安全区是直径 66 的圆），
所以前景层不画底、不画圆角——形状交给遮罩；“K”占 x∈[34,77]、y∈[28,80]，
离中心最远 ≈34.7 < 72 圆半径 36，圆遮罩下也不缺角。`<monochrome>` 放在 -v26 里
即可（API 26–32 只记一条日志后忽略），不必再拆 -v33。Manifest 的 `android:icon`
也从 `@drawable/ic_launcher` 改成了 `@mipmap/ic_launcher`（旧的那个 drawable 已删）。
验证：`:app:assembleDebug` 后 `aapt2 dump xmltree --file res/mipmap-anydpi-v26/ic_launcher.xml`
能看到 background/foreground/monochrome 三层，`dump badging` 的 application-icon 指向它。

---

## 6. 不要做的事（摘自 AGENTS，改壳时最容易犯的几条）

1. **不要 `git add` 用户自己的 `.gitignore` / `README.md`**（会话开始就是 modified）。
2. **不要提交/推送**，除非用户明确要求；不 amend/rebase/force push。
3. **渲染/生命周期/JNI 改动要小**，并且三处同步（Kotlin 声明 / JNI 符号 / C++ 定义）。
4. 新加跨文件符号**顺手补 import**（本地编译能查出来，别省这一步）。
5. commit message 用 `<type>(<scope>): <中文主题>`，正文按 现象/原因/做法/验证 组织；
   **不要**把整段 message 塞进双引号 shell 参数（反引号会被命令替换）。

---

## 7. 已完成：加载期自动日志浮层（§7.1）

> 2026-09-23 落地。下面保留目标行为与验收标准。

### 7.0 实现落点

- `core/AppPrefs.kt`：`debug.auto_log_on_launch`（缺省 **false**）+ getter/setter。
- `core/GameConfig.kt`：`GameConfig.autoLogOnLaunch: Boolean?`（null = 继承），落盘键
  `autoLogOnLaunch`；并入 `GlobalDefaults` / `ResolvedShellSettings` / `resolve()`。
- `ui/GameScreen.kt`：新参数 `autoShowLogs`；`LaunchedEffect(startupState, autoShowLogs)`
  在 `startup state 2` 时把浮层关掉，在此之前按开关自动弹出；用户手动关过就记一个
  `remember` 标志，**本局**不再弹（GameScreen 每个会话重新组合，标志自然按局重置）。
  **失败态（3）不关**——那时日志正是要看的。
- `MainActivity.kt`：`sessionAutoLogOnLaunch` 在 `startSession()` 里与每游戏覆盖合并；
  全局开关改动在“该游戏没有独立配置”时立刻跟到本局（它只是显示开关，引擎不读）。
- 全局设置页与游戏设置页各加一行（游戏页沿用“使用独立配置 + 值”的既有范式）。

**目标行为**：新增一个开关（全局默认 + 每游戏覆盖，与叠加层/按键同范式）。打开后：
从 `launchPath()` 到 `startup state -> 2` 期间**自动显示运行时日志浮层**，游戏起来后
**自动关闭**；用户手动关闭后本局不再自动弹。

**约束**：日志浮层打开时会吞掉 SurfaceView 的触摸（现有 `logsVisible` 分支）——启动期本来就
不该操作游戏，但**自动关闭必须可靠**，否则用户会被卡在只能看日志的界面（已有一个可点关闭按钮）。

**验收**：打开开关 → 开游戏立即看到日志；`startup state -> 2` 后自动消失；
关掉开关行为与现在完全一致；每游戏覆盖生效。
