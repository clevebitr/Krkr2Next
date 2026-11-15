//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "FilePathUtil.h"
// #include <delayimp.h>
// #include <mmsystem.h>
// #include <objbase.h>
// #include <commdlg.h>

#include "SysInitImpl.h"
#include "Exception.h"
#include "StorageIntf.h"
#include "StorageImpl.h"
#include "MsgIntf.h"
#include "GraphicsLoaderIntf.h"
#include "SystemControl.h"
#include "DebugIntf.h"
#include "tjsLex.h"
#include "LayerIntf.h"
#include "Random.h"
#include "DetectCPU.h"
#include "ScriptMgnIntf.h"

#include "BinaryStream.h"
#include "Application.h"
#include "ApplicationSpecialPath.h"
#include "TickCount.h"

#ifdef IID
#undef IID
#endif
#define uint32_t unsigned int

#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <functional>

#undef uint32_t

// spdlog 放在 uint32_t 宏之外：它内部有 std::uint32_t 这类限定名，
// 在 `#define uint32_t unsigned int` 生效期间会被替换成 std::unsigned int 而编译不过。
#include <spdlog/spdlog.h>

#include "Platform.h"
#include "ConfigManager/IndividualConfigManager.h"

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
ttstr nativeProjectDir;
ttstr nativeDataPath;
bool projectDirSelected = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// System security options
//---------------------------------------------------------------------------
// system security options are held inside the executable, where
// signature checker will refer. This enables the signature checker
// (or other security modules like XP3 encryption module) to check
// the changes which is not intended by the contents author.
const static char systemSecurityOptions[] =
    "-- systemSecurityOptions "
    "disablemsgmap(0):forcedataxp3(0):acceptfilenameargument(0) --";

