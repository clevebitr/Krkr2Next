//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
#ifndef ThreadImplH
#define ThreadImplH

#include "tjsNative.h"
#include "ThreadIntf.h"
#include <atomic>
#include <condition_variable>
#include <thread>

namespace krkr::utils {

    class Thread {
        bool Terminated;
        std::thread Handle;
        // DWORD ThreadId;
        std::mutex _mutex; // for suspend
        std::condition_variable _cond;
        bool Suspended;
        std::atomic<bool> Finished{ false }; // set when thread function returns

        static void * /*__stdcall*/ StartProc(void *arg);

    public:
        Thread(bool suspended);

        virtual ~Thread();

        [[nodiscard]] bool GetTerminated() const { return Terminated; }

        void SetTerminated(bool s) { Terminated = s; }

        void Terminate() { Terminated = true; }

    protected:
        virtual void Execute() = 0;

    public:
        void WaitFor();

        ThreadPriority GetPriority();

        void SetPriority(ThreadPriority pri);

        // void Suspend();
        void Resume();

        // 	HANDLE GetHandle() const { return Handle; } 	/* win32 specific
        // */ 	DWORD GetThreadId() const { return ThreadId; }  /* win32
        // specific */
    };

    class ThreadEvent {
        std::condition_variable Handle;
        std::mutex Mutex;

    public:
        void Set();

        void WaitFor(tjs_uint timeout);
    };

    void AddOnThreadExitEvent(const std::function<void()> &ev);

    void OnThreadExited();
} // namespace krkr::utils


#endif
