//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// safe 64bit System Tick Count
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsUtils.h"
#include "TickCount.h"
#include "SysInitIntf.h"
#include "ThreadIntf.h"

#define DWORD uint32_t

namespace krkr::utils {
    // 64bit may enough to hold usual time count.
    // ( 32bit is clearly insufficient )
    static tjs_uint64 TickCountBias = 0;
    static DWORD WatchLastTick;
    static tTJSCriticalSection TickWatchCS;

    static DWORD CheckTickOverflow() {
        DWORD curtick;
        { // thread-protected
            tTJSCriticalSectionHolder holder(TickWatchCS);

            curtick = krkr::utils::GetRoughTickCount32();
            if(curtick < WatchLastTick) {
                // timeGetTime() was overflowed
                TickCountBias += 0x100000000L; // add 1<<32
            }
            WatchLastTick = curtick;
        } // end-of-thread-protected
        return curtick;
    }

    class WatchThread : public Thread {
        // thread which watches overflow of 32bit counter of
        // krkr::utils::GetRoughTickCount32

        krkr::utils::ThreadEvent Event;

    public:
        WatchThread();

        ~WatchThread() override;

    protected:
        void Execute() override;

    } static *watchThread = nullptr;

    static void WatchThreadInit() {
        if(!watchThread) {
            watchThread = new WatchThread();
        }
    }

    static void WatchThreadUninit() {
        if(watchThread) {
            delete watchThread;
            watchThread = nullptr;
        }
    }

    static AtExit WatchThreadUninitAtExit(KRKR_ATEXIT_PRI_SHUTDOWN,
                                                 WatchThreadUninit);

    // krkr::utils::StartTickCount
    void StartTickCount() { WatchThreadInit(); }

    WatchThread::WatchThread() : Thread(true) {
        WatchLastTick = krkr::utils::GetRoughTickCount32();
        SetPriority(ttpNormal);
        Resume();
    }

    WatchThread::~WatchThread() {
        Terminate();
        Resume();
        Event.Set();
        WaitFor();
    }

    void WatchThread::Execute() {
        while(!GetTerminated()) {
            CheckTickOverflow();

            Event.WaitFor(0x10000000);
            // 0x10000000 will be enough to watch timeGetTime()'s counter
            // overflow.
        }
    }

    // krkr::utils::GetTickCount
    tjs_uint64 GetTickCount() {
        WatchThreadInit();

        DWORD curtick = CheckTickOverflow();

        return curtick + TickCountBias;
    }

} // namespace krkr::utils
