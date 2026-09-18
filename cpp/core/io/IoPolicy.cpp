//
// 当前生效的 IO/加载策略实现（见 IoPolicy.h）。
//
#include "IoPolicy.h"

#include <spdlog/spdlog.h>

namespace krkr::io {

    namespace {
        // 缺省 = 旧版 krkr2 层：字段全部取 StoragePolicy 的缺省值，这里显式写出来是为了
        // 让"缺省口径"与 compat 侧的两套取值能一眼对照（compat/CompatLayer.cpp 里还有
        // 一份同义实现，两份必须一致；那边是"层 → 策略"的定义处，这里是 io 的缺省兜底）。
        StoragePolicy MakeClassicPolicy() {
            StoragePolicy p;
            p.tieBreak = AutoPathTieBreak::FirstRegisteredWins;
            p.patchRule = PatchNameRule::SubstringContainsPatch;
            p.patchPriority = PatchPriorityEnd::InsertAtFront;
            p.archiveRoot = ArchiveRootOrder::RootFirst;
            p.autoRepairMissingDelimiter = false;
            p.stripArchiveDelimiterInAppPath = false;
            p.mountSiblingsForArchiveProject = false;
            p.xp3SegmentCacheBytes = 1ull << 20;
            return p;
        }

        // 函数局部静态：避免跨 TU 的静态初始化顺序问题（兼容层可能在 io 的这个对象
        // 完成动态初始化之前就注入策略）。C++11 起局部静态初始化是线程安全的。
        StoragePolicy &PolicyStorage() {
            static StoragePolicy policy = MakeClassicPolicy();
            return policy;
        }
    } // namespace

    StoragePolicy ClassicStoragePolicy() {
        return MakeClassicPolicy();
    }

    const StoragePolicy &ActiveStoragePolicy() {
        return PolicyStorage();
    }

    void SetActiveStoragePolicy(const StoragePolicy &policy) {
        PolicyStorage() = policy;
        if(auto logger = spdlog::get("core")) {
            logger->info(
                "io policy: tie-break={} patch-rule={} patch-end={} archive-root={} "
                "delimiter-repair={} apppath-strip={} archive-project-siblings={} "
                "xp3-segcache={}MiB",
                policy.tieBreak == AutoPathTieBreak::FirstRegisteredWins
                    ? "first-wins"
                    : "last-wins",
                policy.patchRule == PatchNameRule::SubstringContainsPatch
                    ? "substring"
                    : "prefix+seq",
                policy.patchPriority == PatchPriorityEnd::InsertAtFront ? "front"
                                                                        : "end",
                policy.archiveRoot == ArchiveRootOrder::RootFirst ? "root-first"
                                                                  : "root-last",
                policy.autoRepairMissingDelimiter ? "on" : "off",
                policy.stripArchiveDelimiterInAppPath ? "on" : "off",
                policy.mountSiblingsForArchiveProject ? "on" : "off",
                static_cast<unsigned long long>(policy.xp3SegmentCacheBytes >> 20));
        }
    }

} // namespace krkr::io
