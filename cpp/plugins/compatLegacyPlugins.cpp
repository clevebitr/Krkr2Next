//---------------------------------------------------------------------------
// 老插件的名字别名
//---------------------------------------------------------------------------
// 这里不实现功能，只把"Windows 版是独立 DLL、而本引擎已经内建"的名字接过去，
// 免得 Plugins.link 失败之后脚本卡在等回调上。
//
// krmovie.dll / m2vdec.dll → layerExMovie.dll
//   KAG 框架与游戏脚本用这两个名字做视频播放（krmovie 是 movie 图层，m2vdec
//   是它 的解码器）。本引擎真正实现播放的是内建的 layerExMovie.dll（ffmpeg
//   播放核心 + overlay 图层），但没有注册这两个别名，于是 Plugins.link
//   直接失败、视频图层永远 完不成首帧。 实测
//   おっぱいスパイ学園：这两个插件加载都是 Failed，随后 StartApplication 返回
//   成功、窗口与 blit 都就绪，但所有计数静止不动（d_obj=0、layers=25 不再变化）
//   ——引擎活着却什么都不做，正是黑屏。开场动画等不到播放完成回调很可能就卡在这里。
//
// 移植自 AetherKiri cpp/plugins/compatLegacyPlugins.cpp
// 的同名处理（它把这两个名字 直接 LoadModule("layerExMovie.dll")）。
//
// 注意：别名只在"名字 → 内建实现"之间做映射，不改变内建实现本身；layerExMovie
// 若已经注册过，LoadModule 会直接跳过。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "ncbind.hpp"

#include <spdlog/spdlog.h>

#define NCB_MODULE_NAME TJS_W("krmovie.dll")
static void InitPlugin_KrMovieAlias() {
    // 记录一次即可：脚本可能反复 link，日志按"仅边沿"处理
    static bool logged = false;
    if(!logged) {
        logged = true;
        spdlog::info("Plugin alias: krmovie.dll -> layerExMovie.dll");
    }
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));
}
NCB_PRE_REGIST_CALLBACK(InitPlugin_KrMovieAlias);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("m2vdec.dll")
static void InitPlugin_M2VDecAlias() {
    static bool logged = false;
    if(!logged) {
        logged = true;
        spdlog::info("Plugin alias: m2vdec.dll -> layerExMovie.dll");
    }
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));
}
NCB_PRE_REGIST_CALLBACK(InitPlugin_M2VDecAlias);

//---------------------------------------------------------------------------
// 旧插件名桩：只注册名字，不实现功能
//
// 为什么要它：游戏脚本常用 `Plugins.link("xxx.dll")` 显式链接，而
// `TVPLoadInternalPlugin()` 只认**已注册**的模块名 —— 没注册就是 Failed，
// 脚本随后可能卡在等回调上（黑屏但引擎活着）。这些 DLL 在 Windows 版是独立
// 插件，其功能本引擎要么已内建、要么 Android 上根本不适用，所以这里只把
// **名字**注册成空模块，让 link 成功。
//
// 名单与做法移植自 AetherKiri cpp/plugins/dummy_plugin_stubs.cpp。去掉三类：
//   1) 本引擎已实现的模块名；
//   2) 上面已做别名的 krmovie/m2vdec；
//   3) zcompat（cpp/plugins/zcompat/zcompat_plugin.cpp）已经挂过名的 ——
//      那里用同一套 ncbCallbackAutoRegister 注册表，重复登记虽无害但冗余。
//---------------------------------------------------------------------------
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("adjustMonitor.dll")
static void InitPlugin_adjustMonitorStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_adjustMonitorStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("dmmcloud.dll")
static void InitPlugin_dmmcloudStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_dmmcloudStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdevice.dll")
static void InitPlugin_drawdeviceStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_drawdeviceStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceIrrlicht.dll")
static void InitPlugin_drawdeviceIrrlichtStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_drawdeviceIrrlichtStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceOgre.dll")
static void InitPlugin_drawdeviceOgreStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_drawdeviceOgreStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceZ_D3D9.dll")
static void InitPlugin_drawdeviceZ_D3D9Stub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_drawdeviceZ_D3D9Stub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("flashPlayer.dll")
static void InitPlugin_flashPlayerStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_flashPlayerStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("fpslimit.dll")
static void InitPlugin_fpslimitStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_fpslimitStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gameswf.dll")
static void InitPlugin_gameswfStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_gameswfStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")
static void InitPlugin_gfxEffectStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_gfxEffectStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("htmlhelp.dll")
static void InitPlugin_htmlhelpStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_htmlhelpStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httprequest.dll")
static void InitPlugin_httprequestStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_httprequestStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httpserv.dll")
static void InitPlugin_httpservStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_httpservStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("javascript.dll")
static void InitPlugin_javascriptStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_javascriptStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerEx.dll")
static void InitPlugin_layerExStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_layerExStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAgg.dll")
static void InitPlugin_layerExAggStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_layerExAggStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExCairo.dll")
static void InitPlugin_layerExCairoStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_layerExCairoStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExGdiPlus.dll")
static void InitPlugin_layerExGdiPlusStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_layerExGdiPlusStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSubImage.dll")
static void InitPlugin_layerExSubImageStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_layerExSubImageStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("libegl.dll")
static void InitPlugin_libeglStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_libeglStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("libglesv2.dll")
static void InitPlugin_libglesv2Stub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_libglesv2Stub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("magickpp.dll")
static void InitPlugin_magickppStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_magickppStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("messenger.dll")
static void InitPlugin_messengerStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_messengerStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("mkpj.dll")
static void InitPlugin_mkpjStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_mkpjStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msgreceiver.dll")
static void InitPlugin_msgreceiverStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_msgreceiverStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("oleclass.dll")
static void InitPlugin_oleclassStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_oleclassStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("onigruma.dll")
static void InitPlugin_onigrumaStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_onigrumaStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("process.dll")
static void InitPlugin_processStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_processStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("registory.dll")
static void InitPlugin_registoryStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_registoryStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("resourceRW.dll")
static void InitPlugin_resourceRWStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_resourceRWStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("shellExecute.dll")
static void InitPlugin_shellExecuteStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_shellExecuteStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sigcheck.dll")
static void InitPlugin_sigcheckStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_sigcheckStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("stdio.dll")
static void InitPlugin_stdioStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_stdioStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")
static void InitPlugin_systemExStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_systemExStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")
static void InitPlugin_tasktrayStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_tasktrayStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tftSave.dll")
static void InitPlugin_tftSaveStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_tftSaveStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("version.dll")
static void InitPlugin_versionStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_versionStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("videoEncoder.dll")
static void InitPlugin_videoEncoderStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_videoEncoderStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("windowExProgress.dll")
static void InitPlugin_windowExProgressStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_windowExProgressStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wmrdump.dll")
static void InitPlugin_wmrdumpStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_wmrdumpStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wsh.dll")
static void InitPlugin_wshStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_wshStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wumsadp.dll")
static void InitPlugin_wumsadpStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_wumsadpStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xmlhttprequest.dll")
static void InitPlugin_xmlhttprequestStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_xmlhttprequestStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xpressive.dll")
static void InitPlugin_xpressiveStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_xpressiveStub);
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("zlib.dll")
static void InitPlugin_zlibStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_zlibStub);
#undef NCB_MODULE_NAME
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkrsteam.dll")
static void InitPlugin_krkrsteamStub() {}
NCB_PRE_REGIST_CALLBACK(InitPlugin_krkrsteamStub);
