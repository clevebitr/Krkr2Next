//
// AetherKiri 兼容层插件：zlib.dll 与 version.dll
//
// 来源：AetherKiri `cpp/plugins/compatLegacyPlugins.cpp:199-331`（rev bd14a986，见
// compat/upstream/aetherkiri_ports.json）。按本工程的"层"机制登记为 **AetherKiri 层专属**：
// 只有激活 AetherKiri 层时才注册这两个模块（见 compat/ModuleGate.h）。
//
// 与上游的差别（已在移植清单登记为 local-fix）：
//   - `Version.engine` 返回本引擎名，而不是上游写死的 "AetherKiri"；
//   - 模块归属登记（上游没有层机制）。
//
// 为什么值得移植：KRKRZ 系游戏会 `Plugins.link("zlib.dll")` / `"version.dll"` 并在脚本里
// 直接用 `Zlib.compress/uncompress`、`Version.versionString`；缺这两个模块时脚本拿到的是
// link 失败，属于"看着能跑、一到相关分支就抛"的类型。
//
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "compat/ModuleGate.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <zlib.h>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

    tjs_int paramInt(tjs_int index, tjs_int numparams, tTJSVariant **param,
                     tjs_int fallback = 0) {
        if(index < numparams && param && param[index] &&
           param[index]->Type() != tvtVoid)
            return static_cast<tjs_int>(*param[index]);
        return fallback;
    }

    std::vector<tjs_uint8> variantBytes(const tTJSVariant &value) {
        if(value.Type() == tvtOctet) {
            tTJSVariantOctet *octet = value.AsOctetNoAddRef();
            const auto *data =
                reinterpret_cast<const tjs_uint8 *>(octet->GetData());
            return std::vector<tjs_uint8>(data, data + octet->GetLength());
        }
        std::string text = ttstr(value.AsStringNoAddRef()).AsStdString();
        return std::vector<tjs_uint8>(text.begin(), text.end());
    }

    void setOctetResult(tTJSVariant *result,
                        const std::vector<tjs_uint8> &bytes) {
        if(!result)
            return;
        tTJSVariantOctet *octet = TJSAllocVariantOctet(
            bytes.empty() ? nullptr : bytes.data(),
            static_cast<tjs_uint>(bytes.size()));
        *result = octet;
        octet->Release();
    }

    // 本引擎名（`Version.engine` 用；上游返回 "AetherKiri"）。
    const tjs_char *EngineName() { return TJS_W("KrKr2Next"); }

} // namespace

//---------------------------------------------------------------------------
// zlib.dll
//---------------------------------------------------------------------------
#define NCB_MODULE_NAME TJS_W("zlib.dll")

class ZlibCompat {
public:
    ttstr getVersion() const { return ttstr(zlibVersion()); }

    static tjs_error TJS_INTF_METHOD compressCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                ZlibCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> input = variantBytes(*param[0]);
        const int level = paramInt(1, numparams, param, Z_DEFAULT_COMPRESSION);

        uLongf bound = compressBound(static_cast<uLong>(input.size()));
        std::vector<tjs_uint8> output(bound);
        int zret = compress2(output.data(), &bound,
                             input.empty() ? nullptr : input.data(),
                             static_cast<uLong>(input.size()), level);
        if(zret != Z_OK)
            TVPThrowExceptionMessage(TJS_W("zlib compress failed"));
        output.resize(bound);
        setOctetResult(result, output);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD uncompressCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  ZlibCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> input = variantBytes(*param[0]);
        uLongf expected = static_cast<uLongf>(
            paramInt(1, numparams, param,
                     static_cast<tjs_int>(
                         std::max<size_t>(input.size() * 4, 1024))));

        for(int tries = 0; tries < 8; ++tries) {
            std::vector<tjs_uint8> output(expected);
            uLongf actual = expected;
            int zret = uncompress(output.data(), &actual,
                                  input.empty() ? nullptr : input.data(),
                                  static_cast<uLong>(input.size()));
            if(zret == Z_OK) {
                output.resize(actual);
                setOctetResult(result, output);
                return TJS_S_OK;
            }
            if(zret != Z_BUF_ERROR)
                TVPThrowExceptionMessage(TJS_W("zlib uncompress failed"));
            expected *= 2;
        }

        TVPThrowExceptionMessage(TJS_W("zlib output buffer is too large"));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD versionCb(tTJSVariant *result, tjs_int,
                                               tTJSVariant **, ZlibCompat *) {
        if(result)
            *result = ttstr(zlibVersion());
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Zlib, ZlibCompat) {
    RawCallback("compress", &Class::compressCb, 0);
    RawCallback("deflate", &Class::compressCb, 0);
    RawCallback("uncompress", &Class::uncompressCb, 0);
    RawCallback("inflate", &Class::uncompressCb, 0);
    RawCallback("version", &Class::versionCb, 0);
    NCB_PROPERTY_RO(versionString, getVersion);
}

static tjs_error TJS_INTF_METHOD zlibCompressCb(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *) {
    return ZlibCompat::compressCb(result, numparams, param, nullptr);
}

static tjs_error TJS_INTF_METHOD zlibUncompressCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
    return ZlibCompat::uncompressCb(result, numparams, param, nullptr);
}

static tjs_error TJS_INTF_METHOD zlibVersionCb(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               iTJSDispatch2 *) {
    if(result)
        *result = ttstr(zlibVersion());
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(zlibCompress, zlibCompressCb);
NCB_REGISTER_FUNCTION(zlibUncompress, zlibUncompressCb);
NCB_REGISTER_FUNCTION(zlibVersion, zlibVersionCb);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("version.dll")

//---------------------------------------------------------------------------
// version.dll
//---------------------------------------------------------------------------
class VersionCompat {
public:
    ttstr getString() const { return TVPGetVersionString(); }
    ttstr getInformation() const { return TVPGetVersionInformation(); }
    ttstr getEngine() const { return ttstr(EngineName()); }

    static tjs_error TJS_INTF_METHOD dictionaryCb(tTJSVariant *result, tjs_int,
                                                  tTJSVariant **,
                                                  VersionCompat *) {
        if(!result)
            return TJS_S_OK;
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict) {
            result->Clear();
            return TJS_S_OK;
        }
        const auto put = [dict](const tjs_char *name, const ttstr &value) {
            tTJSVariant v(value);
            dict->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, dict);
        };
        put(TJS_W("engine"), ttstr(EngineName()));
        put(TJS_W("versionString"), TVPGetVersionString());
        put(TJS_W("versionInformation"), TVPGetVersionInformation());
        *result = tTJSVariant(dict, dict);
        dict->Release();
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Version, VersionCompat) {
    NCB_PROPERTY_RO(engine, getEngine);
    NCB_PROPERTY_RO(versionString, getString);
    NCB_PROPERTY_RO(versionInformation, getInformation);
    RawCallback("toDictionary", &Class::dictionaryCb, 0);
}

//---------------------------------------------------------------------------
// 层归属登记：这两个模块只在 **AetherKiri 层**注册。
// 静态期登记，早于启动期的 ncbAutoRegister::LoadAllModules（见 ModuleGate.h）。
//---------------------------------------------------------------------------
namespace {
    struct AetherKiriLayerOwnership {
        AetherKiriLayerOwnership() {
            krkr::compat::RegisterModuleOwner("zlib.dll",
                                              krkr::compat::LayerId::AetherKiri);
            krkr::compat::RegisterModuleOwner(
                "version.dll", krkr::compat::LayerId::AetherKiri);
        }
    } g_aetherkiri_layer_ownership;
} // namespace
