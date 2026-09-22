#include "ncbind.hpp"

// extrans.dll：补充内建转场之外的 KAGEX 转场（目前是 'wave'）。
// 其它模块是空桩——引擎内建了它们的功能（vorbis/opus 解码等），
// 但部分游戏会按名字显式 link。
//
// 移植自 AetherKiri cpp/plugins/extrans.cpp 与 extrans_precise/（见
// compat/upstream/aetherkiri_ports.json）。
#include "extrans_precise/wave.h"

#define NCB_MODULE_NAME TJS_W("extrans.dll")
static void initExtrans() {
    RegisterWaveTransHandlerProvider();
}

static void doneExtrans() {
    UnregisterWaveTransHandlerProvider();
}

NCB_PRE_REGIST_CALLBACK(initExtrans);
NCB_POST_UNREGIST_CALLBACK(doneExtrans);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuvorbis.dll")
static void wuvorbis_stub() {}
NCB_PRE_REGIST_CALLBACK(wuvorbis_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuopus.dll")
static void wuopus_stub() {}
NCB_PRE_REGIST_CALLBACK(wuopus_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuflac.dll")
static void wuflac_stub() {}
NCB_PRE_REGIST_CALLBACK(wuflac_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExColor.dll")
static void layerExColor_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExColor_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExMosaic.dll")
static void layerExMosaic_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExMosaic_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSave.dll")
static void layerExSave_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExSave_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAVI.dll")
static void layerExAVI_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExAVI_stub);
