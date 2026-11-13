<#
.SYNOPSIS
    把 Windows 侧（Android Studio / Gradle）构建出来的 APK 装到模拟器或真机，拉起 app，
    并把 logcat 里与 KrKr2Next 有关的部分打出来。

.DESCRIPTION
    这是 WSL 侧 build.sh / install.sh 的 Windows 对应物：**必须用 Windows 侧构建的 APK**，
    因为 WSL 的 ~/.android/debug.keystore 与 Windows/AS 那把不是同一个 key，
    用 WSL 产物去覆盖 AS 装过的包会报 INSTALL_FAILED_UPDATE_INCOMPATIBLE
    （HANDOFF.md §1.14 记了这个坑）。

    默认流程：定位 adb → 选设备 → `adb install -r -t` → force-stop → logcat -c →
    `am start` → 等几秒 → 打印 pid 与过滤后的日志（并显式指出有没有 SIGABRT）。

    退出码：0 = 装好且（若拉起了）进程存活；1 = 环境/安装失败；2 = 装上了但本次启动崩溃。

.PARAMETER Variant
    debug（默认）或 release，决定去 app/app/build/outputs/apk/<variant>/ 找 APK。

.PARAMETER Apk
    直接指定 APK 路径，跳过自动查找。

.PARAMETER Build
    先跑一次 Windows 侧 Gradle 构建（app\gradlew.bat :app:assemble<Variant>）再安装。
    找不到 JAVA_HOME 时会尝试 C:\Program Files\Eclipse Adoptium\jdk-21*。

.PARAMETER Serial
    多设备时指定目标（adb devices 里那一列），例如 emulator-5554。

.PARAMETER Uninstall
    装之前先卸载（会清掉 app 数据/设置），用于绕开签名不一致等安装失败。

.PARAMETER NoLaunch
    只安装，不 force-stop / 不拉起 / 不抓日志。

.PARAMETER WaitSeconds
    am start 之后等多久再抓日志（默认 12 秒，够引擎加载完）。

.PARAMETER LogLines
    打印过滤后日志的末尾多少行（默认 40）。0 = 不过滤行数上限。

.PARAMETER All
    不做过滤，直接 dump 整个 logcat 缓冲区（排查非 KrKr2Next 标签的问题时用）。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install_to_emulator.ps1

.EXAMPLE
    # 先构建再装，release 变体，指定设备
    .\scripts\install_to_emulator.ps1 -Build -Variant release -Serial emulator-5554
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Variant = 'debug',

    [string]$Apk,
    [switch]$Build,
    [string]$Serial,
    [switch]$Uninstall,
    [switch]$NoLaunch,

    [int]$WaitSeconds = 12,
    [int]$LogLines = 40,
    [switch]$All,

    [string]$Package = 'org.dpdns.clevebitr',
    [string]$Activity,
    [string]$Sdk
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host "==== $Text ====" -ForegroundColor Cyan
}

function Write-Ok([string]$Text) { Write-Host "  [OK] $Text" -ForegroundColor Green }
function Write-Warn2([string]$Text) { Write-Host "  [WARN] $Text" -ForegroundColor Yellow }
function Write-Err([string]$Text) { Write-Host "  [ERROR] $Text" -ForegroundColor Red }

# ---------------------------------------------------------------------------
# 工程根（本脚本在 scripts/ 下）
# ---------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir
$Adb = $null

# adb 的输出是 UTF-8，而 PowerShell 5.1 默认按控制台的 ANSI 代码页解码原生命令输出，
# 于是 logcat 里的中文会变成乱码。这里临时把解码改成 UTF-8，跑完再还原，
# 这样脚本自己的中文提示仍然用原来的编码输出。
function Invoke-Adb {
    param([string[]]$AdbArgs)

    $savedEncoding = [Console]::OutputEncoding
    # 把原生命令的 stderr 重定向进来时会变成 ErrorRecord，而全局 $ErrorActionPreference='Stop'
    # 会把它升级成终止性错误（例如 adb install 的 INSTALL_FAILED_* 会直接被吞掉，看不到我们的提示）。
    # 这里局部降级，失败与否由调用方看输出内容/退出码自己判断。
    $savedEap = $ErrorActionPreference
    try {
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
        $ErrorActionPreference = 'Continue'
        return @(& $Adb @AdbArgs 2>&1)
    }
    finally {
        [Console]::OutputEncoding = $savedEncoding
        $ErrorActionPreference = $savedEap
    }
}

