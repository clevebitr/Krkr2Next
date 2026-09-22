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
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <spdlog/spdlog.h>

namespace krkr {
namespace stall {

/**
 * 距上次推进超过这个时长即判定为"卡住"（毫秒）。
 * 取 1500ms：真机实测正常帧 16–33ms、重载帧 150–450ms，故障帧 1000–3700ms
 * （乃至 13s+ 的 ANR）。这个阈值能把"重载"与"卡死"分开。
 */
constexpr int64_t kStallThresholdMs = 1500;

inline std::atomic<int64_t> g_lastProgressMs{ 0 };
inline std::atomic<const char *> g_stage{ "尚未开始" };
/**
 * 影片相关线程的阶段。这些线程卡住会通过 join 级联把渲染线程
 * 也拖死，而它们的日志往往还没打印 —— 卡死时把几边阶段一起落盘才能定位。
 *
 * 为什么分成三个槽位：player（`BasePlayer`）/ video（`CVideoPlayerVideo`）/ audio
 * （`CVideoPlayerAudio`）是三条不同的线程，而 `BasePlayer::OnExit` 的 join 级联会把
 * 卡死点从一条转移到另一条。共用一个槽位时后写的会盖掉先写的，`.stall` 里只剩一条，
 * 无法回答"到底哪条线程卡在哪"（真机 2026-09-23 00:31 就是这个情况：只剩
 * `OnExit→CloseStream(视频)` 与 `解码线程→处理消息/解码` 两条互相盖）。
 */
inline std::atomic<const char *> g_movieStage{ "movie: 未开始" };
inline std::atomic<const char *> g_movieVideoStage{ "movie: 未开始" };
inline std::atomic<const char *> g_movieAudioStage{ "movie: 未开始" };
/** 卡死时额外落盘的文件（绕过 spdlog：日志锁本身可能被卡住的那条线程握着）。 */
inline char g_dumpPathBuf[1024] = { 0 };
inline std::atomic<bool> g_dumpPathSet{ false };
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

/** 影片 player（`BasePlayer`）线程自己的阶段。 */
inline void MarkMovieStage(const char *stage) {
    g_movieStage.store(stage, std::memory_order_relaxed);
}

/** 视频解码线程（`CVideoPlayerVideo`）自己的阶段。 */
inline void MarkMovieVideoStage(const char *stage) {
    g_movieVideoStage.store(stage, std::memory_order_relaxed);
}

/** 音频解码线程（`CVideoPlayerAudio`）自己的阶段。 */
inline void MarkMovieAudioStage(const char *stage) {
    g_movieAudioStage.store(stage, std::memory_order_relaxed);
}

/** 引擎日志路径设定后调用；卡死转储写到 <path>.stall（内部拷贝，路径可临时）。 */
inline void SetDumpPath(const char *path) {
    if(path == nullptr || path[0] == '\0') {
        g_dumpPathSet.store(false, std::memory_order_relaxed);
        return;
    }
    std::snprintf(g_dumpPathBuf, sizeof(g_dumpPathBuf), "%s", path);
    g_dumpPathSet.store(true, std::memory_order_relaxed);
}

/**
 * 卡死转储：用裸 FILE* 追加一行，绝不经过 spdlog（真机上出现过"日志一行都不再
 * 增长"，说明日志锁被卡住的那条线程握着；那种情况下只有独立文件能留下证据）。
 */
inline void WriteDump(const char *reason, const char *renderStage,
                      const char *movieStage, const char *videoStage,
                      const char *audioStage, int64_t stalledMs) {
    char path[1152];
    if(g_dumpPathSet.load(std::memory_order_relaxed) && g_dumpPathBuf[0]) {
        std::snprintf(path, sizeof(path), "%s.stall", g_dumpPathBuf);
    } else {
        std::snprintf(path, sizeof(path), "krkr_stall.stall");
    }
    FILE *f = std::fopen(path, "ab");
    if(!f)
        return;
    const int64_t now = NowMs();
    std::fprintf(f,
                 "[stall] t=%lld stalled=%lldms reason=%s\n"
                 "        render: %s\n"
                 "        player: %s\n"
                 "        video : %s\n"
                 "        audio : %s\n",
                 static_cast<long long>(now),
                 static_cast<long long>(stalledMs), reason ? reason : "?",
                 renderStage ? renderStage : "?", movieStage ? movieStage : "?",
                 videoStage ? videoStage : "?",
                 audioStage ? audioStage : "?");
    std::fflush(f);
    std::fclose(f);
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
                const char *renderStage =
                    g_stage.load(std::memory_order_relaxed);
                const char *movieStage =
                    g_movieStage.load(std::memory_order_relaxed);
                const char *videoStage =
                    g_movieVideoStage.load(std::memory_order_relaxed);
                const char *audioStage =
                    g_movieAudioStage.load(std::memory_order_relaxed);
                // 先写独立转储文件（不依赖日志锁），再尝试常规日志。
                WriteDump("render-thread-stall", renderStage, movieStage,
                          videoStage, audioStage, stalledMs);
                spdlog::warn("引擎卡死探针：渲染线程已 {:.1f}s 没有推进，"
                             "最后阶段＝{}（player＝{}｜video＝{}｜audio＝{}）"
                             "（卡死期间的日志不会再出现，看这一条定位；另有 "
                             "{} .stall 转储）",
                             static_cast<double>(stalledMs) / 1000.0,
                             renderStage, movieStage, videoStage, audioStage,
                             g_dumpPathSet.load(std::memory_order_relaxed) &&
                                     g_dumpPathBuf[0]
                                 ? g_dumpPathBuf
                                 : "krkr_stall");
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
    g_movieStage.store("movie: 未开始");
    g_movieVideoStage.store("movie: 未开始");
    g_movieAudioStage.store("movie: 未开始");
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
