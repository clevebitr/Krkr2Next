#include "shim/classes/TjsStorages.h"

#include <string>

#include "core/io/IoName.h"
#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/engine/TjsEngine.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"

namespace krkr::script {

namespace {

class StoragesClass final : public TJS::tTJSNativeClass {
  public:
    static tjs_uint32 ClassID;

    StoragesClass() : tTJSNativeClass(TJS_W("Storages")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Storages)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ Storages)
      { return TJS_S_OK; }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Storages)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ isExistentStorage) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        bool existent = false;
        if (GetBridge().storage != nullptr) {
          const std::unique_ptr<krkr::io::IStream> stream = GetBridge().storage->Open(name);
          existent = stream != nullptr;
          krkr::log::Debug("Storages.isExistentStorage(\"", name, "\") = ",
                           existent ? "true" : "false");
        } else {
          krkr::log::Warn("Storages.isExistentStorage called before storage attach");
        }
        if (result != nullptr) {
          *result = existent ? 1 : 0;
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ isExistentStorage)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getPlacedPath) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        std::string placed;
        if (GetBridge().storage != nullptr) {
          const std::unique_ptr<krkr::io::IStream> stream = GetBridge().storage->Open(name);
          if (stream != nullptr) {
            placed = name;
          }
        }
        if (result != nullptr) {
          *result = WideFromUtf8(placed);
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ getPlacedPath)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ extractStorageExt) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        const std::size_t pos = name.find_last_of("./");
        const std::string ext =
            (pos != std::string::npos && name[pos] == '.') ? name.substr(pos) : std::string();
        if (result != nullptr) {
          *result = WideFromUtf8(ext);
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ extractStorageExt)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ extractStorageName) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        const std::size_t slash = name.find_last_of("/");
        const std::string base =
            slash == std::string::npos ? name : name.substr(slash + 1);
        const std::size_t dot = base.find_last_of('.');
        if (result != nullptr) {
          *result = WideFromUtf8(dot == std::string::npos ? base : base.substr(0, dot));
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ extractStorageName)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ extractStoragePath) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr nameText = *param[0];
        const std::string name = Utf8FromWide(nameText.c_str());
        const std::size_t slash = name.find_last_of("/");
        if (result != nullptr) {
          *result = WideFromUtf8(slash == std::string::npos ? std::string()
                                                       : name.substr(0, slash + 1));
        }
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ extractStoragePath)

      // 以下方法被大量脚本与 KAG 框架调用，当前版本仅接受调用并记录，
      // 不改变挂载表。自动挂载与补丁优先级由存储层在打开游戏时决定。
      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addAutoPath) {
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        const ttstr autoPathText = *param[0];
        krkr::log::Warn("Storages.addAutoPath(", Utf8FromWide(autoPathText.c_str()),
                        ") ignored: mount set is fixed at open time");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addAutoPath)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ removeAutoPath) {
        krkr::log::Warn("Storages.removeAutoPath ignored: mount set is fixed at open time");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ removeAutoPath)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ clearArchiveCache) {
        krkr::log::Warn("Storages.clearArchiveCache ignored: no archive cache yet");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ clearArchiveCache)

      TJS_END_NATIVE_MEMBERS
    }
};

} // namespace

tjs_uint32 StoragesClass::ClassID = static_cast<tjs_uint32>(-1);

void RegisterStoragesClass(Engine *engine, krkr::io::Storage *storage) {
  GetBridge().engine = engine;
  GetBridge().storage = storage;
  if (engine == nullptr || !engine->IsReady()) {
    krkr::log::Warn("RegisterStoragesClass: engine not ready");
    return;
  }

  auto *global = static_cast<TJS::iTJSDispatch2 *>(engine->GetGlobalObjectRaw());
  if (global == nullptr) {
    krkr::log::Warn("RegisterStoragesClass: global object unavailable");
    return;
  }

  auto *nativeClass = new StoragesClass();
  TJS::tTJSVariant value(nativeClass, nullptr);
  const tjs_error error =
      global->PropSet(TJS_MEMBERENSURE, TJS_W("Storages"), nullptr, &value, global);
  nativeClass->Release();
  if (TJS_FAILED(error)) {
    krkr::log::Error("RegisterStoragesClass: failed to expose Storages, code=", error);
    return;
  }
  krkr::log::Info("script: Storages registered (storage attached=", storage != nullptr, ")");
}

} // namespace krkr::script
