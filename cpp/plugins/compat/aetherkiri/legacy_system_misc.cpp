//
// AetherKiri 兼容层插件：fpslimit.dll / layeredwindow.dll / kztouch.dll / dmmcloud.dll
//
// 来源：AetherKiri（rev 见 compat/upstream/aetherkiri_ports.json）
//   - fpslimit.dll      → `cpp/plugins/compatSystemPlugins.cpp:832-846`
//   - layeredwindow.dll → `cpp/plugins/compatLegacyPlugins.cpp:617-657`
// 归属：**AetherKiri 层专属**（见 compat/ModuleGate.h）——旧层不注册这两个名字，
// 因此缺省（classic）行为完全不变。
//
// 与上游的差别（移植清单里按 local-fix / partial-extract 记录）：
//   - 模块归属登记（上游没有层机制）；
//   - `layeredwindow` 的说明日志文案改为本仓库口径。
//
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "ncbind.hpp"
#include "compat/ModuleGate.h"

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

//---------------------------------------------------------------------------
// fpslimit.dll —— 帧率上限开关
//
// `System.fpslimit` 是 KRKRZ 系脚本会写的"期望帧率"（默认 1000）。上游是纯状态桩
// （不真的限速，实际限速由宿主/引擎选项负责），这里照搬：只保存取值供脚本读回，
// 不改变引擎的帧率控制（本仓库的限速在壳侧 fpsLimit 选项）。
//---------------------------------------------------------------------------
#define NCB_MODULE_NAME TJS_W("fpslimit.dll")

namespace {
    tjs_int g_fpsLimit = 1000;
}

class SystemFpsLimitCompat {
public:
    tjs_int getFpsLimit() const { return g_fpsLimit; }
    void setFpsLimit(tjs_int value) { g_fpsLimit = value > 0 ? value : 1000; }
};

NCB_ATTACH_CLASS(SystemFpsLimitCompat, System) {
    NCB_PROPERTY(fpslimit, getFpsLimit, setFpsLimit);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layeredwindow.dll")

//---------------------------------------------------------------------------
// layeredwindow.dll —— Windows 分层窗口
//
// 移动端没有分层窗口概念：真机里 Layer 树的合成由宿主负责，所以这个模块只需要让
// `layeredwindow(...)` 可调用并返回 true（脚本据此认为"已启用"）。上游同款做法。
//---------------------------------------------------------------------------
namespace {
    tjs_error TJS_INTF_METHOD layeredWindowCompatCb(tTJSVariant *result, tjs_int,
                                                    tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    void registerLayeredWindowCompat() {
        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global)
            return;

        iTJSDispatch2 *method = TJSCreateNativeClassMethod(layeredWindowCompatCb);
        if(method) {
            tTJSVariant value(method, method);
            global->PropSet(TJS_MEMBERENSURE, TJS_W("layeredwindow"), nullptr,
                            &value, global);
            method->Release();
        }
        global->Release();
        TVPAddLog(TJS_W("compat plugin layeredwindow.dll: Layer 合成由宿主负责"));
    }

    void unregisterLayeredWindowCompat() {
        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global)
            return;
        global->DeleteMember(0, TJS_W("layeredwindow"), nullptr, global);
        global->Release();
    }
} // namespace

NCB_PRE_REGIST_CALLBACK(registerLayeredWindowCompat);
NCB_POST_UNREGIST_CALLBACK(unregisterLayeredWindowCompat);


#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kztouch.dll")

//---------------------------------------------------------------------------
// kztouch.dll —— KiriKiri Z 的触摸控件开关
//
// 纯状态桩（上游同款）：真机触摸由核心的 Window.getTouchPoint 等提供，这里只需让
// `KZTouch` 类存在、`available` 为 true，游戏按 available 决定走触摸 UI 分支。
// 来源 AetherKiri `compatLegacyPlugins.cpp:334-356`。
//---------------------------------------------------------------------------
class KZTouch {
public:
    bool getEnabled() const { return enabled_; }
    void setEnabled(bool value) { enabled_ = value; }
    bool getAvailable() const { return true; }
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    void reset() { enabled_ = true; }

private:
    bool enabled_ = true;
};

NCB_REGISTER_CLASS(KZTouch) {
    Constructor();
    NCB_PROPERTY(enabled, getEnabled, setEnabled);
    NCB_PROPERTY_RO(available, getAvailable);
    NCB_METHOD(enable);
    NCB_METHOD(disable);
    NCB_METHOD(reset);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("dmmcloud.dll")

//---------------------------------------------------------------------------
// dmmcloud.dll —— DMM 云存档/购买（桌面商店概念）
//
// 移动端没有对应服务：`available=false`、登录/购买一律失败、`logout` 成功、`userId` 空。
// 上游同款桩；至少让走 DMM 分支的游戏能 link 成功并按"不可用"降级。
// 来源 AetherKiri `compatLegacyPlugins.cpp:359-381`。
//---------------------------------------------------------------------------
class DMMCloud {
public:
    bool getAvailable() const { return false; }
    bool initialize(const tjs_char * = nullptr) { return false; }
    bool login(const tjs_char * = nullptr, const tjs_char * = nullptr) {
        return false;
    }
    bool logout() { return true; }
    bool purchase(const tjs_char * = nullptr) { return false; }
    ttstr getUserId() const { return ttstr(); }
};

NCB_REGISTER_CLASS(DMMCloud) {
    Constructor();
    NCB_PROPERTY_RO(available, getAvailable);
    NCB_PROPERTY_RO(userId, getUserId);
    NCB_METHOD(initialize);
    NCB_METHOD(login);
    NCB_METHOD(logout);
    NCB_METHOD(purchase);
}

//---------------------------------------------------------------------------
// 层归属登记（见 compat/ModuleGate.h）：本文件里的模块都只在 AetherKiri 层注册。
//---------------------------------------------------------------------------
namespace {
    struct AetherKiriOwnershipForMiscSystem {
        AetherKiriOwnershipForMiscSystem() {
            krkr::compat::RegisterModuleOwner("fpslimit.dll",
                                              krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "layeredwindow.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner("kztouch.dll",
                                              krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner("dmmcloud.dll",
                                              krkr::compat::LayerId::AetherKiri);
        }
    } g_aetherkiri_ownership_misc_system;
} // namespace