# ---------------------------------------------------------------------------
# 定位 Android SDK / adb
# ---------------------------------------------------------------------------
function Resolve-SdkRoot {
    param([string]$Explicit)

    if ($Explicit) { return $Explicit }
    if ($env:ANDROID_HOME) { return $env:ANDROID_HOME }
    if ($env:ANDROID_SDK_ROOT) { return $env:ANDROID_SDK_ROOT }

    # app/local.properties 里形如 sdk.dir=C\:\\Users\\xiaol\\AppData\\Local\\Android\\Sdk
    # （Java properties 转义：`\:` → `:`，`\\` → `\`）。构建脚本会临时把它改写成 WSL 路径
    # 再还原，这里只读不写。
    $localProps = Join-Path $Root 'app\local.properties'
    if (Test-Path $localProps) {
        foreach ($line in Get-Content $localProps) {
            if ($line -match '^\s*sdk\.dir\s*=\s*(.+?)\s*$') {
                $candidate = $Matches[1] -replace '\\\\', '\' -replace '\\:', ':'
                if ($candidate -match '^[A-Za-z]:' -or $candidate -match '^\\\\') {
                    if (Test-Path $candidate) { return $candidate }
                    Write-Warn2 "local.properties 里的 sdk.dir 不存在：$candidate"
                }
                else {
                    Write-Warn2 "local.properties 里的 sdk.dir 不是 Windows 路径（$candidate），跳过"
                }
            }
        }
    }

    $fallback = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
    if (Test-Path $fallback) { return $fallback }
    return $null
}

Write-Step '定位 adb'
$SdkRoot = Resolve-SdkRoot -Explicit $Sdk
if (-not $SdkRoot) {
    Write-Err '找不到 Android SDK。用 -Sdk <路径> 指定，或设置 ANDROID_HOME。'
    exit 1
}
$Adb = Join-Path $SdkRoot 'platform-tools\adb.exe'
if (-not (Test-Path $Adb)) {
    Write-Err "SDK 里没有 adb：$Adb"
    exit 1
}
Write-Ok "SDK  $SdkRoot"

# ---------------------------------------------------------------------------
# 可选：先构建（Windows 侧，debug keystore 与 AS 一致）
# ---------------------------------------------------------------------------
if ($Build) {
    Write-Step "构建 APK（$Variant，Windows 侧 Gradle）"
    $gradlew = Join-Path $Root 'app\gradlew.bat'
    if (-not (Test-Path $gradlew)) { Write-Err "找不到 $gradlew"; exit 1 }

    if (-not $env:JAVA_HOME -or -not (Test-Path $env:JAVA_HOME)) {
        $candidates = @()
        $candidates += Get-ChildItem 'C:\Program Files\Eclipse Adoptium' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like 'jdk-21*' } | ForEach-Object { $_.FullName }
        $candidates += Get-ChildItem 'C:\Program Files\Java' -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName }
        if ($candidates.Count -gt 0) {
            $env:JAVA_HOME = $candidates[0]
            Write-Warn2 "JAVA_HOME 未设置，暂用 $($env:JAVA_HOME)"
        }
        else {
            Write-Warn2 '找不到 JDK（AGP 8.13 需要 JDK 17/21）。若 Gradle 报错请设置 JAVA_HOME。'
        }
    }
    Write-Ok "JAVA_HOME  $($env:JAVA_HOME)"

    $task = ':app:assemble' + ($Variant.Substring(0, 1).ToUpper() + $Variant.Substring(1))
    Push-Location (Join-Path $Root 'app')
    try {
        # Gradle 会往 stderr 写提示，全局 Stop 会把它变成终止性错误 → 局部降级，靠退出码判断成败。
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & $gradlew $task '--console=plain' }
        finally { $ErrorActionPreference = $savedEap }
        if ($LASTEXITCODE -ne 0) { Write-Err "Gradle 构建失败（exit $LASTEXITCODE）"; exit 1 }
    }
    finally { Pop-Location }
    Write-Ok "构建完成：$task"
}

