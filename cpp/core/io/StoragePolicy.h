//
// IO/加载组件的策略契约。
//
// 为什么需要它：KiriNext（旧 krkr2 层）与 AetherKiri 层在"同名资源谁生效"上的差别，
// 全部集中在少数几个开关上（auto path 表的 tie-break、挂载排序、补丁判定、包内目录
// 顺序、缺分隔符修复、档案工程的 app path 语义、段缓存预算）。IO 组件本身不认"层"，
// 只认这份策略；层在 compat/CompatLayer.cpp 里给出各自的取值。
//
// 约束：本头文件（以及整个 cpp/core/io/）不得包含任何具体层、具体游戏、具体发行版的
// 知识；策略取值只能由 compat/ 提供。差异证据见 compat/recon/io-loading-diff.md。
//
// 注意：这些开关目前**只有取值与语义登记**，还没有接到 StorageIntf/StorageImpl 的实现
// 上——接线属于 M1.2–M1.6 的迁移工作，在那之前改动本文件不产生任何运行时影响。
#pragma once

namespace krkr::io {

    // auto path 表同键冲突时谁生效。
    // 旧层：先注册者优先（建表时 Find 守卫）；AetherKiri：后注册者优先（哈希表覆盖）。
    enum class AutoPathTieBreak {
        FirstRegisteredWins,
        LastRegisteredWins,
    };

    // 补丁档案名判定规则。
    enum class PatchNameRule {
        // 旧层：路径里出现 "patch" 子串就算（unpatched.xp3 也会被当成补丁）。
        SubstringContainsPatch,
        // AetherKiri：patch / patchN（按序号）/ patchXXX（具名，排在数字补丁之后）。
        PrefixPatchWithSequence,
    };

    // 补丁档案插到 auto path 表的哪一端。必须与 tie-break 配套，语义才等价于
    // "补丁优先"：先注册者优先的表要把补丁插到队首，后注册者优先的表要挪到队尾。
    enum class PatchPriorityEnd {
        InsertAtFront,
        AppendAtEnd,
    };

    // 包内目录条目（auto path 里的 "xxx.xp3>目录/" 前缀）根目录的先后。
    // AetherKiri 把根目录放最后，避免 tools/startup.tjs 之类遮住包根的同名文件。
    enum class ArchiveRootOrder {
        RootFirst,
        RootLast,
    };

    struct StoragePolicy {
        AutoPathTieBreak tieBreak = AutoPathTieBreak::FirstRegisteredWins;
        PatchNameRule patchRule = PatchNameRule::SubstringContainsPatch;
        PatchPriorityEnd patchPriority = PatchPriorityEnd::InsertAtFront;
        ArchiveRootOrder archiveRoot = ArchiveRootOrder::RootFirst;

        // 缺尾部分隔符的路径：false = 抛异常（旧层），true = 自动补齐并打日志。
        bool autoRepairMissingDelimiter = false;

        // TVPGetAppPath() 是否去掉档案分隔符 '>'：旧层不去（档案工程返回 ".../data.xp3>"），
        // AetherKiri 去（返回档案所在目录）。影响 patch.tjs / AfterStartup.tjs 的定位与
        // 临时文件落点。
        bool stripArchiveDelimiterInAppPath = false;

        // 档案工程启动（工程路径以 ".../data.xp3>" 形式给出）时是否挂载兄弟 patch*.xp3。
        // 旧层不挂（两个挂载函数都在非 '/' 结尾时提前返回）。
        bool mountSiblingsForArchiveProject = false;

        // XP3 段缓存预算（字节）：旧层 1 MiB，AetherKiri 256 MiB。
        // 生效规则：显式覆盖 > 本值（见 io/IoPolicy.h 的 EffectiveSegmentCacheLimitBytes）。
        unsigned long long xp3SegmentCacheBytes = 1ull << 20;
    };

} // namespace krkr::io
