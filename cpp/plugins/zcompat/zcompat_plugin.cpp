//---------------------------------------------------------------------------
// Z (krkrz / KIRIKIRI Z) 插件兼容模块
//---------------------------------------------------------------------------
// 游戏 plugin/ 目录下的 Z 系插件 DLL 在移动端不存在独立二进制（ncb 插件编进
// libengine_api.so），由内部注册表直接命中。与 Kirikiroid2 一致：TVPLoadPlugin
// 密封所有 DLL 加载，功能内建在引擎核心 + 插件层，link 只查内部注册表。
//
// 各条目状态（详见 krkrz-compat.md）：
//   - 功能已内建（核心类/渲染管线已有，link 即可用）：drawdeviceD3DZ /
//     drawdeviceD3D（主 DrawBuffer 由 core/visual 合成）、kztouch（Window
//     getTouchPoint 等触摸类已内建）、menu（MenuItem 核心类全局注册）。
//   - 真实现（内嵌参考源码）：lzfs（"lzfs://" 存储协议，见下方 LzfsStorageMedia；
//     NEKOPARA 4 的 E-mote 立绘动作文件靠它取 PSB）。
//   - 源码已内嵌、默认不执行（引擎选项门控）：k2compat（Krkr2Compat 纯 TJS 层，
//     k2compat_scripts.cpp；只有 k2compat_scripts=1 时才在框架就绪后安装）。
//   - 名字映射（已有实现复用）：motionplayer_nod3d → motionplayer.dll（在
//     PluginImpl.cpp 的 TVPLoadPlugin 映射）。
//   - 暂不可实现（源码不可得/桌面概念）：squirrel（VM 源码可得但待移植）、
//     multiimage（闭源）、win32ole/PackinOne/extNagano/pkutil/xpzdec/
//     yuzuex（桌面/工具/私有），挂名消除 Failed 噪音，属"先可 link，再实现"
//     的中间态，游戏若实际调用对应类会抛「类不存在」。
//---------------------------------------------------------------------------
#include "ncbind.hpp"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "EventIntf.h"
#include <spdlog/spdlog.h>

// 挂名空回调：仅让 ncbAutoRegister::LoadModule 命中内部注册表返回成功。
static void ZCompatStub() {}

// k2compat_scripts.cpp 的安装入口（该 TU 与本文件同属 krkr2plugin 目标）。
extern void TVPInstallK2CompatScripts();

namespace {
    // Krkr2Compat（k2compat.dll）内嵌 TJS 层：**默认不执行**。
    //
    // 历史（见下面 g_z_k2compat 的注释）：在插件注册期执行这套脚本会抛异常打断启动链，
    // 当年实测三款游戏全黑，于是退回"只挂名"。现在的处理是：
    //   1) 脚本源始终编译进产物（不再是"没有任何 target 编译的死代码"）；
    //   2) 只有显式开启引擎选项 `k2compat_scripts=1` 才安装；
    //   3) 安装时机推迟到**框架就绪后的第一帧**（连续事件钩子一次性触发），
    //      而不是注册期 —— 这正是当年缺的那个时机。
    class K2CompatInstallHook : public tTVPContinuousEventCallbackIntf {
    public:
        void OnContinuousCallback(tjs_uint64 /*tick*/) override {
            TVPRemoveContinuousEventHook(this);
            spdlog::info("K2Compat: 框架已就绪，安装内嵌 Krkr2Compat 脚本层");
            TVPInstallK2CompatScripts();
        }
    };

    K2CompatInstallHook g_k2compat_install_hook;

    bool K2CompatEnabledByOption() {
        tTJSVariant raw;
        if(!TVPGetCommandLine(TJS_W("k2compat_scripts"), &raw))
            return false;
        ttstr v(raw);
        v.ToLowerCase();
        return v == TJS_W("1") || v == TJS_W("true") || v == TJS_W("on") ||
               v == TJS_W("yes");
    }

    void ZCompatRegisterK2Compat() {
        if(!K2CompatEnabledByOption()) {
            spdlog::info("K2Compat: k2compat.dll 挂名（未开启 k2compat_scripts，"
                         "不执行内嵌 Krkr2Compat 脚本层）");
            return;
        }
        spdlog::info("K2Compat: 已开启 k2compat_scripts，等框架就绪后安装脚本层");
        TVPAddContinuousEventHook(&g_k2compat_install_hook);
    }
} // namespace

