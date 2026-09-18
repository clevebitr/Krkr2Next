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
ttstr TVPNativeProjectDir;
ttstr TVPNativeDataPath;
bool TVPProjectDirSelected = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// System security options
//---------------------------------------------------------------------------
// system security options are held inside the executable, where
// signature checker will refer. This enables the signature checker
// (or other security modules like XP3 encryption module) to check
// the changes which is not intended by the contents author.
const static char TVPSystemSecurityOptions[] =
    "-- TVPSystemSecurityOptions "
    "disablemsgmap(0):forcedataxp3(0):acceptfilenameargument(0) --";

//---------------------------------------------------------------------------
int GetSystemSecurityOption(const char *name) {
    size_t namelen = TJS_nstrlen(name);
    const char *p = TJS_nstrstr(TVPSystemSecurityOptions, name);
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

void TVPDumpHWException() {
    // dummy
}

//---------------------------------------------------------------------------
static void TVPInitRandomGenerator() {
    tjs_uint32 tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
    std::thread::id tid = std::this_thread::get_id();
    TVPPushEnvironNoise(&tid, sizeof(tid));
    time_t curtime = time(nullptr);
    TVPPushEnvironNoise(&curtime, sizeof(curtime));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitializeBaseSystems
//---------------------------------------------------------------------------
void TVPInitializeBaseSystems() {
    // set system archive delimiter
    tTJSVariant v;
    if(TVPGetCommandLine(TJS_W("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

    // set default current directory
    {
        TVPSetCurrentDirectory(
            IncludeTrailingBackslash(ExtractFileDir(ExePath())));
    }

    // load message map file
    bool load_msgmap = GetSystemSecurityOption("disablemsgmap") == 0;

    if(load_msgmap) {
        const tjs_char name_msgmap[] = TJS_W("msgmap.tjs");
        if(TVPIsExistentStorage(name_msgmap))
            TVPExecuteStorage(name_msgmap, nullptr, false, TJS_W(""));
    }
}

static tjs_uint64 TVPTotalPhysMemory = 0;

static void TVPInitProgramArgumentsAndDataPath(bool stop_after_datapath_got);

void TVPBeforeSystemInit() {
    // RegisterDllLoadHook();
    //  register DLL delayed import hook to support _inmm.dll

    TVPInitProgramArgumentsAndDataPath(false); // ensure command line

    // set system archive delimiter after patch.tjs specified
    tTJSVariant v;
    if(TVPGetCommandLine(TJS_W("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

    if(TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir)) {
        TVPProjectDir += TVPArchiveDelimiter;
    } else {
        TVPProjectDir += TJS_W("/");
    }
    TVPSetCurrentDirectory(TVPProjectDir);
    // randomize
    TVPInitRandomGenerator();

    // memory usage
    {
        TVPMemoryInfo meminf;
        TVPGetMemoryInfo(meminf);
        TVPPushEnvironNoise(&meminf, sizeof(meminf));

        TVPTotalPhysMemory = meminf.MemTotal * 1024;
        if(TVPTotalPhysMemory > 768 * 1024 * 1024) {
            TVPTotalPhysMemory -=
                512 * 1024 * 1024; // assume that system reserved 512M memory
        } else {
            TVPTotalPhysMemory /= 2; // use half memory in small memory devices
        }

        TVPAddImportantLog(TVPFormatMessage(TVPInfoTotalPhysicalMemory,
                                            tjs_int64(TVPTotalPhysMemory)));
        if(TVPTotalPhysMemory > 256 * 1024 * 1024) {
            std::string str =
                IndividualConfigManager::GetInstance()->GetValue<std::string>(
                    "memusage", "unlimited");
            if(str == ("low"))
                TVPTotalPhysMemory = 0; // assumes zero
            else if(str == ("medium"))
                TVPTotalPhysMemory = 128 * 1024 * 1024;
            else if(str == ("high"))
                TVPTotalPhysMemory = 256 * 1024 * 1024;
        } else { // use minimum memory usage if less than 256M(512M
                 // physics)
            TVPTotalPhysMemory = 0;
        }

        if(TVPTotalPhysMemory < 128 * 1024 * 1024) {
            // very very low memory, forcing to assume zero memory
            TVPTotalPhysMemory = 0;
        }

        if(TVPTotalPhysMemory < 128 * 1024 * 1024) {
            // extra low memory
            if(TJSObjectHashBitsLimit > 0)
                TJSObjectHashBitsLimit = 0;
            TVPSegmentCacheLimit = 0;
            TVPFreeUnusedLayerCache = true; // in LayerIntf.cpp
        } else if(TVPTotalPhysMemory < 256 * 1024 * 1024) {
            // low memory
            if(TJSObjectHashBitsLimit > 4)
                TJSObjectHashBitsLimit = 4;
        }
    }
}

//---------------------------------------------------------------------------
static void TVPDumpOptions();

//---------------------------------------------------------------------------
extern bool TVPEnableGlobalHeapCompaction;

extern void TVPGL_SSE2_Init();

extern "C" void TVPGL_ASM_Init();
extern bool TVPAutoSaveBookMark;
static bool TVPHighTimerPeriod = false;
static uint32_t TVPTimeBeginPeriodRes = 0;

//---------------------------------------------------------------------------
void TVPAfterSystemInit() {
    // check CPU type
    TVPDetectCPU();

    TVPAllocGraphicCacheOnHeap = false; // always false since beta 20

    // determine maximum graphic cache limit
    tTJSVariant opt;
    tjs_int64 limitmb = -1;
    if(TVPGetCommandLine(TJS_W("-gclim"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("auto"))
            limitmb = -1;
        else
            limitmb = opt.AsInteger();
    }

    if(limitmb == -1) {
        if(TVPTotalPhysMemory <= 32 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if(TVPTotalPhysMemory <= 48 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if(TVPTotalPhysMemory <= 64 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if(TVPTotalPhysMemory <= 96 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 4;
        else if(TVPTotalPhysMemory <= 128 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 8;
        else if(TVPTotalPhysMemory <= 192 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 12;
        else if(TVPTotalPhysMemory <= 256 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 20;
        else if(TVPTotalPhysMemory <= 512 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 40;
        else
            TVPGraphicCacheSystemLimit =
                tjs_uint64(TVPTotalPhysMemory /
                           (1024 * 1024 * 10)); // cachemem = physmem / 10
        TVPGraphicCacheSystemLimit *= 1024 * 1024;
    } else {
        TVPGraphicCacheSystemLimit = limitmb * 1024 * 1024;
    }
    // Cap at 256MB to leave headroom for VRAM, TJS heap, and system on mobile
    if(TVPGraphicCacheSystemLimit >= 256 * 1024 * 1024)
        TVPGraphicCacheSystemLimit = 256 * 1024 * 1024;

    if(TVPTotalPhysMemory <= 64 * 1024 * 1024)
        TVPSetFontCacheForLowMem();

    //	TVPGraphicCacheSystemLimit = 1*1024*1024; // DEBUG

    if(TVPGetCommandLine(TJS_W("-autosave"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("yes")) {
            TVPAutoSaveBookMark = true;
        }
    }
    // check TVPGraphicSplitOperation option
    // Prefer command-line option set via engine_set_option
    std::string _val;
    tTJSVariant renderer_opt;
    if(TVPGetCommandLine(TJS_W("renderer"), &renderer_opt)) {
        _val = ttstr(renderer_opt).AsStdString();
    }
    if(_val.empty()) {
        _val = IndividualConfigManager::GetInstance()->GetValue<std::string>(
            "renderer", "opengl");
    }
    if(_val != "software") {
        TVPGraphicSplitOperationType = gsotNone;
    } else {
        TVPDrawThreadNum =
            IndividualConfigManager::GetInstance()->GetValue<int>(
                "software_draw_thread", 0);
        if(TVPGetCommandLine(TJS_W("-gsplit"), &opt)) {
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
    if(TVPGetCommandLine(TJS_W("-holdalpha"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("yes") || str == TJS_W("true"))
            TVPDefaultHoldAlpha = true;
        else
            TVPDefaultHoldAlpha = false;
    }

    // check TVPJPEGFastLoad option
    if(TVPGetCommandLine(TJS_W("-jpegdec"),
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
    TVPDumpOptions();

    // initilaize x86 graphic routines
#if 0
#ifndef TJS_64BIT_OS
    TVPGL_IA32_Init();
#endif
    TVPGL_SSE2_Init();
#endif
    //	TVPGL_ASM_Init();

    // timer precision
    uint32_t prectick = 1;
    if(TVPGetCommandLine(TJS_W("-timerprec"), &opt)) {
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
    if(TVPGetCommandLine(TJS_W("-drawthread"), &opt)) {
        ttstr str(opt);
        if(str == TJS_W("auto"))
            drawThreadNum = 0;
        else
            drawThreadNum = (tjs_int)opt;
    }
    TVPDrawThreadNum = drawThreadNum;
}

//---------------------------------------------------------------------------
void TVPBeforeSystemUninit() {
    // TVPDumpHWException(); // dump cached hw exceptoin
}

//---------------------------------------------------------------------------
void TVPAfterSystemUninit() {}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVPTerminated = false;
// 终止是否由"游戏关掉了自己的窗口"引起（与脚本直接 System.exit() 区分开）：
// 前者窗口已经没了，宿主即便确认"继续游戏"也只是在空场景上继续跑（实测就是卡死），
// engine_tick 会用不同的结果码告诉宿主，见 ENGINE_RESULT_WINDOW_CLOSED。
bool TVPTerminateWindowClosed = false;
bool TVPTerminateOnWindowClose = true;
bool TVPTerminateOnNoWindowStartup = true;
int TVPTerminateCode = 0;
bool TVPHostSuppressProcessExit = false;

//---------------------------------------------------------------------------
// 关窗确认闸门状态（见 SysInitImpl.h 的说明）。原子即可：写方是宿主线程
// （engine_resolve_window_close，其实也就是 tick 的 owner 线程）与渲染线程。
namespace krkr::host {
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
    TVPTerminateWindowClosed = true;
    TVPTerminateAsync(0);
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
} // namespace krkr::host

//---------------------------------------------------------------------------
void TVPTerminateAsync(int code) {
    // do "A"synchronous temination of application
    TVPTerminated = true;
    TVPTerminateCode = code;

    // posting dummy message will prevent "missing WM_QUIT bug" in
    // Direct3D framework.
    if(TVPSystemControl)
        TVPSystemControl->CallDeliverAllEventsOnIdle();

    Application->Terminate();

    if(TVPSystemControl)
        TVPSystemControl->CallDeliverAllEventsOnIdle();
}

//---------------------------------------------------------------------------
void TVPTerminateSync(int code) {
    // do synchronous temination of application (never return)
    if(TVPHostSuppressProcessExit) {
        // In embedded host mode, calling TVPSystemUninit() here
        // would destroy the TJS engine while still inside a TJS call stack,
        // causing undefined behavior (hang/crash) since exit() is suppressed.
        // Instead, mark as terminated and throw EAbort to safely unwind the
        // stack back to Application->Run() which catches EAbort.
        TVPTerminateAsync(code);
        throw EAbort(TJS_W("application exit"));
    }
    TVPSystemUninit();
    TVPExitApplication(code);
}

//---------------------------------------------------------------------------
void TVPMainWindowClosed() {
    // called from WindowIntf.cpp, caused by closing all window.
    if(TVPTerminateOnWindowClose) {
        // 记下"由窗口关闭引起"：宿主据此不提供"继续游戏"（窗口已经没了）。
        // 这条日志用来区分两种关窗来路：Window.close()（走 HostWindowLayer::Close，
        // 可被确认闸门拦下）与窗口注销（走这里，窗口已经注销，拦不住）。
        spdlog::debug("TVPMainWindowClosed: 主窗口已注销，按关窗退出处理");
        TVPTerminateWindowClosed = true;
        TVPTerminateAsync();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// GetCommandLine
//---------------------------------------------------------------------------
static std::vector<std::string> *TVPGetEmbeddedOptions() { return nullptr; }

//---------------------------------------------------------------------------
static std::vector<std::string> *
TVPGetConfigFileOptions(const ttstr &filename) {
    return nullptr;
}

static ttstr TVPParseCommandLineOne(const ttstr &i) {
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
std::vector<ttstr> TVPProgramArguments;
static bool TVPProgramArgumentsInit = false;
static tjs_int TVPCommandLineArgumentGeneration = 0;
static bool TVPDataPathDirectoryEnsured = false;

//---------------------------------------------------------------------------
tjs_int TVPGetCommandLineArgumentGeneration() {
    return TVPCommandLineArgumentGeneration;
}

//---------------------------------------------------------------------------
void TVPEnsureDataPathDirectory() {
    if(!TVPDataPathDirectoryEnsured) {
        TVPDataPathDirectoryEnsured = true;
        // ensure data path existence
        if(!TVPCheckExistentLocalFolder(TVPNativeDataPath.c_str())) {
            if(TVPCreateFolders(TVPNativeDataPath.c_str()))
                TVPAddImportantLog(
                    TVPFormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                     (const tjs_char *)TVPOk));
            else
                TVPAddImportantLog(
                    TVPFormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
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
            TVPProgramArguments.push_back(
                TVPParseCommandLineOne(TJS_W("-") + ttstr(option.c_str())));
    }
}

//---------------------------------------------------------------------------
// Options set via engine_set_option before TVPProgramArguments is initialized
static std::vector<std::pair<ttstr, ttstr>> TVPEarlySetOptions;

static void TVPInitProgramArgumentsAndDataPath(bool stop_after_datapath_got) {
    if(!TVPProgramArgumentsInit) {
        TVPProgramArgumentsInit = true;

        // find options from self executable image
        const int num_option_layers = 3;
        std::vector<std::string> *options[num_option_layers];
        for(auto &option : options)
            option = nullptr;
        try {
            // read embedded options and default configuration file
            options[0] = TVPGetEmbeddedOptions();
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
            // 			if(TVPGetCommandLine(TJS_W("-datapath"),
            // &val)) 				config_datapath =
            // ((ttstr)val).AsStdString();
            TVPNativeDataPath = ApplicationSpecialPath::GetDataPathDirectory(
                config_datapath, ExePath());

            if(stop_after_datapath_got)
                return;

            // read per-user configuration file
            //			options[2] =
            // TVPGetConfigFileOptions(ApplicationSpecialPath::GetUserConfigFileName(config_datapath,
            // ExePath()));

            // push each options into option stock
            // we need to clear TVPProgramArguments first because of
            // the option priority order.
            TVPProgramArguments.clear();
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
        TVPDataPath = TVPNormalizeStorageName(TVPNativeDataPath);
        TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPath, TVPDataPath));

        // set log output directory
        TVPSetLogLocation(TVPNativeDataPath);

        // merge early-set options (from engine_set_option before init)
        for(auto it = TVPEarlySetOptions.rbegin();
            it != TVPEarlySetOptions.rend(); ++it) {
            TVPProgramArguments.insert(TVPProgramArguments.begin(),
                                       it->first + TJS_W("=") + it->second);
        }
        TVPEarlySetOptions.clear();

        // increment TVPCommandLineArgumentGeneration
        TVPCommandLineArgumentGeneration++;
    }
}

//---------------------------------------------------------------------------
static void TVPDumpOptions() {
    std::vector<ttstr>::const_iterator i;
    ttstr options(TVPInfoSpecifiedOptionEarlierItemHasMorePriority);
    if(TVPProgramArguments.size()) {
        for(i = TVPProgramArguments.begin(); i != TVPProgramArguments.end();
            i++) {
            options += TJS_W(" ");
            options += *i;
        }
    } else {
        options += (const tjs_char *)TVPNone;
    }
    TVPAddImportantLog(options);
}

//---------------------------------------------------------------------------
bool TVPGetCommandLine(const tjs_char *name, tTJSVariant *value) {
    TVPInitProgramArgumentsAndDataPath(false);

    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::const_iterator i;
    for(i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++) {
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
void TVPSetCommandLine(const tjs_char *name, const ttstr &value) {
    // If not yet initialized, store in early options to be merged after init
    if(!TVPProgramArgumentsInit) {
        ttstr nameStr(name);
        // Update existing early option or add new one
        for(auto &opt : TVPEarlySetOptions) {
            if(opt.first == nameStr) {
                opt.second = value;
                return;
            }
        }
        TVPEarlySetOptions.push_back({ nameStr, value });
        return;
    }

    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::iterator i;
    for(i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++) {
        if(!TJS_strncmp(i->c_str(), name, namelen)) {
            if(i->c_str()[namelen] == TJS_W('=') || i->c_str()[namelen] == 0) {
                // value found
                *i = ttstr(i->c_str(), namelen) + TJS_W("=") + value;
                TVPCommandLineArgumentGeneration++;
                if(TVPCommandLineArgumentGeneration == 0)
                    TVPCommandLineArgumentGeneration = 1;
                return;
            }
        }
    }

    // value not found; insert argument into front
    TVPProgramArguments.insert(TVPProgramArguments.begin(),
                               ttstr(name) + TJS_W("=") + value);
    TVPCommandLineArgumentGeneration++;
    if(TVPCommandLineArgumentGeneration == 0)
        TVPCommandLineArgumentGeneration = 1;
}

bool TVPCheckPrintDataPath() { return false; }

bool TVPCheckAbout() { return false; }

static void TVPExecuteAsync(const std::wstring &progname) {}

static bool TVPWaitWritePermit(const std::wstring &fn) { return false; }

bool TVPExecuteUserConfig() { return false; }

//---------------------------------------------------------------------------
// TVPResetSysInitImplForRestart : Reset impl-level state for restart
//---------------------------------------------------------------------------
extern bool TVPSystemControlAlive; // SystemControl.cpp

void TVPResetSysInitImplForRestart() {
    TVPProgramArgumentsInit = false;
    TVPDataPathDirectoryEnsured = false;
    TVPProgramArguments.clear();
    TVPEarlySetOptions.clear();
    TVPCommandLineArgumentGeneration = 0;

    TVPTerminated = false;
    TVPTerminateWindowClosed = false;
    TVPTerminateCode = 0;

    // 上一局的未决关窗请求不能带到新启动的引擎里（否则新游戏一 tick 就被报成
    // "请求关窗"）。闸门开关由宿主在 engine_create 里重新打开。
    krkr::host::CancelWindowClose();

    TVPProjectDirSelected = false;
    TVPNativeProjectDir.Clear();
    TVPNativeDataPath.Clear();

    // Reset SystemControl alive flag (set in tTVPSystemControl constructor,
    // never cleared by destructor).
    TVPSystemControlAlive = false;
}
//---------------------------------------------------------------------------
