//
// 兼容层注册表实现。取值依据见 compat/recon/io-loading-diff.md。
//
#include "CompatLayer.h"

#include "AetherKiriCompanions.h"
#include "io/IoPolicy.h"
#include "tjsObject.h"

#include <cstring>
#include <spdlog/spdlog.h>

namespace krkr::compat {

    namespace {
        LayerId g_active = LayerId::Krkr2Classic;

        // 旧版 krkr2 层：保持本仓库既有语义，字段全部用 StoragePolicy 的缺省值。
        // 这里显式写出来（而不是依赖缺省值），是为了让两层的差异一眼可比。
        const io::StoragePolicy kClassicPolicy = [] {
            io::StoragePolicy p;
            p.tieBreak = io::AutoPathTieBreak::FirstRegisteredWins;
            p.patchRule = io::PatchNameRule::SubstringContainsPatch;
            p.patchPriority = io::PatchPriorityEnd::InsertAtFront;
            p.archiveRoot = io::ArchiveRootOrder::RootFirst;
            p.autoRepairMissingDelimiter = false;
            p.stripArchiveDelimiterInAppPath = false;
            p.mountSiblingsForArchiveProject = false;
            p.xp3SegmentCacheBytes = 1ull << 20; // 1 MiB
            return p;
        }();

        // AetherKiri 层：上游 krkr2 语义 + AetherKiri 的补丁/路径处理。
        const io::StoragePolicy kAetherKiriPolicy = [] {
            io::StoragePolicy p;
            p.tieBreak = io::AutoPathTieBreak::LastRegisteredWins;
            p.patchRule = io::PatchNameRule::PrefixPatchWithSequence;
            p.patchPriority = io::PatchPriorityEnd::AppendAtEnd;
            p.archiveRoot = io::ArchiveRootOrder::RootLast;
            p.autoRepairMissingDelimiter = true;
            p.stripArchiveDelimiterInAppPath = true;
            p.mountSiblingsForArchiveProject = true;
            p.xp3SegmentCacheBytes = 256ull << 20; // 256 MiB
            return p;
        }();

        bool EqualsIgnoreCase(const char *a, const char *b) {
            if(!a || !b)
                return false;
            for(; *a && *b; ++a, ++b) {
                char ca = *a, cb = *b;
                if(ca >= 'A' && ca <= 'Z')
                    ca = static_cast<char>(ca - 'A' + 'a');
                if(cb >= 'A' && cb <= 'Z')
                    cb = static_cast<char>(cb - 'A' + 'a');
                if(ca != cb)
                    return false;
            }
            return *a == 0 && *b == 0;
        }
    } // namespace

    const char *LayerName(LayerId id) {
        switch(id) {
            case LayerId::AetherKiri:
                return "aetherkiri";
            case LayerId::Krkr2Classic:
            default:
                return "krkr2-classic";
        }
    }

    bool LayerFromName(const char *name, LayerId &out) {
        if(!name || !*name)
            return false;
        if(EqualsIgnoreCase(name, "aetherkiri") || EqualsIgnoreCase(name, "ak")) {
            out = LayerId::AetherKiri;
            return true;
        }
        if(EqualsIgnoreCase(name, "krkr2-classic") ||
           EqualsIgnoreCase(name, "classic") ||
           EqualsIgnoreCase(name, "kirikiri2-classic") ||
           EqualsIgnoreCase(name, "krkr2")) {
            out = LayerId::Krkr2Classic;
            return true;
        }
        return false;
    }

    LayerId ActiveLayer() {
        return g_active;
    }

    void SetActiveLayer(LayerId id) {
        // 伴生脚本 provider 的注册点在 AetherKiriCompanions.cpp；显式调一次，
        // 避免静态库 dead-strip 把该 TU 丢掉（注册幂等）。
        RegisterAetherKiriCompanions();
        // 策略注入必须**每次都做**（不能只在层变化时做）：本函数每次档解析都会被调用，
        // 而 io 侧的策略对象是函数局部静态，首次初始化可能是缺省值；这里保证"当前层"
        // 与"io 生效策略"始终一致。
        io::SetActiveStoragePolicy(StoragePolicyFor(id));
        // A 块（TJS2 内核兼容回退）：用户裁决只给 AetherKiri 层开启。
        // tjs2 侧不认识"层"，只认这个开关（见 tjsObject.h / tjsObject.cpp 的说明）。
        TJS::TJSSetCompatFallbacksEnabled(id == LayerId::AetherKiri);
        // 常量回退（`lls*` / `archiveUniqueKey` / `kirikiriz*` / `developMode` …）
        // **两层都给**：它们只是 Windows/krkrz 的运行期常量名，不改变脚本语义；
        // 缺了它们，直接读 `llsUserDirs` 的 initialize.tjs 在 classic 层会启动期抛异常。
        TJS::TJSSetCompatConstantFallbacksEnabled(true);
        if(g_active == id)
            return;
        g_active = id;
        if(auto logger = spdlog::get("core")) {
            logger->info("compat layer: 激活层切换为 {}（IO 策略：tie-break={}、patch={}、"
                         "档案工程兄弟挂载={}、段缓存={} MiB）",
                         LayerName(id),
                         StoragePolicyFor(id).tieBreak ==
                                 io::AutoPathTieBreak::FirstRegisteredWins
                             ? "first-wins"
                             : "last-wins",
                         StoragePolicyFor(id).patchRule ==
                                 io::PatchNameRule::SubstringContainsPatch
                             ? "substring"
                             : "prefix+seq",
                         StoragePolicyFor(id).mountSiblingsForArchiveProject ? "on"
                                                                            : "off",
                         static_cast<unsigned long long>(
                             StoragePolicyFor(id).xp3SegmentCacheBytes >> 20));
        }
    }

    bool SetActiveLayerByName(const char *name) {
        LayerId id;
        if(!LayerFromName(name, id))
            return false;
        SetActiveLayer(id);
        return true;
    }

    io::StoragePolicy StoragePolicyFor(LayerId id) {
        return id == LayerId::AetherKiri ? kAetherKiriPolicy : kClassicPolicy;
    }

    const io::StoragePolicy &ActiveStoragePolicy() {
        return g_active == LayerId::AetherKiri ? kAetherKiriPolicy : kClassicPolicy;
    }

} // namespace krkr::compat