# ---------------------------------------------------------------------------
# 找 APK
# ---------------------------------------------------------------------------
Write-Step '选择 APK'
if (-not $Apk) {
    $apkRoot = Join-Path $Root 'app\app\build\outputs\apk'
    $apkDir = Join-Path $apkRoot $Variant
    $expected = Join-Path $apkDir "app-$Variant.apk"
    if (Test-Path $expected) {
        $Apk = $expected
    }
    elseif (Test-Path $apkDir) {
        $newest = Get-ChildItem $apkDir -Filter '*.apk' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest) { $Apk = $newest.FullName }
    }
    if (-not $Apk -and (Test-Path $apkRoot)) {
        # 变体目录不存在时（例如 AS 刚 clean 过）兜底在整个 apk 输出树里找该变体
        $newest = Get-ChildItem $apkRoot -Recurse -Filter "*$Variant*.apk" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest) { $Apk = $newest.FullName }
    }
    if (-not $Apk) {
        Write-Err "找不到 $Variant 的 APK。"
        if (Test-Path $apkRoot) {
            $others = @(Get-ChildItem $apkRoot -Recurse -Filter '*.apk' -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending)
            if ($others.Count -gt 0) {
                Write-Host '         输出目录里现有这些 APK（可用 -Apk 指定，或换 -Variant）：' -ForegroundColor Yellow
                $others | Select-Object -First 5 | ForEach-Object {
                    Write-Host ("           {0}  ({1:N1} MB, {2})" -f $_.FullName, ($_.Length / 1MB), $_.LastWriteTime) -ForegroundColor Yellow
                }
            }
        }
        Write-Host '         先在 Android Studio 里构建（Build > Build APK(s)，或 Run 一次），或加 -Build 走命令行。' -ForegroundColor Yellow
        exit 1
    }
}
if (-not (Test-Path $Apk)) {
    Write-Err "指定的 APK 不存在：$Apk"
    exit 1
}
$apkItem = Get-Item $Apk
Write-Ok ("{0}  ({1:N1} MB, {2})" -f $apkItem.FullName, ($apkItem.Length / 1MB), $apkItem.LastWriteTime)

# ---------------------------------------------------------------------------
# 选设备
# ---------------------------------------------------------------------------
Write-Step '选择设备'
$deviceLines = Invoke-Adb @('devices') | Select-Object -Skip 1 | Where-Object { $_ -match '^\S+\s+device\s*$' }
$serials = @($deviceLines | ForEach-Object { ($_ -split '\s+')[0] })
if ($serials.Count -eq 0) {
    Write-Err '没有已连接的设备/模拟器。先启动 AVD（或插上真机并允许 USB 调试），再用 adb devices 确认。'
    exit 1
}
if ($Serial) {
    if ($serials -notcontains $Serial) {
        Write-Err "指定设备 $Serial 不在列表里：$($serials -join ', ')"
        exit 1
    }
}
elseif ($serials.Count -eq 1) {
    $Serial = $serials[0]
}
else {
    $emulators = @($serials | Where-Object { $_ -like 'emulator-*' })
    if ($emulators.Count -eq 1) {
        $Serial = $emulators[0]
        Write-Warn2 "有 $($serials.Count) 台设备，自动选了唯一的模拟器 $Serial（可用 -Serial 覆盖）"
    }
    else {
        Write-Err "有 $($serials.Count) 台设备，请用 -Serial 指定：$($serials -join ', ')"
        exit 1
    }
}
Write-Ok "设备  $Serial"

# ---------------------------------------------------------------------------
# 卸载（可选）
# ---------------------------------------------------------------------------
if ($Uninstall) {
    Write-Step "卸载 $Package（会清掉 app 数据）"
    Invoke-Adb @('-s', $Serial, 'uninstall', $Package) | ForEach-Object { "  $_" }
}

