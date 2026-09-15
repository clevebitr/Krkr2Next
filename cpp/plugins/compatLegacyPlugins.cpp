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