// drawdeviceD3DZ.dll —— Z 主画面 D3D drawdevice。移动端真形态是 core/visual
// 渲染 管线（Kirikiroid2 RenderManager_ogl 直接合成 Z 主
// DrawBuffer），非独立插件； 挂名仅为让 Z 游戏启动时的 Plugins.link 不报
// Failed。
static ncbCallbackAutoRegister g_z_drawdeviceD3DZ(TJS_W("drawdeviceD3DZ.dll"),
                                                  ncbAutoRegister::PreRegist,
                                                  &ZCompatStub, nullptr);

// drawdeviceD3D.dll —— krkr2 常规 D3D draw device（Z 之前的路径），同上挂名。
static ncbCallbackAutoRegister g_z_drawdeviceD3D(TJS_W("drawdeviceD3D.dll"),
                                                 ncbAutoRegister::PreRegist,
                                                 &ZCompatStub, nullptr);

// kztouch.dll —— Z 触摸/触摸控件；移动端触摸已内建（Window.getTouchPoint 等），
// link 通过即满足 Z 游戏启动链路，挂名。
static ncbCallbackAutoRegister g_z_kztouch(TJS_W("kztouch.dll"),
                                           ncbAutoRegister::PreRegist,
                                           &ZCompatStub, nullptr);

// k2compat.dll —— krkr2→Z 兼容层：Krkr2Compat 纯 TJS 层（k2compat_scripts.cpp 内嵌）。
//
// 默认只挂名，原因见上面 ZCompatRegisterK2Compat 的注释：在插件注册期执行这套脚本会抛
// 异常打断启动链（engine(22) 实测：LoadAllModules 时 k2compat/preseed 异常 → 三款游戏
// 全黑）。现在改为"源始终编译 + 引擎选项门控 + 框架就绪后一次性安装"。
static ncbCallbackAutoRegister g_z_k2compat(TJS_W("k2compat.dll"),
                                            ncbAutoRegister::PreRegist,
                                            &ZCompatRegisterK2Compat, nullptr);

// kagexopt.dll —— KAG 系统扩展（KAGEX 优化），挂名。
static ncbCallbackAutoRegister g_z_kagexopt(TJS_W("kagexopt.dll"),
                                            ncbAutoRegister::PreRegist,
                                            &ZCompatStub, nullptr);

// multiimage.dll —— 多图/多图层纹理（PSD 相关），源码未开源（krkrz
// 无源码），挂名。
static ncbCallbackAutoRegister g_z_multiimage(TJS_W("multiimage.dll"),
                                              ncbAutoRegister::PreRegist,
                                              &ZCompatStub, nullptr);

// squirrel.dll —— 内嵌 Squirrel 脚本语言（部分 Z 游戏系统/存档逻辑）；krkr2
// 源码 可得（trunk/src/plugins/win32/squirrel + Squirrel
// VM），待移植，当前挂名。
static ncbCallbackAutoRegister g_z_squirrel(TJS_W("squirrel.dll"),
                                            ncbAutoRegister::PreRegist,
                                            &ZCompatStub, nullptr);

// menu.dll —— MenuItem 类/Window.menu；核心已全局注册 tTJSNC_MenuItem
// （ScriptMgnIntf.cpp registerObject("MenuItem")），link 即用，挂名。
static ncbCallbackAutoRegister g_z_menu(TJS_W("menu.dll"),
                                        ncbAutoRegister::PreRegist,
                                        &ZCompatStub, nullptr);

// yuzuex.dll —— 千恋万花等 Yuzusoft 作品专用（疑似私有），挂名。
static ncbCallbackAutoRegister g_z_yuzuex(TJS_W("yuzuex.dll"),
                                          ncbAutoRegister::PreRegist,
                                          &ZCompatStub, nullptr);

