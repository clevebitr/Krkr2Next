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

#include <vector>
#include <algorithm>
#include <functional>

#include <dlfcn.h> // dladdr：把 at-exit handler 函数指针解析为符号名，便于真机定位

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "tjsUtils.h"
#include "SysInitIntf.h"
#include "ScriptMgnIntf.h"
#include "KAGParser.h"
#include "tvpgl.h"
#include <spdlog/spdlog.h>

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
namespace krkr::base {

ttstr projectDir; // project directory (in unified storage name)
ttstr dataPath; // data directory (in unified storage name)

} // namespace krkr::base
//---------------------------------------------------------------------------

extern void GL_C_Init();

// krkr::sound::StopAllWaveSoundsForRestart 定义在 sound 模块的全局作用域。块作用域的
// extern 声明在 namespace 内会绑到该 namespace，所以提到文件作用域。
extern void krkr::sound::StopAllWaveSoundsForRestart();

namespace krkr::base {

//---------------------------------------------------------------------------
// SystemInit : Entire System Initialization
//---------------------------------------------------------------------------
void SystemInit() {
#ifdef _WIN32
#ifdef USING_PROTECT
    while(!TVPProtectInit()) {
        TVPUpdateLicense();
    }
#endif
#endif

    BeforeSystemInit();

    TVPInitScriptEngine();

    TVPInitTVPGL();
    //	GL_C_Init();

    AfterSystemInit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// SystemUninit : System shutdown, cleanup, etc...
//---------------------------------------------------------------------------
static void CauseAtExit();

bool systemUninitCalled = false;

void SystemUninit() {
    if(systemUninitCalled)
        return;
    systemUninitCalled = true;

    BeforeSystemUninit();

    TVPUninitTVPGL();

    try {
        TVPUninitScriptEngine();
    } catch(...) {
        // ignore errors
    }

    AfterSystemUninit();

    CauseAtExit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// AddAtExitHandler related
//---------------------------------------------------------------------------
struct AtExitInfo {
    AtExitInfo(tjs_int pri, void (*handler)()) {
        Priority = pri, Handler = handler;
    }

    tjs_int Priority;

    void (*Handler)();

    bool operator<(const AtExitInfo &r) const {
        return this->Priority < r.Priority;
    }

    bool operator>(const AtExitInfo &r) const {
        return this->Priority > r.Priority;
    }

    bool operator==(const AtExitInfo &r) const {
        return this->Priority == r.Priority;
    }
};

static std::vector<AtExitInfo> *atExitInfos = nullptr;
static bool atExitShutdown = false;

//---------------------------------------------------------------------------
void AddAtExitHandler(tjs_int pri, void (*handler)()) {
    if(atExitShutdown)
        return;

    if(!atExitInfos)
        atExitInfos = new std::vector<AtExitInfo>();
    atExitInfos->emplace_back(pri, handler);
}

//---------------------------------------------------------------------------
static void CauseAtExit() {
    // 修正 runtime-restart：tTVPAtExit 文件级 static 对象只在进程启动注册一次，
    // 首次 engine_destroy 的 CauseAtExit 已把 atExitInfos delete
    // 置空；二次 engine_destroy（ResetRuntimeForRestart
    // 复位后会再次进入）时为空，不能解引用。
    if(atExitShutdown || !atExitInfos)
        return;
    atExitShutdown = true;

    std::sort(atExitInfos->begin(),
              atExitInfos->end()); // descending sort

    // 逐 handler 打点：真机退出卡死时据最后一条日志定位卡在哪个 at-exit
    // handler。
    tjs_uint idx = 0;
    for(auto i = atExitInfos->begin(); i != atExitInfos->end();
        ++i, ++idx) {
        // dladdr 把 handler 函数指针解析为符号名（release/strip 后可能为 "?"）
        const char *sym = "?";
        Dl_info inf;
        if(dladdr(reinterpret_cast<void *>(i->Handler), &inf) &&
           inf.dli_sname && inf.dli_sname[0])
            sym = inf.dli_sname;
        spdlog::info("CauseAtExit: handler[{}] pri={} sym={} begin", idx,
                     i->Priority, sym);
        spdlog::default_logger()->flush();
        i->Handler();
        spdlog::info("CauseAtExit: handler[{}] pri={} sym={} end", idx,
                     i->Priority, sym);
        spdlog::default_logger()->flush();
    }

    delete atExitInfos;
    atExitInfos = nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// ResetRuntimeForRestart : Reset state to allow re-initialization
//---------------------------------------------------------------------------
void ResetRuntimeForRestart() {
    systemUninitCalled = false;
    atExitShutdown = false;
    // atExitInfos was deleted by CauseAtExit(); leave it null so
    // AddAtExitHandler will re-create it on next startup.
    atExitInfos = nullptr;
    projectDir.Clear();
    dataPath.Clear();
    // TVPScenarioCache 以场景短名（如 "first.ks"）为 key 的进程级缓存，restart
    // 不随 VM 销毁； 换游戏后下一个游戏 kag.loadScenario
    // 会命中前一个游戏的内容（跨游戏同名场景碰撞），
    // 此处清空。同游戏复开重读缓存为空后正常重建，仅损失一次解压。
    TVPClearScnearioCache();

    // tTVPAtExit 一次性、首次已消费 → 重启不再执行
    // ShutdownWaveSoundBuffers，上一游戏的 BGM/音效由 krkr::sound::waveSoundBufferThread
    // 继续混音输出、叠进下一游戏。这里显式停掉。 （实现见
    // cpp/core/sound/impl/WaveImpl.cpp krkr::sound::StopAllWaveSoundsForRestart）
    krkr::sound::StopAllWaveSoundsForRestart();
}
//---------------------------------------------------------------------------

} // namespace krkr::base
