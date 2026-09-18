#pragma once

#include "KRMovieDef.h"
#include <atomic>
#include <condition_variable>
#include <thread>
#include <mutex>

NS_KRMOVIE_BEGIN
typedef std::recursive_mutex CCriticalSection;
typedef std::unique_lock<std::recursive_mutex> CSingleLock;

class CThread {
public:
    CThread();

    virtual ~CThread();

    void Create();

    bool IsRunning() const { return m_bRunning; }

    void StopThread(bool bWait = true);

    /**
     * 有界等待线程自己退出（entry() 末尾把 m_bRunning 置 false）。
     *
     * 为什么需要：StopThread() 里的 join() 没有上界，而影片线程可能卡在文件读取/
     * 音频设备写入里永不返回，级联下去会把调用它的**渲染线程**永久钉死（真机
     * 16:51 的 engine.log.stall：render 停在"~MoviePlayerOverlay→删除播放器"，
     * 影片侧停在"CloseInputStream→等 player 线程退出(join)"）。
     */
    bool WaitForExit(unsigned int milliseconds);

    void Sleep(unsigned int milliseconds);

    bool IsCurrentThread();

protected:
    int entry();

    virtual void OnStartup() {}

    virtual void Process() = 0;

    virtual void OnExit() {}

    std::thread *m_ThreadId = nullptr;
    std::atomic<bool> m_bStop, m_bRunning;
    std::mutex m_mtxStopEvent;
    std::condition_variable m_StopEvent;
    CCriticalSection m_CriticalSection;
};

class CEvent {
public:
    void Set() { m_cond.notify_all(); }

    void Reset() {}

    bool WaitMSec(unsigned int milliSeconds) {
        CSingleLock lock(mutex);
        return m_cond.wait_for(lock, std::chrono::milliseconds(milliSeconds)) !=
            std::cv_status::timeout;
    }

    void Wait() {
        CSingleLock lock(mutex);
        m_cond.wait(lock);
    }

protected:
    CCriticalSection mutex;
    std::condition_variable_any m_cond;
};
NS_KRMOVIE_END