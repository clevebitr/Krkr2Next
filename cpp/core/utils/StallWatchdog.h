// StallWatchdog.h — 渲染线程卡死定位探针（header-only）
//
// 为什么需要它：真机上出现过两次"画面停住、日志一行不再增长、随后系统 ANR"
// （おっぱいスパイ学園 播 CG 视频时；千恋万花 退出一类操作）。这种卡死**没有任何
// 日志**可看——出问题的那个调用还没返回，所以它的日志永远不会出现，只能看到
// "最后一条日志是什么"，无法证明卡在哪儿。反复猜是浪费时间（AGENTS.md 第 14 条）。
//
// 做法：渲染线程在每个关键阶段边界调用 MarkStage()（两次 relaxed 原子写，热路径
// 可忽略），一条看门狗线程每 500ms 检查"距上次推进多久"；只有**真的卡住
// ≥4 秒**才打一条 warn，并带上最后推进到的阶段名。同一段卡死只报一次，恢复后
// 重新武装 —— 属于"只记状态边沿"，不刷屏。
//
// 约束：
//   - 只做观测：不改变结果、时序、生命周期，也不参与任何加锁顺序。
//   - 看门狗线程只在卡死时写日志；它不持有任何锁（spdlog 内部锁除外）。
//   - 线程随 engine_create 启动、engine_destroy 停止并 join，500ms 一轮，join 很快。
#ifndef KRKR_STALLWATCHDOG_H
#define KRKR_STALLWATCHDOG_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <spdlog/spdlog.h>

namespace krkr {
namespace stall {

/** 距上次推进超过这个时长即判定为"卡住"（毫秒）。 */
constexpr int64_t kStallThresholdMs = 4000;

inline std::atomic<int64_t> g_lastProgressMs{ 0 };
inline std::atomic<const char *> g_stage{ "尚未开始" };
inline std::atomic<bool> g_stop{ false };
inline std::atomic<bool> g_running{ false };
/** 宿主主动暂停引擎（切后台/进设置）时置位：此时没有 tick 是**预期**的，不能报警。 */
inline std::atomic<bool> g_paused{ false };
inline std::thread g_thread;

inline int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/**
 * 标记"渲染线程推进到了某个阶段"。stage 必须是**静态存储期**的字符串字面量
 * （只存指针，不做拷贝），典型一次调用的代价是两次 relaxed 原子写。
 */
inline void MarkStage(const char *stage) {
    g_stage.store(stage, std::memory_order_relaxed);
    g_lastProgressMs.store(NowMs(), std::memory_order_relaxed);
}

inline void WatchdogLoop() {
    bool reported = false;
    while(!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if(g_stop.load(std::memory_order_relaxed))
            break;
        if(g_paused.load(std::memory_order_relaxed)) {
            reported = false;
            continue; // 宿主主动暂停：没有 tick 是正常的
        }
        const int64_t last = g_lastProgressMs.load(std::memory_order_relaxed);
        if(last == 0)
            continue; // 还没开始跑（启动期）
        const int64_t stalledMs = NowMs() - last;
        if(stalledMs >= kStallThresholdMs) {
            if(!reported) {
                reported = true;
                spdlog::warn("引擎卡死探针：渲染线程已 {:.1f}s 没有推进，"
                             "最后阶段＝{}（卡死期间的日志不会再出现，"
                             "看这一条定位）",
                             static_cast<double>(stalledMs) / 1000.0,
                             g_stage.load(std::memory_order_relaxed));
            }
        } else if(stalledMs < 1000) {
            reported = false; // 已恢复，重新武装
        }
    }
}

/** 引擎创建后调用；重复调用无副作用。 */
inline void Start() {
    if(g_running.exchange(true))
        return;
    g_stop.store(false);
    g_paused.store(false);
    g_lastProgressMs.store(0);
    g_stage.store("尚未开始");
    g_thread = std::thread(WatchdogLoop);
}

/** 宿主主动暂停/恢复引擎（engine_pause / engine_resume）。 */
inline void SetPaused(bool paused) {
    g_paused.store(paused, std::memory_order_relaxed);
    if(!paused)
        g_lastProgressMs.store(NowMs(), std::memory_order_relaxed);
}

/** 引擎销毁时调用；会 join（最多等一轮 500ms）。 */
inline void Stop() {
    if(!g_running.exchange(false))
        return;
    g_stop.store(true);
    if(g_thread.joinable())
        g_thread.join();
}

} // namespace stall
} // namespace krkr

#endif // KRKR_STALLWATCHDOG_H
