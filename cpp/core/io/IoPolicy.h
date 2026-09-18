//
// 当前生效的 IO/加载策略（注入点）。
//
// 设计约束（见 compat/README.md §2）：`cpp/core/io/` **不认识"层"**，只认这份取值。
// 兼容层激活时调用 `SetActiveStoragePolicy()` 把本层口径注入进来；io 的实现只读
// `ActiveStoragePolicy()`。因此依赖方向是 compat -> io，io 不会反向认识 aetherkiri /
// krkr2-classic 这些名字。
//
// 缺省值 = 旧版 krkr2 层口径，与搬迁前的历史行为逐条一致（见 StoragePolicy.h 的缺省值）。
#pragma once

#include "StoragePolicy.h"

namespace krkr::io {

    // 当前生效策略（进程内唯一）。
    const StoragePolicy &ActiveStoragePolicy();

    // 注入策略。只在引擎启动期调用（层激活）；取值变化时打一条 info 日志，
    // 便于真机确认"这一次跑的是哪套口径"。
    void SetActiveStoragePolicy(const StoragePolicy &policy);

    // 旧版 krkr2 层口径（缺省）。
    StoragePolicy ClassicStoragePolicy();

    // ── XP3 段缓存预算 ────────────────────────────────────────────────
    // 取值优先级：显式覆盖（低内存路径 SysInitImpl、或将来脚本设置）> 激活层策略。
    // 为什么不能直接读策略：`TVPSegmentCacheLimit` 是静态初始化期定型的全局，而层激活
    // 发生在 engine_create（更晚）；改成"惰性 + 显式覆盖标记"后两边的意图都能保住。
    void SetSegmentCacheLimitOverride(unsigned long long bytes);
    bool HasSegmentCacheLimitOverride();
    // 生效值（字节）。未覆盖时 = ActiveStoragePolicy().xp3SegmentCacheBytes。
    unsigned long long EffectiveSegmentCacheLimitBytes();

} // namespace krkr::io
