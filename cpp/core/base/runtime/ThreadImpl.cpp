/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/

#define NOMINMAX

#include "tjsCommHead.h"

// #include <process.h>
#include <algorithm>

#include "ThreadIntf.h"
#include "ThreadImpl.h"
#include "MsgIntf.h"
#include "DebugIntf.h"

#if defined(CC_TARGET_OS_IPHONE) || defined(__aarch64__)
#else
// #define USING_THREADPOOL11
#endif

#ifdef USING_THREADPOOL11
#include "threadpool11/pool.hpp"
#endif

#include <thread>

tjs_int DrawThreadNum = 1;

namespace krkr::utils {
    static tjs_int GetProcesserNum() {
        static tjs_int processor_num = 0;
        if(!processor_num) {
            processor_num = std::thread::hardware_concurrency();
            tjs_char tmp[34];
            krkr::utils::AddLog(ttstr(TJS_W("Detected CPU core(s): ")) +
                      TJS_tTVInt_to_str(processor_num, tmp));
        }
        return processor_num;
    }

    tjs_int GetProcessorNum() { return GetProcesserNum(); }

    Thread::Thread(bool suspended) {
        Terminated = false;
        Suspended = suspended;

        try {
            Handle = std::thread([this] { StartProc(this); });
            // Do NOT detach: we need Handle.joinable() == true so that
            // WaitFor() (join) works correctly. Detaching was a legacy
            // pattern that made it impossible to synchronize on thread
            // exit, leading to use-after-free on member mutexes.
        } catch(const std::system_error &) {
            KRKR_THROW_INTERNAL_ERROR;
        }
    }

    Thread::~Thread() {
        // Ensure the std::thread is not joinable when destroyed.
        // Subclass destructors SHOULD call Terminate() + WaitFor() first.
        // This is a safety net to avoid std::terminate().
        if(Handle.joinable()) {
            Terminated = true;
            _cond.notify_one(); // wake up if suspended
            Handle.join();
        }
    }

    void *Thread::StartProc(void *arg) {
        auto *_this = (krkr::utils::Thread *)arg;
        if(_this->Suspended) {
            std::unique_lock lk(_this->_mutex);
            _this->_cond.wait(lk);
        }
        _this->Execute();
        _this->Finished.store(true, std::memory_order_release);
        krkr::utils::OnThreadExited();
        return nullptr;
    }

    void Thread::WaitFor() {
        if(Handle.joinable()) {
            Handle.join();
        }
        // If the thread was somehow detached before (shouldn't happen with
        // the current code), spin-wait on the Finished flag as a safety net.
        while(!Finished.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ThreadPriority Thread::GetPriority() {
        // TODO: impl
        return ttpNormal;
    }

    void Thread::SetPriority(ThreadPriority pri) {
        // TODO: impl
    }

    void Thread::Resume() {
        Suspended = false;
        _cond.notify_one();
        // while((tjs_int32)ResumeThread(Handle) > 1) ;
    }

    void ThreadEvent::Set() {
        std::unique_lock lk(Mutex);
        Handle.notify_one();
    }

    void ThreadEvent::WaitFor(tjs_uint timeout) {
        // wait for event;
        // returns true if the event is set, otherwise (when timed out)
        // returns false.

        std::unique_lock lk(Mutex);
        if(timeout != 0) {
            Handle.wait_for(lk, std::chrono::milliseconds(timeout));
        } else {
            Handle.wait(lk);
        }
    }


    tjs_int GetThreadNum() {
        tjs_int threadNum =
            DrawThreadNum ? DrawThreadNum : GetProcesserNum();
        threadNum = std::min(threadNum, krkr::utils::MaxThreadNum);
        return threadNum;
    }

    void ExecThreadTask(int numThreads, ThreadTaskFunc func) {
        if(numThreads == 1) {
            func(0);
            return;
        }
#if !defined(USING_THREADPOOL11)
#pragma omp parallel for schedule(static)
        for(int i = 0; i < numThreads; ++i)
            func(i);
#else
        static threadpool11::Pool pool;
        std::vector<std::future<void>> futures;
        for(int i = 0; i < numThreads; ++i) {
            futures.emplace_back(pool.postWork<void>(std::bind(func, i)));
        }
        for(auto &it : futures)
            it.get();
#endif
#if 0
    ThreadInfo *threadInfo;
    threadInfo = ThreadList[ThreadTaskCount++];
    threadInfo->lpStartAddress = func;
    threadInfo->lpParameter = param;
    InterlockedIncrement(&RunningThreadCount);
    while (ResumeThread(threadInfo->thread) == 0)
      Sleep(0);
#endif
    }

    std::vector<std::function<void()>> _OnThreadExitedEvents;

    void OnThreadExited() {
        for(const auto &ev : _OnThreadExitedEvents) {
            ev();
        }
    }

    void AddOnThreadExitEvent(const std::function<void()> &ev) {
        _OnThreadExitedEvents.emplace_back(ev);
    }

} // namespace krkr::utils
