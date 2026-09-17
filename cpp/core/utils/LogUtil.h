// LogUtil.h — 日志去重/限频小工具（header-only）
//
// 为什么需要它：引擎里有若干"解析循环 / 每帧 / 每次资源加载"里的 info 级日志，
// 同一条件会连续刷很多行。真机日志里实测过：
//   - `ExtractFrameInfo[...]: no content in frame0` 连刷 6 行完全相同的内容；
//   - `Cannot open storage .../blandlogo1.png` 连刷 10 行；
// 重复行不但会挤掉真正有用的信息（日志有 4MiB×3 的轮转上限，被刷满就把
// 之前的证据顶掉了），写盘本身也在真机上带来可观测的 IO 开销。
//
// 这里的两个工具都**只在真正要记录时才求值消息体**：调用方把日志语句作为变参
// 传给宏，被限流/去重掉的调用不会格式化参数、也不会构造消息字符串。
//
// ⚠️ 但**缓存键本身每次都会构造**（宏要先用它查表）。键应当是一个便宜的字面量
// 加少量拼接（如 "psb_extract_no_src_" + label）；不要把昂贵的计算塞进键里来
// 换取"少一次查表"。真正的高频路径（每帧）应当直接不做日志，而不是靠限频。
//
// 约束（与 AGENTS.md「硬约束」一致）：
//   - 不得改变正常结果、时序或生命周期；本文件只做"记不记"的判断。
//   - 高频日志必须采样/限频/去重或只记状态边沿。
//
// 线程安全：内部用一把互斥锁保护表。日志本身不是热路径上的关键操作，锁的
// 粒度（一次 map 查找）远小于格式化一条日志的代价。
#ifndef KRKR_LOGUTIL_H
#define KRKR_LOGUTIL_H

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// 只依赖标准库：本文件只决定"记不记"，不负责输出，调用方自己 include spdlog。
namespace krkr {

// ---------------------------------------------------------------------------
// 按 key 去重：同一 key 在进程生命周期内最多记录 limit 次。
//
// 适合"同一原因必然重复触发"的场景（解析失败、资源缺失）。默认只记一次，
// 需要知道"到底发生了几次"时把 limit 调大。
// ---------------------------------------------------------------------------
class LogDedup {
public:
    static LogDedup &Instance() {
        static LogDedup inst;
        return inst;
    }

    // 允许记录则返回 true。调用方据此决定是否构造并输出日志。
    bool ShouldLog(const std::string &key, int limit = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        int &count = counts_[key];
        if(count >= limit)
            return false;
        ++count;
        return true;
    }

    // 换游戏/重开时清空，避免上一局的状态把这一局的首次记录吞掉。
    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        counts_.clear();
    }

private:
    LogDedup() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, int> counts_;
};

// ---------------------------------------------------------------------------
// 按 key 限频：同一 key 在 interval_ms 内最多记录一条。
//
// 适合"持续发生但不想刷屏"的场景（每帧/每次都失败）。与 LogDedup 的区别是
// 它不会永久静音，长时间运行仍能周期性看到最新状态。
// ---------------------------------------------------------------------------
class LogRateLimit {
public:
    static LogRateLimit &Instance() {
        static LogRateLimit inst;
        return inst;
    }

    bool ShouldLog(const std::string &key, int64_t interval_ms) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = last_.find(key);
        if(it != last_.end()) {
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second)
                    .count();
            if(elapsed_ms < interval_ms)
                return false;
        }
        last_[key] = now;
        return true;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        last_.clear();
    }

private:
    LogRateLimit() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        last_;
};

// 重开游戏时把两张表都清掉。
inline void ResetLogGuards() {
    LogDedup::Instance().Reset();
    LogRateLimit::Instance().Reset();
}

} // namespace krkr

// 便捷宏：只有允许记录时才求值 lambda 里的 spdlog 调用。
//   KRKR_LOG_ONCE(key, spdlog::warn("...{}", expensive()));
//   KRKR_LOG_RATE(key, 5000, spdlog::info("...{}", expensive()));
#define KRKR_LOG_ONCE(key, ...)                                                \
    do {                                                                       \
        if(::krkr::LogDedup::Instance().ShouldLog((key))) {                    \
            __VA_ARGS__;                                                       \
        }                                                                      \
    } while(0)

#define KRKR_LOG_RATE(key, interval_ms, ...)                                   \
    do {                                                                       \
        if(::krkr::LogRateLimit::Instance().ShouldLog((key), (interval_ms))) { \
            __VA_ARGS__;                                                       \
        }                                                                      \
    } while(0)

#endif // KRKR_LOGUTIL_H