//---------------------------------------------------------------------------
int GetSystemSecurityOption(const char *name) {
    size_t namelen = TJS_nstrlen(name);
    const char *p = TJS_nstrstr(systemSecurityOptions, name);
    if(!p)
        return 0;
    if(p[namelen] == '(' && p[namelen + 2] == ')')
        return p[namelen + 1] - '0';
    return 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// delayed DLL load procedure hook
//---------------------------------------------------------------------------
// for supporting of "_inmm.dll" (C) irori
// http://www.geocities.co.jp/Playtown-Domino/8282/
//---------------------------------------------------------------------------
/*
note:
        _inmm.dll is a replacement of winmm.dll ( windows multimedia
system dll
). _inmm.dll enables "MCI CD-DA supporting applications" to play
musics using various way, including midi, mp3, wave or digital CD-DA,
by applying a patch on those applications.

        TVP(kirikiri) system has a special structure of executable
file -- delayed loading of winmm.dll, in addition to compressed
code/data area by the UPX executable packer. _inmm.dll's patcher can
not recognize TVP's import area.

        So we must implement supporting of _inmm.dll alternatively.

        This function only works when -_inmm=yes or -inmm=yes option
is specified at command line or embeded options area.
*/

void DumpHWException() {
    // dummy
}

//---------------------------------------------------------------------------
static void InitRandomGenerator() {
    tjs_uint32 tick = krkr::utils::GetRoughTickCount32();
    krkr::utils::PushEnvironNoise(&tick, sizeof(tick));
    std::thread::id tid = std::this_thread::get_id();
    krkr::utils::PushEnvironNoise(&tid, sizeof(tid));
    time_t curtime = time(nullptr);
    krkr::utils::PushEnvironNoise(&curtime, sizeof(curtime));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// InitializeBaseSystems
//---------------------------------------------------------------------------
void InitializeBaseSystems() {
    // set system archive delimiter
    tTJSVariant v;
    if(GetCommandLine(TJS_W("-arcdelim"), &v))
        krkr::io::archiveDelimiter = ttstr(v)[0];

    // set default current directory
    {
        krkr::io::SetCurrentStorageDirectory(
            IncludeTrailingBackslash(ExtractFileDir(ExePath())));
    }

    // load message map file
    bool load_msgmap = GetSystemSecurityOption("disablemsgmap") == 0;

    if(load_msgmap) {
        const tjs_char name_msgmap[] = TJS_W("msgmap.tjs");
        if(krkr::io::IsExistentStorage(name_msgmap))
            krkr::base::ExecuteStorage(name_msgmap, nullptr, false, TJS_W(""));
    }
}

static tjs_uint64 totalPhysMemory = 0;

static void InitProgramArgumentsAndDataPath(bool stop_after_datapath_got);

void BeforeSystemInit() {
    // RegisterDllLoadHook();
    //  register DLL delayed import hook to support _inmm.dll

    InitProgramArgumentsAndDataPath(false); // ensure command line

    // set system archive delimiter after patch.tjs specified
    tTJSVariant v;
    if(GetCommandLine(TJS_W("-arcdelim"), &v))
        krkr::io::archiveDelimiter = ttstr(v)[0];

    if(krkr::io::IsExistentStorageNoSearchNoNormalize(projectDir)) {
        projectDir += krkr::io::archiveDelimiter;
    } else {
        projectDir += TJS_W("/");
    }
    krkr::io::SetCurrentStorageDirectory(projectDir);
    // randomize
    InitRandomGenerator();

    // memory usage
    {
        MemoryInfo meminf;
        GetMemoryInfo(meminf);
        krkr::utils::PushEnvironNoise(&meminf, sizeof(meminf));

        totalPhysMemory = meminf.MemTotal * 1024;
        if(totalPhysMemory > 768 * 1024 * 1024) {
            totalPhysMemory -=
                512 * 1024 * 1024; // assume that system reserved 512M memory
        } else {
            totalPhysMemory /= 2; // use half memory in small memory devices
        }

        krkr::utils::AddImportantLog(krkr::base::FormatMessage(TVPInfoTotalPhysicalMemory,
                                            tjs_int64(totalPhysMemory)));
        if(totalPhysMemory > 256 * 1024 * 1024) {
            std::string str =
                IndividualConfigManager::GetInstance()->GetValue<std::string>(
                    "memusage", "unlimited");
            if(str == ("low"))
                totalPhysMemory = 0; // assumes zero
            else if(str == ("medium"))
                totalPhysMemory = 128 * 1024 * 1024;
            else if(str == ("high"))
                totalPhysMemory = 256 * 1024 * 1024;
        } else { // use minimum memory usage if less than 256M(512M
                 // physics)
            totalPhysMemory = 0;
        }

        if(totalPhysMemory < 128 * 1024 * 1024) {
            // very very low memory, forcing to assume zero memory
            totalPhysMemory = 0;
        }

        if(totalPhysMemory < 128 * 1024 * 1024) {
            // extra low memory
            if(TJSObjectHashBitsLimit > 0)
                TJSObjectHashBitsLimit = 0;
            krkr::io::segmentCacheLimit = 0;
            // 同步告知 IO 组件：这是**显式覆盖**，优先级高于激活层的策略值
            // （否则策略里的 1 MiB/256 MiB 会把这个“极低内存就关缓存”的决定盖掉）。
            // 状态声明在 StorageIntf.h（krkr::io 块）里，io 只读它——base 不再 include
            // io/policy/IoPolicy.h。
            krkr::io::segmentCacheLimitOverridden = true;
            krkr::io::segmentCacheLimitOverrideBytes = 0;
            TVPFreeUnusedLayerCache = true; // in LayerIntf.cpp
        } else if(totalPhysMemory < 256 * 1024 * 1024) {
            // low memory
            if(TJSObjectHashBitsLimit > 4)
                TJSObjectHashBitsLimit = 4;
        }
    }
}

//---------------------------------------------------------------------------
static void DumpOptions();

//---------------------------------------------------------------------------
extern bool enableGlobalHeapCompaction;

extern void GL_SSE2_Init();

extern "C" void InitGLAsm();
extern bool autoSaveBookMark;
static bool highTimerPeriod = false;
static uint32_t timeBeginPeriodRes = 0;

//---------------------------------------------------------------------------
void AfterSystemInit() {
    // check CPU type
    DetectCPU();

    TVPAllocGraphicCacheOnHeap = false; // always false since beta 20

    // determine maximum graphic cache limit
    tTJSVariant opt;
    tjs_int64 limitmb = -1;
    if(GetCommandLine(TJS_W("-gclim"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("auto"))
            limitmb = -1;
        else
            limitmb = opt.AsInteger();
    }

    if(limitmb == -1) {
        if(totalPhysMemory <= 32 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if(totalPhysMemory <= 48 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if(totalPhysMemory <= 64 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if(totalPhysMemory <= 96 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 4;
        else if(totalPhysMemory <= 128 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 8;
        else if(totalPhysMemory <= 192 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 12;
        else if(totalPhysMemory <= 256 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 20;
        else if(totalPhysMemory <= 512 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 40;
        else
            TVPGraphicCacheSystemLimit =
                tjs_uint64(totalPhysMemory /
                           (1024 * 1024 * 10)); // cachemem = physmem / 10
        TVPGraphicCacheSystemLimit *= 1024 * 1024;
    } else {
        TVPGraphicCacheSystemLimit = limitmb * 1024 * 1024;
    }
    // Cap at 256MB to leave headroom for VRAM, TJS heap, and system on mobile
    if(TVPGraphicCacheSystemLimit >= 256 * 1024 * 1024)
        TVPGraphicCacheSystemLimit = 256 * 1024 * 1024;

    if(totalPhysMemory <= 64 * 1024 * 1024)
        TVPSetFontCacheForLowMem();

    //	TVPGraphicCacheSystemLimit = 1*1024*1024; // DEBUG

    if(GetCommandLine(TJS_W("-autosave"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("yes")) {
            autoSaveBookMark = true;
        }
    }
    // check TVPGraphicSplitOperation option
    // Prefer command-line option set via engine_set_option
    std::string _val;
    tTJSVariant renderer_opt;
    if(GetCommandLine(TJS_W("renderer"), &renderer_opt)) {
        _val = ttstr(renderer_opt).AsStdString();
    }
    if(_val.empty()) {
        _val = IndividualConfigManager::GetInstance()->GetValue<std::string>(
            "renderer", "opengl");
    }
    if(_val != "software") {
        TVPGraphicSplitOperationType = gsotNone;
    } else {
        DrawThreadNum =
            IndividualConfigManager::GetInstance()->GetValue<int>(
                "software_draw_thread", 0);
        if(GetCommandLine(TJS_W("-gsplit"), &opt)) {
            ttstr str(opt);
            if(str == TJS_W("no"))
                TVPGraphicSplitOperationType = gsotNone;
            else if(str == TJS_W("int"))
                TVPGraphicSplitOperationType = gsotInterlace;
            else if(str == TJS_W("yes") || str == TJS_W("simple"))
                TVPGraphicSplitOperationType = gsotSimple;
            else if(str == TJS_W("bidi"))
                TVPGraphicSplitOperationType = gsotBiDirection;
        }
    }

    // check TVPDefaultHoldAlpha option
    if(GetCommandLine(TJS_W("-holdalpha"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("yes") || str == TJS_W("true"))
            TVPDefaultHoldAlpha = true;
        else
            TVPDefaultHoldAlpha = false;
    }

    // check TVPJPEGFastLoad option
    if(GetCommandLine(TJS_W("-jpegdec"),
                         &opt)) // this specifies precision for JPEG decoding
    {
        ttstr str(opt);
        if(str == TJS_W("normal"))
            TVPJPEGLoadPrecision = jlpMedium;
        else if(str == TJS_W("low"))
            TVPJPEGLoadPrecision = jlpLow;
        else if(str == TJS_W("high"))
            TVPJPEGLoadPrecision = jlpHigh;
    }

    // dump option
    DumpOptions();

    // initilaize x86 graphic routines
#if 0
#ifndef TJS_64BIT_OS
    TVPGL_IA32_Init();
#endif
    GL_SSE2_Init();
#endif
    //	InitGLAsm();

    // timer precision
    uint32_t prectick = 1;
    if(GetCommandLine(TJS_W("-timerprec"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("high"))
            prectick = 1;
        if(str == TJS_W("higher"))
            prectick = 5;
        if(str == TJS_W("normal"))
            prectick = 10;
    }

    // draw thread num
    tjs_int drawThreadNum = 0;
    if(GetCommandLine(TJS_W("-drawthread"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("auto"))
            drawThreadNum = 0;
        else
            drawThreadNum = (tjs_int)opt;
    }
    DrawThreadNum = drawThreadNum;
}

//---------------------------------------------------------------------------
void BeforeSystemUninit() {
    // DumpHWException(); // dump cached hw exceptoin
}

//---------------------------------------------------------------------------
void AfterSystemUninit() {}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool terminated = false;
// 终止是否由"游戏关掉了自己的窗口"引起（与脚本直接 System.exit() 区分开）：
// 前者窗口已经没了，宿主即便确认"继续游戏"也只是在空场景上继续跑（实测就是卡死），
// engine_tick 会用不同的结果码告诉宿主，见 ENGINE_RESULT_WINDOW_CLOSED。
bool terminateWindowClosed = false;
bool terminateOnWindowClose = true;
bool terminateOnNoWindowStartup = true;
int terminateCode = 0;
bool hostSuppressProcessExit = false;

//---------------------------------------------------------------------------
// 关窗确认闸门状态（见 SysInitImpl.h 的说明）。原子即可：写方是宿主线程
// （engine_resolve_window_close，其实也就是 tick 的 owner 线程）与渲染线程。
namespace krkr::base {
namespace {
std::atomic<bool> g_deferWindowClose{ false };
std::atomic<bool> g_windowClosePending{ false };
std::atomic<int64_t> g_windowCloseRequestMs{ 0 };

// 宿主一直不回答时的兜底：宁可自己关掉，也不要把游戏永久钉在挂起状态。
constexpr int64_t kWindowCloseFallbackMs = 20000;

int64_t NowSteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

void SetDeferWindowClose(bool defer) {
    g_deferWindowClose.store(defer, std::memory_order_relaxed);
}

bool DeferWindowClose() {
    return g_deferWindowClose.load(std::memory_order_relaxed);
}

bool RequestWindowClose() {
    if(!g_deferWindowClose.load(std::memory_order_relaxed))
        return false;
    // 已经挂起时同样返回 true：调用方（HostWindowLayer::Close 的第二次调用）
    // 也不能再把窗口拆掉。
    bool expected = false;
    if(g_windowClosePending.compare_exchange_strong(expected, true)) {
        g_windowCloseRequestMs.store(NowSteadyMs(), std::memory_order_relaxed);
        spdlog::info("host: 游戏请求关闭窗口，已挂起等宿主确认（engine_tick 返回 "
                     "WINDOW_CLOSE_REQUESTED）");
    }
    return true;
}

bool WindowClosePending() {
    if(!g_windowClosePending.load(std::memory_order_relaxed))
        return false;
    const int64_t requested = g_windowCloseRequestMs.load(std::memory_order_relaxed);
    if(NowSteadyMs() - requested > kWindowCloseFallbackMs) {
        spdlog::warn("host: 关窗请求 {}ms 无人确认，兜底执行关窗终止",
                     static_cast<long long>(NowSteadyMs() - requested));
        ConfirmWindowClose();
        return false;
    }
    return true;
}

void CancelWindowClose() {
    if(g_windowClosePending.exchange(false))
        spdlog::info("host: 宿主选择继续游戏，已撤销未决的关窗请求");
}

void ConfirmWindowClose() {
    g_windowClosePending.store(false, std::memory_order_relaxed);
    // 不走 HostWindowLayer::Close()（那会再次撞上闸门）：宿主已经确认，
    // 这里直接按"关窗退出"处理，engine_tick 下一帧会报 WINDOW_CLOSED。
    terminateWindowClosed = true;
    krkr::base::TerminateAsync(0);
}

// 模态对话框的输入泵（见头文件说明）。回调由 engine_api 注册；泵本身不持有
// 任何引擎状态，只在引擎线程（模态循环所在线程）里被调用。
namespace {
std::mutex g_modalPumpMutex;
ModalInputPump g_modalInputPump;
} // namespace

void SetModalInputPump(ModalInputPump pump) {
    std::lock_guard<std::mutex> lk(g_modalPumpMutex);
    g_modalInputPump = std::move(pump);
}

void PumpModalInput() {
    ModalInputPump pump;
    {
        std::lock_guard<std::mutex> lk(g_modalPumpMutex);
        pump = g_modalInputPump;
    }
    if(pump)
        pump();
}
} // namespace krkr::base

//---------------------------------------------------------------------------
void krkr::base::TerminateAsync(int code) {
    // do "A"synchronous temination of application
    terminated = true;
    terminateCode = code;

    // posting dummy message will prevent "missing WM_QUIT bug" in
    // Direct3D framework.
    if(systemControl)
        systemControl->CallDeliverAllEventsOnIdle();

    Application->Terminate();

    if(systemControl)
        systemControl->CallDeliverAllEventsOnIdle();
}

//---------------------------------------------------------------------------
void TerminateSync(int code) {
    // do synchronous temination of application (never return)
    if(hostSuppressProcessExit) {
        // In embedded host mode, calling SystemUninit() here
        // would destroy the TJS engine while still inside a TJS call stack,
        // causing undefined behavior (hang/crash) since exit() is suppressed.
        // Instead, mark as terminated and throw EAbort to safely unwind the
        // stack back to Application->Run() which catches EAbort.
        krkr::base::TerminateAsync(code);
        throw EAbort(TJS_W("application exit"));
    }
    SystemUninit();
    ExitApplication(code);
}

//---------------------------------------------------------------------------
void MainWindowClosed() {
    // called from WindowIntf.cpp, caused by closing all window.
    if(terminateOnWindowClose) {
        // 记下"由窗口关闭引起"：宿主据此不提供"继续游戏"（窗口已经没了）。
        // 这条日志用来区分两种关窗来路：Window.close()（走 HostWindowLayer::Close，
        // 可被确认闸门拦下）与窗口注销（走这里，窗口已经注销，拦不住）。
        spdlog::debug("MainWindowClosed: 主窗口已注销，按关窗退出处理");
        terminateWindowClosed = true;
        krkr::base::TerminateAsync();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// GetCommandLine
//---------------------------------------------------------------------------
static std::vector<std::string> *GetEmbeddedOptions() { return nullptr; }

//---------------------------------------------------------------------------
static std::vector<std::string> *
TVPGetConfigFileOptions(const ttstr &filename) {
    return nullptr;
}

static ttstr ParseCommandLineOne(const ttstr &i) {
    // value is specified
    const tjs_char *p, *o;
    p = o = i.c_str();
    p = TJS_strchr(p, '=');

    if(p == nullptr) {
        return i + TJS_W("=yes");
    }

    p++;

    ttstr optname(o, (int)(p - o));

    if(*p == TJS_W('\'') || *p == TJS_W('\"')) {
        // as an escaped string
        tTJSVariant v;
        TJSParseString(v, &p);

        return optname + ttstr(v);
    } else {
        // as a string
        return optname + p;
    }
}

//---------------------------------------------------------------------------
std::vector<ttstr> programArguments;
static bool programArgumentsInit = false;
static tjs_int commandLineArgumentGeneration = 0;
static bool dataPathDirectoryEnsured = false;

//---------------------------------------------------------------------------
tjs_int GetCommandLineArgumentGeneration() {
    return commandLineArgumentGeneration;
}

//---------------------------------------------------------------------------
void EnsureDataPathDirectory() {
    if(!dataPathDirectoryEnsured) {
        dataPathDirectoryEnsured = true;
        // ensure data path existence
        if(!krkr::io::CheckExistentLocalFolder(nativeDataPath.c_str())) {
            if(TVPCreateFolders(nativeDataPath.c_str()))
                krkr::utils::AddImportantLog(
                    krkr::base::FormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                     (const tjs_char *)TVPOk));
            else
                krkr::utils::AddImportantLog(
                    krkr::base::FormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                     (const tjs_char *)TVPFaild));
        }
    }
}

//---------------------------------------------------------------------------
static void PushAllCommandlineArguments() {}

//---------------------------------------------------------------------------
static void PushConfigFileOptions(const std::vector<std::string> *options) {
    if(!options)
        return;
    for(const auto &option : *options) {
        if(option.c_str()[0] != ';') // unless comment
            programArguments.push_back(
                ParseCommandLineOne(TJS_W("-") + ttstr(option.c_str())));
    }
}

//---------------------------------------------------------------------------
// Options set via engine_set_option before programArguments is initialized
static std::vector<std::pair<ttstr, ttstr>> earlySetOptions;

static void InitProgramArgumentsAndDataPath(bool stop_after_datapath_got) {
    if(!programArgumentsInit) {
        programArgumentsInit = true;

        // find options from self executable image
        const int num_option_layers = 3;
        std::vector<std::string> *options[num_option_layers];
        for(auto &option : options)
            option = nullptr;
        try {
            // read embedded options and default configuration file
            options[0] = GetEmbeddedOptions();
            //			options[1] =
            // TVPGetConfigFileOptions(ApplicationSpecialPath::GetConfigFileName(ExePath()));

            // at this point, we need to push all exsting known
            // options to be able to see datapath
            PushAllCommandlineArguments();
            PushConfigFileOptions(options[1]); // has more priority
            PushConfigFileOptions(options[0]); // has lesser priority

            // read datapath
            tTJSVariant val;
            ttstr config_datapath;
            // 			if(GetCommandLine(TJS_W("-datapath"),
            // &val)) 				config_datapath =
            // ((ttstr)val).AsStdString();
            nativeDataPath = ApplicationSpecialPath::GetDataPathDirectory(
                config_datapath, ExePath());

            if(stop_after_datapath_got)
                return;

            // read per-user configuration file
            //			options[2] =
            // TVPGetConfigFileOptions(ApplicationSpecialPath::GetUserConfigFileName(config_datapath,
            // ExePath()));

            // push each options into option stock
            // we need to clear programArguments first because of
            // the option priority order.
            programArguments.clear();
            PushAllCommandlineArguments();
            PushConfigFileOptions(options[2]); // has more priority
            PushConfigFileOptions(options[1]); // has more priority
            PushConfigFileOptions(options[0]); // has lesser priority
        } catch(...) {
            for(auto &option : options)
                if(option)
                    delete option;
            throw;
        }
        for(auto &option : options)
            if(option)
                delete option;

        // set data path
        dataPath = krkr::io::NormalizeStorageName(nativeDataPath);
        krkr::utils::AddImportantLog(krkr::base::FormatMessage(TVPInfoDataPath, dataPath));

        // set log output directory
        krkr::utils::SetLogLocation(nativeDataPath);

        // merge early-set options (from engine_set_option before init)
        for(auto it = earlySetOptions.rbegin();
            it != earlySetOptions.rend(); ++it) {
            programArguments.insert(programArguments.begin(),
                                       it->first + TJS_W("=") + it->second);
        }
        earlySetOptions.clear();

        // increment commandLineArgumentGeneration
        commandLineArgumentGeneration++;
    }
}

//---------------------------------------------------------------------------
static void DumpOptions() {
    std::vector<ttstr>::const_iterator i;
    ttstr options(TVPInfoSpecifiedOptionEarlierItemHasMorePriority);
    if(programArguments.size()) {
        for(i = programArguments.begin(); i != programArguments.end();
            i++) {
            options += TJS_W(" ");
            options += *i;
        }
    } else {
        options += (const tjs_char *)TVPNone;
    }
    krkr::utils::AddImportantLog(options);
}

//---------------------------------------------------------------------------
bool GetCommandLine(const tjs_char *name, tTJSVariant *value) {
    InitProgramArgumentsAndDataPath(false);

    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::const_iterator i;
    for(i = programArguments.begin(); i != programArguments.end(); i++) {
        if(!TJS_strncmp(i->c_str(), name, namelen)) {
            if(i->c_str()[namelen] == TJS_W('=')) {
                // value is specified
                const tjs_char *p = i->c_str() + namelen + 1;
                if(value)
                    *value = p;
                return true;
            } else if(i->c_str()[namelen] == 0) {
                // value is not specified
                if(value)
                    *value = TJS_W("yes");
                return true;
            }
        }
    }
    return false;
}

//---------------------------------------------------------------------------
void krkr::base::SetCommandLine(const tjs_char *name, const ttstr &value) {
    // If not yet initialized, store in early options to be merged after init
    if(!programArgumentsInit) {
        ttstr nameStr(name);
        // Update existing early option or add new one
        for(auto &opt : earlySetOptions) {
            if(opt.first == nameStr) {
                opt.second = value;
                return;
            }
        }
        earlySetOptions.push_back({ nameStr, value });
        return;
    }

    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::iterator i;
    for(i = programArguments.begin(); i != programArguments.end(); i++) {
        if(!TJS_strncmp(i->c_str(), name, namelen)) {
            if(i->c_str()[namelen] == TJS_W('=') || i->c_str()[namelen] == 0) {
                // value found
                *i = ttstr(i->c_str(), namelen) + TJS_W("=") + value;
                commandLineArgumentGeneration++;
                if(commandLineArgumentGeneration == 0)
                    commandLineArgumentGeneration = 1;
                return;
            }
        }
    }

    // value not found; insert argument into front
    programArguments.insert(programArguments.begin(),
                               ttstr(name) + TJS_W("=") + value);
    commandLineArgumentGeneration++;
    if(commandLineArgumentGeneration == 0)
        commandLineArgumentGeneration = 1;
}

bool CheckPrintDataPath() { return false; }

bool CheckAbout() { return false; }

static void ExecuteAsync(const std::wstring &progname) {}

static bool WaitWritePermit(const std::wstring &fn) { return false; }

bool ExecuteUserConfig() { return false; }

//---------------------------------------------------------------------------
// ResetSysInitImplForRestart : Reset impl-level state for restart
//---------------------------------------------------------------------------
extern bool systemControlAlive; // SystemControl.cpp

void ResetSysInitImplForRestart() {
    programArgumentsInit = false;
    dataPathDirectoryEnsured = false;
    programArguments.clear();
    earlySetOptions.clear();
    commandLineArgumentGeneration = 0;

    terminated = false;
    terminateWindowClosed = false;
    terminateCode = 0;

    // 上一局的未决关窗请求不能带到新启动的引擎里（否则新游戏一 tick 就被报成
    // "请求关窗"）。闸门开关由宿主在 engine_create 里重新打开。
    krkr::base::CancelWindowClose();

    projectDirSelected = false;
    nativeProjectDir.Clear();
    nativeDataPath.Clear();

    // Reset SystemControl alive flag (set in SystemControl constructor,
    // never cleared by destructor).
    systemControlAlive = false;
}
//---------------------------------------------------------------------------