# ---------------------------------------------------------------------------
# 安装
# ---------------------------------------------------------------------------
Write-Step '安装'
# -r 保留 app 数据并覆盖安装；-t 允许 test/debuggable 包（AS 装出来的包需要它）。
$installOut = Invoke-Adb @('-s', $Serial, 'install', '-r', '-t', $Apk)
$installOut | Where-Object { $_ -notmatch '^System\.Management\.Automation\.' } | ForEach-Object { "  $_" }
$installText = $installOut -join "`n"
if ($installText -match 'INSTALL_FAILED_UPDATE_INCOMPATIBLE') {
    Write-Err '签名不一致：现装的包与本 APK 的 key 不同。'
    Write-Host '         常见原因：设备上现装的包是 WSL 侧 Gradle 装的（WSL 的 ~/.android/debug.keystore' -ForegroundColor Yellow
    Write-Host '         与 Windows/AS 那把不是同一个 key，见 HANDOFF.md §1.14）。' -ForegroundColor Yellow
    Write-Host '         两种做法：① 在 Windows 侧构建（本脚本加 -Build）后安装；' -ForegroundColor Yellow
    Write-Host '                   ② 加 -Uninstall 先卸载（注意会清掉 app 内的设置与游戏库配置）。' -ForegroundColor Yellow
    exit 1
}
if ($installText -notmatch 'Success') {
    Write-Err '安装失败（见上面 adb 输出）。'
    exit 1
}
Write-Ok '安装成功'

if ($NoLaunch) { exit 0 }

# ---------------------------------------------------------------------------
# 解析启动组件
# ---------------------------------------------------------------------------
Write-Step '拉起 app'
$component = $null
if ($Activity) {
    $component = if ($Activity -like "$Package/*") { $Activity } else { "$Package/$Activity" }
}
else {
    $resolve = Invoke-Adb @('-s', $Serial, 'shell', 'cmd', 'package', 'resolve-activity', '--brief',
        '-c', 'android.intent.category.LAUNCHER', $Package)
    $line = @($resolve | Where-Object { $_ -match '/' } | Select-Object -Last 1)
    if ($line) { $component = $line.Trim() }
}
if (-not $component) {
    Write-Warn2 "解析不到启动 Activity，退回 $Package/.MainActivity"
    $component = "$Package/.MainActivity"
}
Write-Ok "组件  $component"

# force-stop 后再清 logcat：这样抓到的日志一定属于本次启动（否则会看到上次崩溃的残留）。
Invoke-Adb @('-s', $Serial, 'shell', 'am', 'force-stop', $Package) | Out-Null
Invoke-Adb @('-s', $Serial, 'logcat', '-c') | Out-Null
Invoke-Adb @('-s', $Serial, 'shell', 'am', 'start', '-n', $component) | ForEach-Object { "  $_" }

# ---------------------------------------------------------------------------
# 等一会儿，然后看进程与日志
# ---------------------------------------------------------------------------
Write-Host "  等待 $WaitSeconds 秒（引擎加载 + 首帧）..." -ForegroundColor DarkGray
Start-Sleep -Seconds $WaitSeconds

Write-Step '结果'
$pidOut = Invoke-Adb @('-s', $Serial, 'shell', 'pidof', $Package)
$pidText = ($pidOut | Out-String).Trim()
if ($pidText) {
    Write-Ok "进程存活：pid=$pidText"
}
else {
    Write-Err '进程不在了（可能崩了或被系统杀掉），看下面的日志'
}

$log = Invoke-Adb @('-s', $Serial, 'logcat', '-d', '-v', 'threadtime')
if ($All) {
    $shown = @($log | Select-Object -Last $LogLines)
}
else {
    # 默认只留本项目自己的日志 + 崩溃/翻译层信号行；把每 5 秒刷一次的模拟器 HAL 噪声排除掉。
    $shown = @(
        $log | Where-Object {
            $_ -match 'KrKr2Next' -or
            ($_ -match 'Fatal signal' -and $_ -match $Package) -or
            ($_ -match 'Zygote' -and $_ -match 'exited due to signal') -or
            $_ -match 'berberis' -or $_ -match 'libndk_translation'
        }
    )
    if ($LogLines -gt 0) { $shown = @($shown | Select-Object -Last $LogLines) }
}

Write-Host ''
if ($shown.Count -gt 0) { $shown | ForEach-Object { Write-Host $_ } }
else { Write-Host '  （本次启动没有匹配的日志行——用 -All 看完整缓冲区）' -ForegroundColor DarkGray }

# 明确判定：本次启动有没有我们自己的 SIGABRT
$crashed = @($log | Where-Object { $_ -match 'Fatal signal' -and $_ -match $Package })
if ($crashed.Count -gt 0) {
    Write-Host ''
    Write-Err "检测到本 app 的 native 崩溃（$($crashed.Count) 条 Fatal signal）"
    exit 2
}

Write-Host ''
Write-Ok '完成'
exit 0
