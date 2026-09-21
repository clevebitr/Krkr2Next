//
// AetherKiri 兼容层插件：一组 system-environment 类 Windows 插件桩
//   registory.dll / stdio.dll / javascript.dll / messenger.dll /
//   msgreceiver.dll / tasktray.dll / adjustMonitor.dll
//
// 来源：AetherKiri（rev 见 compat/upstream/aetherkiri_ports.json）
//   - registory.dll / stdio.dll / javascript.dll / messenger.dll /
//     msgreceiver.dll / tasktray.dll / adjustMonitor.dll
//       → `cpp/plugins/compatSystemPlugins.cpp`
// 归属：**AetherKiri 层专属**（见 compat/ModuleGate.h）——旧层不注册这些名字，
// 因此缺省（classic）行为完全不变。
//
// 为什么值得移植：这些是"脚本真的会调"的模块（KRKRZ/KAG 系常见），缺寄存器时
// `Plugins.link(...)` 失败、相关分支直接抛异常。本批只取**纯桩、无外部进程/网络**
// 的子集；process/shellExecute/httprequest 未进本批（compat/recon/plugin-compat-diff.md §5.1：
// 它们跑在持有 EGL 的脚本线程上，阻塞会直接卡帧）。
//
// 与上游的差别：只去掉上游的 `logCompatOnce` 文案里的平台名，改为本仓库口径；
// 其余语义逐条保留。
//
#include "DebugIntf.h"
#include "ncbind.hpp"
#include "compat/ModuleGate.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

    // tjs_char(UTF-16) <-> UTF-8。narrow 字符串在本工程按 UTF-8 处理
    // （见 tjsString.h 的 AsStdString / tTJSString(const std::string &)）。
    std::string toUtf8(const ttstr &text) { return text.AsStdString(); }

    void logCompatOnce(const tjs_char *module, const tjs_char *message) {
        static std::map<std::string, bool> emitted;
        const std::string key = toUtf8(ttstr(module) + TJS_W(":") + message);
        if(emitted[key])
            return;
        emitted[key] = true;
        TVPAddLog(ttstr(TJS_W("AetherKiri compat plugin ")) + module +
                  TJS_W(": ") + message);
    }

    void setDict(iTJSDispatch2 *dict, const tjs_char *name,
                 const tTJSVariant &value) {
        if(dict)
            dict->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dict);
    }

    // 外部脚本 VM（JavaScript / Squirrel）不存在：调用返回 void，但脚本进程
    // 不因缺模块而中断。上游同款。
    tjs_error TJS_INTF_METHOD unsupportedScriptCb(tTJSVariant *result, tjs_int,
                                                  tTJSVariant **,
                                                  iTJSDispatch2 *) {
        logCompatOnce(TJS_W("script-vm"),
                      TJS_W("external script VM is not embedded"));
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD clearTrueCb(tTJSVariant *result, tjs_int,
                                          tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = true;
        return TJS_S_OK;
    }

} // namespace

// ---------------------------------------------------------------------------
// systemEx.dll —— 环境变量 / URL 编解码 / 交互确认
//
// 真实可用的子集：`writeEnvValue`（setenv）、`urlencode`/`urldecode`（UTF-8 字节级）、
// `confirm`（无 UI，返回 true）、`waitForAppLock`（无锁，返回 true）。
// `readEnvValue`/`expandEnvString` 故意**不注册**：本仓库 windowEx.cpp 已提供同层实现，
// 这里再注册会在 AetherKiri 层把它们覆盖掉（TJS_MEMBERENSURE 后注册者赢）。
// ---------------------------------------------------------------------------
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")

namespace {
    tjs_error TJS_INTF_METHOD writeEnvValueCb(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        const std::string name = toUtf8(ttstr(*param[0]));
        const std::string value = toUtf8(ttstr(*param[1]));
#if defined(_WIN32)
        const int rc = _putenv_s(name.c_str(), value.c_str());
#else
        const int rc = setenv(name.c_str(), value.c_str(), 1);
#endif
        if(result)
            *result = rc == 0;
        return TJS_S_OK;
    }

    bool isUrlSafe(unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.' ||
               c == '~';
    }

    tjs_error TJS_INTF_METHOD urlencodeCb(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const std::string bytes = toUtf8(ttstr(*param[0]));
        std::string out;
        const char *hex = "0123456789ABCDEF";
        for(unsigned char c : bytes) {
            if(isUrlSafe(c))
                out.push_back(static_cast<char>(c));
            else {
                out += '%';
                out += hex[(c >> 4) & 0xf];
                out += hex[c & 0xf];
            }
        }
        if(result)
            *result = ttstr(out);
        return TJS_S_OK;
    }

