// MoviePlayerJanitor.h — 把"销毁播放器"从渲染线程挪到后台清理线程（header-only）
//
// 为什么必须这样：BasePlayer 的析构会走 CloseInputStream → StopThread()（join 解码
// 线程）并排空消息队列。解码线程可能正卡在队列/同步等待上，于是 join 无限期不返回，
// **渲染线程被永久钉死**。真机 2026-09-18 15:50:51 的现象正是如此：
//     (info) Video EC_COMPLETE: releasing video resources      ← 最后一行日志
// 之后 engine.log 再也没有任何一行（连"引擎卡死探针"都打不出来，说明日志锁本身也被
// 卡住的那条线程握着），只能杀进程。
//
// 这里把销毁动作交给一条后台线程：渲染线程立刻返回、游戏继续跑；即使清理真的卡住，
// 也只卡在后台线程，并且日志里会留下"开始/完成"两行，一眼能看出是不是卡在销毁里。
//
// 生命周期是安全的：只是让对象**活得更久**（延迟释放），不会提前析构。
#ifndef KRKR_MOVIE_PLAYER_JANITOR_H
#define KRKR_MOVIE_PLAYER_JANITOR_H

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <spdlog/spdlog.h>

namespace krkr {
namespace movie {

template <class T>
class DeferredDestroyer {
public:
    static DeferredDestroyer &Instance() {
        // 故意泄漏：后台线程是 detached 的，进程退出时不能让静态对象先析构
        // （线程还在用它的 mutex/队列）。
        static DeferredDestroyer *inst = new DeferredDestroyer();
        return *inst;
    }

    /** 接管所有权并在后台销毁；p 为 nullptr 时是空操作。 */
    void Defer(T *p) {
        if(p == nullptr)
            return;
        size_t queued = 0;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if(!m_started) {
                m_started = true;
                std::thread([this] { Run(); }).detach();
            }
            m_queue.push_back(p);
            queued = m_queue.size();
        }
        m_cv.notify_one();
        spdlog::info("movie: 后台销毁请求入队（排队 {} 个）", queued);
    }

private:
    void Run() {
        for(;;) {
            T *p = nullptr;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [this] { return !m_queue.empty(); });
                p = m_queue.front();
                m_queue.pop_front();
            }
            spdlog::info("movie: 后台销毁开始（临时文件删除/join 线程等阻塞操作）");
            delete p;
            spdlog::info("movie: 后台销毁完成");
        }
    }

    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<T *> m_queue;
    bool m_started = false;
};

} // namespace movie
} // namespace krkr

#endif // KRKR_MOVIE_PLAYER_JANITOR_H
