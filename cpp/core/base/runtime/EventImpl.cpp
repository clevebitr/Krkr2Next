//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Script Event Handling and Dispatching
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "EventImpl.h"
#include "SystemControl.h"
#include "ThreadIntf.h"
#include "TickCount.h"
#include "TimerIntf.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "WindowImpl.h"
// #include <mmsystem.h>

#include "Application.h"
#include "NativeEventQueue.h"
#include "UserEvent.h"

namespace krkr::base {

//---------------------------------------------------------------------------
// TVPInvokeEvents
//---------------------------------------------------------------------------
bool TVPEventInvoked = false;

void TVPInvokeEvents() {
    if(TVPEventInvoked)
        return;
    TVPEventInvoked = true;
    if(krkr::environ::GetSystemControl) {
        krkr::environ::GetSystemControl->InvokeEvents();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPEventReceived
//---------------------------------------------------------------------------
void TVPEventReceived() {
    TVPEventInvoked = false;
    if(krkr::environ::GetSystemControl)
        krkr::environ::GetSystemControl->NotifyEventDelivered();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCallDeliverAllEventsOnIdle
//---------------------------------------------------------------------------
void TVPCallDeliverAllEventsOnIdle() {
    if(krkr::environ::GetSystemControl) {
        krkr::environ::GetSystemControl->CallDeliverAllEventsOnIdle();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPBreathe
//---------------------------------------------------------------------------
static bool TVPBreathing = false;

void TVPBreathe() {
    TVPEventDisabled = true; // not to call TVP events...
    TVPBreathing = true;
    try {
        Application->ProcessMessages(); // do Windows message pumping
    } catch(...) {
        TVPBreathing = false;
        TVPEventDisabled = false;
        throw;
    }

    TVPBreathing = false;
    TVPEventDisabled = false;
}

//---------------------------------------------------------------------------
bool TVPGetBreathing() {
    // return whether now is in event breathing
    return TVPBreathing;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSystemEventDisabledState
//---------------------------------------------------------------------------
void TVPSetSystemEventDisabledState(bool en) {
    krkr::environ::GetSystemControl->SetEventEnabled(!en);
    if(!en)
        TVPDeliverAllEvents();
}

//---------------------------------------------------------------------------
bool TVPGetSystemEventDisabledState() {
    return !krkr::environ::GetSystemControl->GetEventEnabled();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPContinuousHandlerCallLimitThread
//---------------------------------------------------------------------------
class tTVPContinuousHandlerCallLimitThread : public krkr::utils::Thread {
    tjs_uint64 NextEventTick;
    tjs_uint64 Interval;
    krkr::utils::ThreadEvent Event;
    tTJSCriticalSection CS;

    bool Enabled;

    NativeEventQueue<tTVPContinuousHandlerCallLimitThread> EventQueue;

public:
    tTVPContinuousHandlerCallLimitThread();

    ~tTVPContinuousHandlerCallLimitThread() override;

protected:
    void Execute() override;

    void WndProc(NativeEvent &ev) { EventQueue.HandlerDefault(ev); }

public:
    void SetEnabled(bool enabled);

    void SetInterval(tjs_uint64 interval) { Interval = interval; }

} static *TVPContinuousHandlerCallLimitThread = nullptr;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTVPContinuousHandlerCallLimitThread::tTVPContinuousHandlerCallLimitThread() :
    krkr::utils::Thread(true),
    EventQueue(this, &tTVPContinuousHandlerCallLimitThread::WndProc) {
    NextEventTick = 0;
    Interval = (1 << KRKR_SUBMILLI_FRAC_BITS) * 1000 / 60; // default 60Hz
    Enabled = false;
    EventQueue.Allocate();
    Resume();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTVPContinuousHandlerCallLimitThread::~tTVPContinuousHandlerCallLimitThread() {
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
    EventQueue.Deallocate();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPContinuousHandlerCallLimitThread::Execute() {
    while(!GetTerminated()) {
        tjs_uint64 curtick = krkr::utils::GetTickCount() << KRKR_SUBMILLI_FRAC_BITS;
        tjs_uint32 sleeptime;

        { // thread-protected
            tTJSCriticalSectionHolder holder(CS);

            if(Enabled) {
                if(NextEventTick <= curtick) {
                    TVPProcessContinuousHandlerEventFlag =
                        true; // set flag to process event on next
                              // idle
                    EventQueue.PostEvent(
                        NativeEvent(TVP_EV_CONTINUE_LIMIT_THREAD));
                    while(NextEventTick <= curtick)
                        NextEventTick += Interval;
                }
                tjs_uint64 sleeptime_64 = NextEventTick - curtick;
                sleeptime =
                    (tjs_uint32)(sleeptime_64 >> KRKR_SUBMILLI_FRAC_BITS) +
                    ((sleeptime_64 & ((1 << KRKR_SUBMILLI_FRAC_BITS) - 1)) ? 1
                                                                          : 0);
                // add 1 if fraction exists
            } else {
                sleeptime = 10000; // how long to sleep when disabled
                                   // does not matter
            }

        } // end-of-thread-protected

        if(sleeptime == 0)
            sleeptime = 1; // 0 will let thread sleeping forever ...
        Event.WaitFor(sleeptime);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPContinuousHandlerCallLimitThread::SetEnabled(bool enabled) {
    tTJSCriticalSectionHolder holder(CS);

    Enabled = enabled;
    if(enabled) {
        tjs_uint64 curtick = krkr::utils::GetTickCount() << KRKR_SUBMILLI_FRAC_BITS;
        NextEventTick = ((curtick + 1) / Interval) * Interval;
        Event.Set();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static tjs_int TVPContinousHandlerLimitFrequency = 0;

//---------------------------------------------------------------------------
void TVPBeginContinuousEvent() {
    // read commandline options
    static tjs_int ArgumentGeneration = 0;
    if(ArgumentGeneration != krkr::base::GetCommandLineArgumentGeneration()) {
        ArgumentGeneration = krkr::base::GetCommandLineArgumentGeneration();

        tTJSVariant val;
        if(krkr::base::GetCommandLine(TJS_W("-contfreq"), &val)) {
            TVPContinousHandlerLimitFrequency = (tjs_int)val;
        }
    }
    //	if(!TVPIsWaitVSync())
    {
        if(TVPContinousHandlerLimitFrequency == 0) {
            // no limit
            // this notifies continuous calling of
            // TVPDeliverAllEvents.
            if(krkr::environ::GetSystemControl)
                krkr::environ::GetSystemControl->BeginContinuousEvent();
        } else {
            // has limit
            if(!TVPContinuousHandlerCallLimitThread)
                TVPContinuousHandlerCallLimitThread =
                    new tTVPContinuousHandlerCallLimitThread();
            TVPContinuousHandlerCallLimitThread->SetInterval(
                (1 << KRKR_SUBMILLI_FRAC_BITS) * 1000 /
                TVPContinousHandlerLimitFrequency);
            TVPContinuousHandlerCallLimitThread->SetEnabled(true);
        }
    }

    // TVPEnsureVSyncTimingThread();
    // if we wait vsync, the continuous handler will be executed at
    // the every timing of vsync.
}

//---------------------------------------------------------------------------
void TVPEndContinuousEvent() {
    // anyway
    if(TVPContinuousHandlerCallLimitThread)
        TVPContinuousHandlerCallLimitThread->SetEnabled(false);

    // anyway
    if(krkr::environ::GetSystemControl)
        krkr::environ::GetSystemControl->EndContinuousEvent();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPReleaseContinuousHandlerCallLimitThread() {
    if(TVPContinuousHandlerCallLimitThread)
        delete TVPContinuousHandlerCallLimitThread,
            TVPContinuousHandlerCallLimitThread = nullptr;
}

// to release TVPContinuousHandlerCallLimitThread at exit
static tTVPAtExit
    TVPTimerThreadUninitAtExit(KRKR_ATEXIT_PRI_SHUTDOWN,
                               TVPReleaseContinuousHandlerCallLimitThread);
//---------------------------------------------------------------------------

} // namespace krkr::base