    int hexDigit(char c) {
        if(c >= '0' && c <= '9')
            return c - '0';
        if(c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if(c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    }

    tjs_error TJS_INTF_METHOD urldecodeCb(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const std::string text = toUtf8(ttstr(*param[0]));
        std::string out;
        for(size_t i = 0; i < text.size(); ++i) {
            if(text[i] == '%' && i + 2 < text.size()) {
                const int hi = hexDigit(text[i + 1]);
                const int lo = hexDigit(text[i + 2]);
                if(hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            out.push_back(text[i] == '+' ? ' ' : text[i]);
        }
        if(result)
            *result = ttstr(out);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD confirmCb(tTJSVariant *result, tjs_int,
                                        tTJSVariant **, iTJSDispatch2 *) {
        logCompatOnce(TJS_W("systemEx.dll"),
                      TJS_W("confirm returns true in native compat mode"));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD waitForAppLockCb(tTJSVariant *result, tjs_int,
                                               tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = true;
        return TJS_S_OK;
    }
} // namespace

NCB_ATTACH_FUNCTION(writeEnvValue, System, writeEnvValueCb);
NCB_ATTACH_FUNCTION(urlencode, System, urlencodeCb);
NCB_ATTACH_FUNCTION(urldecode, System, urldecodeCb);
NCB_ATTACH_FUNCTION(confirm, System, confirmCb);
NCB_ATTACH_FUNCTION(waitForAppLock, System, waitForAppLockCb);

// ---------------------------------------------------------------------------
// registory.dll —— Windows 注册表写入
//
// 移动端没有注册表：写入落到进程内 map（同局内可读回），删除按前缀清；不触碰
// 文件系统。上游同款。名字是上游的拼写（`registory`）。
// ---------------------------------------------------------------------------
#define NCB_MODULE_NAME TJS_W("registory.dll")

namespace {
    std::map<std::string, tTJSVariant> g_registryCompat;

    tjs_error TJS_INTF_METHOD writeRegValueCb(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        g_registryCompat[toUtf8(ttstr(*param[0]))] = *param[1];
        logCompatOnce(TJS_W("registory.dll"),
                      TJS_W("registry writes are stored in-memory"));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD deleteRegValueCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        g_registryCompat.erase(toUtf8(ttstr(*param[0])));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD deleteRegKeyCb(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const std::string prefix = toUtf8(ttstr(*param[0]));
        for(auto it = g_registryCompat.begin(); it != g_registryCompat.end();) {
            if(it->first.rfind(prefix, 0) == 0)
                it = g_registryCompat.erase(it);
            else
                ++it;
        }
        if(result)
            *result = true;
        return TJS_S_OK;
    }
} // namespace

NCB_ATTACH_FUNCTION_WITHTAG(writeRegValue, RegistryCompatWrite, System,
                            writeRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(deleteRegValue, RegistryCompatDeleteValue, System,
                            deleteRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(writeDeleteValue, RegistryCompatWriteDeleteValue,
                            System, deleteRegValueCb);
NCB_ATTACH_FUNCTION_WITHTAG(deleteRegKey, RegistryCompatDeleteKey, System,
                            deleteRegKeyCb);

// ---------------------------------------------------------------------------
// stdio.dll —— 控制台 API
//
// 真机没有控制台窗口：attach/alloc/free 一律成功，stdin 返回空，
// stdout/stderr 写宿主标准流。上游同款。
// ---------------------------------------------------------------------------
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("stdio.dll")

class Stdio {
public:
    bool attachConsole(tjs_int = 0) { return true; }
    bool allocConsole(tjs_int = 0) { return true; }
    bool freeConsole() { return true; }
    ttstr stdinRead(bool = false) { return ttstr(); }
    void stdoutWrite(const tjs_char *text, bool = false) {
        if(text)
            std::fputs(toUtf8(ttstr(text)).c_str(), stdout);
    }
    void stderrWrite(const tjs_char *text, bool = false) {
        if(text)
            std::fputs(toUtf8(ttstr(text)).c_str(), stderr);
    }
    void flush() {
        std::fflush(stdout);
        std::fflush(stderr);
    }
};

NCB_ATTACH_CLASS(Stdio, System) {
    NCB_METHOD(attachConsole);
    NCB_METHOD(allocConsole);
    NCB_METHOD(freeConsole);
    NCB_METHOD_DIFFER(stdin, stdinRead);
    NCB_METHOD_DIFFER(stdout, stdoutWrite);
    NCB_METHOD_DIFFER(stderr, stderrWrite);
    NCB_METHOD(flush);
}

// ---------------------------------------------------------------------------
// javascript.dll —— 内嵌 JavaScript VM（不存在）
//
// `Scripts.execJS/execStorageJS` 返回 void；调试开关返回 true。
// 上游同款：让"检测到 JS 就用 JS"的脚本按"能力存在但无输出"降级。
// ---------------------------------------------------------------------------
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("javascript.dll")

NCB_ATTACH_FUNCTION(execJS, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(execStorageJS, Scripts, unsupportedScriptCb);
NCB_ATTACH_FUNCTION(enableDebugJS, Scripts, clearTrueCb);
NCB_ATTACH_FUNCTION(processDebugJS, Scripts, clearTrueCb);

// ---------------------------------------------------------------------------
// messenger.dll / msgreceiver.dll / tasktray.dll —— Win32 消息与托盘
//
// 真机消息/托盘由宿主拥有：注册接收器返回句柄 1、发送返回 true、
// 托盘图标调用返回 true（不产生 UI）。上游同款。
// ---------------------------------------------------------------------------
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("messenger.dll")

class WindowMessengerCompat {
public:
    tjs_int registerUserMessageReceiver(tjs_int, tjs_int, tTJSVariant,
                                        tTJSVariant) {
        return 1;
    }
    bool sendUserMessage(tjs_int, tjs_int = 0, tjs_int = 0) { return true; }
    bool sendMessage(const tjs_char *, const tjs_char *) { return true; }
};

NCB_ATTACH_CLASS(WindowMessengerCompat, Window) {
    NCB_METHOD(registerUserMessageReceiver);
    NCB_METHOD(sendUserMessage);
    NCB_METHOD(sendMessage);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msgreceiver.dll")

NCB_ATTACH_FUNCTION_WITHTAG(startMessageReceiver, MsgReceiverStart, Window,
                            clearTrueCb);
NCB_ATTACH_FUNCTION_WITHTAG(stopMessageReceiver, MsgReceiverStop, Window,
                            clearTrueCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")

class WindowTasktrayCompat {
public:
    bool showTasktrayIcon(const tjs_char * = nullptr) { return true; }
    bool hideTasktrayIcon() { return true; }
    bool setTasktrayIcon(const tjs_char * = nullptr) { return true; }
    bool popupTasktrayInfo(const tjs_char *, const tjs_char *, const tjs_char *,
                           tjs_int = 0) {
        return true;
    }
};

NCB_ATTACH_CLASS(WindowTasktrayCompat, Window) {
    NCB_METHOD(showTasktrayIcon);
    NCB_METHOD(hideTasktrayIcon);
    NCB_METHOD(setTasktrayIcon);
    NCB_METHOD(popupTasktrayInfo);
}

// ---------------------------------------------------------------------------
// adjustMonitor.dll —— 多显示器吸附
//
// Android/单显示器：返回以参数位置为准的矩形（不可用分量填 0），
// 脚本据 left/top 摆放窗口时不会跑飞。上游同款。
// ---------------------------------------------------------------------------
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("adjustMonitor.dll")

namespace {
    tjs_error TJS_INTF_METHOD adjustMoniCb(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *) {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return TJS_E_FAIL;
        tjs_int x = 0;
        tjs_int y = 0;
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject) {
            iTJSDispatch2 *src = param[0]->AsObjectNoAddRef();
            tTJSVariant value;
            if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("left2"),
                                          nullptr, &value, src)))
                x = static_cast<tjs_int>(value);
            else if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("left"),
                                               nullptr, &value, src)))
                x = static_cast<tjs_int>(value);
            if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("top2"),
                                          nullptr, &value, src)))
                y = static_cast<tjs_int>(value);
            else if(TJS_SUCCEEDED(src->PropGet(TJS_IGNOREPROP, TJS_W("top"),
                                               nullptr, &value, src)))
                y = static_cast<tjs_int>(value);
        }
        setDict(dict, TJS_W("x"), tTJSVariant(x));
        setDict(dict, TJS_W("y"), tTJSVariant(y));
        setDict(dict, TJS_W("left"), tTJSVariant(0));
        setDict(dict, TJS_W("top"), tTJSVariant(0));
        setDict(dict, TJS_W("right"), tTJSVariant(0));
        setDict(dict, TJS_W("bottom"), tTJSVariant(0));
        if(result)
            *result = tTJSVariant(dict, dict);
        dict->Release();
        return TJS_S_OK;
    }
} // namespace

NCB_REGISTER_FUNCTION(AdjustMoni, adjustMoniCb);

// ---------------------------------------------------------------------------
// 层归属登记（见 compat/ModuleGate.h）：本文件里的模块都只在 AetherKiri 层注册。
// ---------------------------------------------------------------------------
namespace {
    struct AetherKiriOwnershipForSystemEnv {
        AetherKiriOwnershipForSystemEnv() {
            krkr::compat::RegisterModuleOwner(
                "systemEx.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "registory.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner("stdio.dll",
                                              krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "javascript.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "messenger.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "msgreceiver.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "tasktray.dll", krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "adjustMonitor.dll", krkr::compat::LayerId::AetherKiri);
        }
    } g_aetherkiri_ownership_system_env;
} // namespace