// lzfs.dll —— "LZ 文件系统"存储协议（真实现，非挂名）。
//
// NEKOPARA 4（官中 KRKR 版）的动态立绘是 E-mote，动作文件通过它自己的存储协议取：
//     lzfs://./e-moteショコラ冬制服a.psb
// 此前本插件只是挂名（让 Plugins.link 不报 Failed），于是存储系统在
// `NormalizeStorageName` 阶段就回答
//     Not supported media type "lzfs"
// → 所有 e-mote*.psb 打不开 → 立绘全空（真机 2026-09-18 16:48 的 script exception
// 实证）。这里按参考实现（AetherKiri compatLegacyPlugins.cpp 的 LzfsStorageMedia）
// 注册一个同名存储媒体，把 "媒体名://<域>/<内层路径>" 的内层路径交回引擎的普通
// 存储解析 —— 散装文件、auto-path、XP3 内条目都照常命中。
namespace {

/** 从 "域/内层路径" 取出内层路径（去掉域与开头的 ./ 或 .\）。 */
ttstr LzfsInnerPath(const ttstr &name) {
    const tjs_char *raw = name.c_str();
    const tjs_char *slash = TJS_strchr(raw, TJS_W('/'));
    ttstr path = slash ? ttstr(slash + 1) : name;
    while(path.GetLen() >= 2 && path[0] == TJS_W('.') &&
          (path[1] == TJS_W('/') || path[1] == TJS_W('\\'))) {
        path = ttstr(path.c_str() + 2);
    }
    return path;
}

class LzfsStorageMedia : public iTVPStorageMedia {
public:
    void AddRef() override { ++ref_count_; }

    void Release() override {
        if(ref_count_ == 1)
            delete this;
        else
            --ref_count_;
    }

    void GetName(ttstr &name) override { name = TJS_W("lzfs"); }

    void NormalizeDomainName(ttstr &) override {}

    void NormalizePathName(ttstr &) override {}

    bool CheckExistentStorage(const ttstr &name) override {
        return !TVPGetPlacedPath(LzfsInnerPath(name)).IsEmpty();
    }

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        const ttstr inner = LzfsInnerPath(name);
        if(auto lg = spdlog::get("plugin"))
            lg->info("lzfs: 打开 {} -> {}", name.AsStdString(),
                     inner.AsStdString());
        return TVPCreateStream(inner, flags);
    }

    void GetListAt(const ttstr &, iTVPStorageLister *) override {}

    void GetLocallyAccessibleName(ttstr &name) override {
        name = TVPGetLocallyAccessibleName(LzfsInnerPath(name));
    }

private:
    ~LzfsStorageMedia() override = default;
    tjs_int ref_count_ = 1;
};

void ZCompatRegisterLzfs() {
    // Register 按媒体名去重（同名已注册直接返回），所以重复 link 不会叠加。
    static LzfsStorageMedia *media = nullptr;
    if(media == nullptr)
        media = new LzfsStorageMedia(); // 进程级存活：存储管理器持引用
    TVPRegisterStorageMedia(media);
    if(auto lg = spdlog::get("plugin"))
        lg->info("lzfs: 已注册 lzfs:// 存储协议（E-mote 立绘等靠它取 PSB）");
}

} // namespace

static ncbCallbackAutoRegister g_z_lzfs(TJS_W("lzfs.dll"),
                                        ncbAutoRegister::PreRegist,
                                        &ZCompatRegisterLzfs, nullptr);

// win32ole.dll —— OLE 自动化（桌面概念，移动端无意义），挂名避免启动报错。
static ncbCallbackAutoRegister g_z_win32ole(TJS_W("win32ole.dll"),
                                            ncbAutoRegister::PreRegist,
                                            &ZCompatStub, nullptr);

// motionplayer_nod3d.dll —— 无 D3D 版 motionplayer（千恋万花）；已在
// PluginImpl.cpp TVPLoadPlugin 映射到 motionplayer.dll 复用现有实现，无需挂名。

// PackinOne.dll —— 打包/资源插件，疑似闭源（krkrz-compat.md 判定 C 级跳过）；
// 挂名消除 Failed 噪音，功能不实现。
static ncbCallbackAutoRegister g_z_packinone(TJS_W("packinone.dll"),
                                             ncbAutoRegister::PreRegist,
                                             &ZCompatStub, nullptr);

// extNagano.dll —— 独立冷门扩展（千恋万花实载 Failed；krkrz-compat.md C
// 级跳过），挂名。
static ncbCallbackAutoRegister g_z_extnagano(TJS_W("extnagano.dll"),
                                             ncbAutoRegister::PreRegist,
                                             &ZCompatStub, nullptr);

// pkutil.dll —— 打包工具（Kemomusu 等实载 Failed），挂名。
static ncbCallbackAutoRegister g_z_pkutil(TJS_W("pkutil.dll"),
                                          ncbAutoRegister::PreRegist,
                                          &ZCompatStub, nullptr);

// xpzdec.dll —— xpz 加密归档解码（.tpm 在 TVPLoadInternalPlugin 中归一为
// .dll），挂名。
static ncbCallbackAutoRegister g_z_xpzdec(TJS_W("xpzdec.dll"),
                                          ncbAutoRegister::PreRegist,
                                          &ZCompatStub, nullptr);
