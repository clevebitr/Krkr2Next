//
// 虚拟文件提供者注册表实现。设计见 IoVirtualFile.h。
//
#include "IoVirtualFile.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include "UtilStreams.h" // tTVPMemoryStream（io 侧负责把内容包成流）

namespace krkr::io {
    namespace {

        struct ProviderEntry {
            VirtualFileExistsFn exists;
            VirtualFileContentFn content;
            // 覆盖型：排在物理存储之前（见 IoVirtualFile.h 的说明）。
            bool isOverride;
        };

        // 函数局部静态：避免跨 TU 的静态初始化顺序问题（provider 的静态注册可能
        // 早于本 TU 的命名空间级对象构造）。
        std::mutex &ProviderMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::vector<ProviderEntry> &Providers() {
            static std::vector<ProviderEntry> providers;
            return providers;
        }

        // provider 重入保护：provider 内部可能再走 io 查询（如"源文件是否存在"），
        // 必须让它看到"没有虚拟文件"，否则会无限递归。
        thread_local int g_providerDepth = 0;

        struct ProviderDepthGuard {
            ProviderDepthGuard() { ++g_providerDepth; }
            ~ProviderDepthGuard() { --g_providerDepth; }
            ProviderDepthGuard(const ProviderDepthGuard &) = delete;
            ProviderDepthGuard &operator=(const ProviderDepthGuard &) = delete;
        };

        std::vector<ProviderEntry> SnapshotProviders(bool overrides) {
            std::lock_guard<std::mutex> lock(ProviderMutex());
            std::vector<ProviderEntry> out;
            out.reserve(Providers().size());
            for(const auto &entry : Providers()) {
                if(entry.isOverride == overrides)
                    out.push_back(entry);
            }
            return out;
        }

        void RegisterProvider(VirtualFileExistsFn exists,
                              VirtualFileContentFn content, bool isOverride) {
            if(!exists)
                return;
            std::lock_guard<std::mutex> lock(ProviderMutex());
            auto &providers = Providers();
            for(auto &entry : providers) {
                if(entry.exists == exists) {
                    entry.content = content; // 重复注册：更新 content
                    entry.isOverride = isOverride;
                    return;
                }
            }
            providers.push_back({ exists, content, isOverride });
        }

        void UnregisterProvider(VirtualFileExistsFn exists) {
            std::lock_guard<std::mutex> lock(ProviderMutex());
            auto &providers = Providers();
            providers.erase(
                std::remove_if(providers.begin(), providers.end(),
                               [exists](const ProviderEntry &entry) {
                                   return entry.exists == exists;
                               }),
                providers.end());
        }

        bool AnyProviderMatches(const ttstr &name, bool overrides) {
            if(name.IsEmpty() || g_providerDepth != 0)
                return false;

            ProviderDepthGuard guard;
            for(const auto &entry : SnapshotProviders(overrides)) {
                if(!entry.exists)
                    continue;
                try {
                    if(entry.exists(name))
                        return true;
                } catch(...) {
                    // provider 不得让普通存储查询失败；它自己的诊断留在别处。
                }
            }
            return false;
        }

    } // namespace

    void RegisterVirtualFileProvider(VirtualFileExistsFn exists,
                                     VirtualFileContentFn content) {
        RegisterProvider(exists, content, /*isOverride=*/false);
    }

    void UnregisterVirtualFileProvider(VirtualFileExistsFn exists) {
        UnregisterProvider(exists);
    }

    void RegisterVirtualFileOverrideProvider(VirtualFileExistsFn exists,
                                             VirtualFileContentFn content) {
        RegisterProvider(exists, content, /*isOverride=*/true);
    }

    void UnregisterVirtualFileOverrideProvider(VirtualFileExistsFn exists) {
        UnregisterProvider(exists);
    }

    bool IsVirtualFile(const ttstr &name) {
        return AnyProviderMatches(name, /*overrides=*/false);
    }

    bool IsVirtualFileOverride(const ttstr &name) {
        return AnyProviderMatches(name, /*overrides=*/true);
    }

    tTJSBinaryStream *OpenVirtualFile(const ttstr &name) {
        if(name.IsEmpty() || g_providerDepth != 0)
            return nullptr;

        ProviderDepthGuard guard;
        // 覆盖型先试（与 IsVirtualFileOverride 的优先级一致）。
        for(bool overrides : { true, false }) {
            for(const auto &entry : SnapshotProviders(overrides)) {
                if(!entry.content)
                    continue;
                std::string content;
                bool handled = false;
                try {
                    handled = entry.content(name, content);
                } catch(...) {
                    handled = false;
                }
                if(!handled)
                    continue;

                // io 负责包流：provider 不认识流类型，也不依赖 base 的流实现。
                auto *stream = new tTVPMemoryStream();
                try {
                    if(!content.empty())
                        stream->Write(content.data(),
                                      static_cast<tjs_uint>(content.size()));
                    stream->Seek(0, TJS_BS_SEEK_SET);
                    return stream;
                } catch(...) {
                    delete stream;
                    throw;
                }
            }
        }
        return nullptr;
    }

} // namespace krkr::io
