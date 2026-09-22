#include "Thread.h"
#include <thread>
#include <stdexcept>
#include "MsgIntf.h"
#include "ThreadImpl.h"

NS_KRMOVIE_BEGIN

CThread::CThread() : m_bStop(false), m_bRunning(false) {}

CThread::~CThread() {
    if(m_bRunning) {
        StopThread();
    }
    if(m_ThreadId) {
        m_ThreadId->join();
        delete m_ThreadId;
    }
}

void CThread::Create() {
    if(m_bRunning.exchange(true)) {
        TVPThrowExceptionMessage(TJS_W("thread already in running"));
    }
    m_bStop = false;
    if(m_ThreadId) {
        m_ThreadId->join();
        delete m_ThreadId;
    }
    m_ThreadId = new std::thread(&CThread::entry, this);
}

void CThread::StopThread(bool bWait /*= true*/) {
    {
        // 在锁内改状态、出了锁再通知：否则与 Sleep() 的等待交错时会丢唤醒，
        // 睡眠方要等满自己的超时才回到循环顶部看到 m_bStop。
        std::lock_guard<std::mutex> lock(m_mtxStopEvent);
        m_bStop = true;
    }
    m_StopEvent.notify_all();
    if(m_ThreadId && bWait) {
        if(IsCurrentThread()) {
            // 从线程自己里调（OnExit→CloseStream 路径）不能 join 自己：
            // std::thread::join() 会抛 system_error(EDEADLK)，在 entry() 里没人接就是
            // std::terminate。只置 m_bStop，循环自然退出。
            return;
        }
        m_ThreadId->join();
        delete m_ThreadId;
        m_ThreadId = nullptr;
    }
}

bool CThread::WaitForExit(unsigned int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    while(m_bRunning.load(std::memory_order_acquire)) {
        if(std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

void CThread::Sleep(unsigned int milliseconds) {
    if(IsCurrentThread()) {
        std::unique_lock<std::mutex> lock(m_mtxStopEvent);
        m_StopEvent.wait_for(lock, std::chrono::milliseconds(milliseconds));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

bool CThread::IsCurrentThread() {
    if(!m_ThreadId)
        return false;
    return m_ThreadId->get_id() == std::this_thread::get_id();
}

int CThread::entry() {
    OnStartup();
    Process();
    OnExit();
    m_bRunning = false;
    TVPOnThreadExited();
    return 0;
}

NS_KRMOVIE_END
