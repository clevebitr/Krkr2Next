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
// InvokeTJSEvents
//---------------------------------------------------------------------------
bool EventInvoked = false;

void InvokeTJSEvents() {
    if(EventInvoked)
        return;
    EventInvoked = true;
    if(krkr::environ::systemControl) {
        krkr::environ::systemControl->InvokeEvents();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// EventReceived
//---------------------------------------------------------------------------
void EventReceived() {
    EventInvoked = false;
    if(krkr::environ::systemControl)
        krkr::environ::systemControl->NotifyEventDelivered();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CallDeliverAllTJSEventsOnIdle
//---------------------------------------------------------------------------
void CallDeliverAllTJSEventsOnIdle() {
    if(krkr::environ::systemControl) {
        krkr::environ::systemControl->CallDeliverAllEventsOnIdle();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Breathe
//---------------------------------------------------------------------------
static bool Breathing = false;

void Breathe() {
    EventDisabled = true; // not to call TVP events...
    Breathing = true;
    try {
        Application->ProcessMessages(); // do Windows message pumping
    } catch(...) {
        Breathing = false;
        EventDisabled = false;
        throw;
    }

    Breathing = false;
    EventDisabled = false;
}

//---------------------------------------------------------------------------
bool GetBreathing() {
    // return whether now is in event breathing
    return Breathing;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// SystemEventDisabledState
//---------------------------------------------------------------------------
void SetSystemEventDisabledState(bool en) {
    krkr::environ::systemControl->SetEventEnabled(!en);
    if(!en)
        DeliverAllEvents();
}

//---------------------------------------------------------------------------
bool GetSystemEventDisabledState() {
    return !krkr::environ::systemControl->GetEventEnabled();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// ContinuousHandlerCallLimitThread
//---------------------------------------------------------------------------
class ContinuousHandlerCallLimitThread : public krkr::utils::Thread {
    tjs_uint64 NextEventTick;
    tjs_uint64 Interval;
    krkr::utils::ThreadEvent Event;
    tTJSCriticalSection CS;

    bool Enabled;

    NativeEventQueue<ContinuousHandlerCallLimitThread> EventQueue;

public:
    ContinuousHandlerCallLimitThread();

    ~ContinuousHandlerCallLimitThread() override;

protected:
    void Execute() override;

    void WndProc(NativeEvent &ev) { EventQueue.HandlerDefault(ev); }

public:
    void SetEnabled(bool enabled);

    void SetInterval(tjs_uint64 interval) { Interval = interval; }

} static *continuousHandlerCallLimitThread = nullptr;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
ContinuousHandlerCallLimitThread::ContinuousHandlerCallLimitThread() :
    krkr::utils::Thread(true),
    EventQueue(this, &ContinuousHandlerCallLimitThread::WndProc) {
    NextEventTick = 0;
    Interval = (1 << KRKR_SUBMILLI_FRAC_BITS) * 1000 / 60; // default 60Hz
    Enabled = false;
    EventQueue.Allocate();
    Resume();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
ContinuousHandlerCallLimitThread::~ContinuousHandlerCallLimitThread() {
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
    EventQueue.Deallocate();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void ContinuousHandlerCallLimitThread::Execute() {
    while(!GetTerminated()) {
        tjs_uint64 curtick = krkr::utils::GetTickCount() << KRKR_SUBMILLI_FRAC_BITS;
        tjs_uint32 sleeptime;

        { // thread-protected
            tTJSCriticalSectionHolder holder(CS);

            if(Enabled) {
                if(NextEventTick <= curtick) {
                    ProcessContinuousHandlerEventFlag =
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
void ContinuousHandlerCallLimitThread::SetEnabled(bool enabled) {
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
static tjs_int ContinousHandlerLimitFrequency = 0;

//---------------------------------------------------------------------------
void BeginContinuousEvent() {
    // read commandline options
    static tjs_int ArgumentGeneration = 0;
    if(ArgumentGeneration != krkr::base::GetCommandLineArgumentGeneration()) {
        ArgumentGeneration = krkr::base::GetCommandLineArgumentGeneration();

        tTJSVariant val;
        if(krkr::base::GetCommandLine(TJS_W("-contfreq"), &val)) {
            ContinousHandlerLimitFrequency = (tjs_int)val;
        }
    }
    //	if(!IsWaitVSync())
    {
        if(ContinousHandlerLimitFrequency == 0) {
            // no limit
            // this notifies continuous calling of
            // DeliverAllEvents.
            if(krkr::environ::systemControl)
                krkr::environ::systemControl->BeginContinuousEvent();
        } else {
            // has limit
            if(!continuousHandlerCallLimitThread)
                continuousHandlerCallLimitThread =
                    new ContinuousHandlerCallLimitThread();
            continuousHandlerCallLimitThread->SetInterval(
                (1 << KRKR_SUBMILLI_FRAC_BITS) * 1000 /
                ContinousHandlerLimitFrequency);
            continuousHandlerCallLimitThread->SetEnabled(true);
        }
    }

    // EnsureVSyncTimingThread();
    // if we wait vsync, the continuous handler will be executed at
    // the every timing of vsync.
}

//---------------------------------------------------------------------------
void EndContinuousEvent() {
    // anyway
    if(continuousHandlerCallLimitThread)
        continuousHandlerCallLimitThread->SetEnabled(false);

    // anyway
    if(krkr::environ::systemControl)
        krkr::environ::systemControl->EndContinuousEvent();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void ReleaseContinuousHandlerCallLimitThread() {
    if(continuousHandlerCallLimitThread)
        delete continuousHandlerCallLimitThread,
            continuousHandlerCallLimitThread = nullptr;
}

// to release continuousHandlerCallLimitThread at exit
static AtExit
    TimerThreadUninitAtExit(KRKR_ATEXIT_PRI_SHUTDOWN,
                               ReleaseContinuousHandlerCallLimitThread);
//---------------------------------------------------------------------------

} // namespace krkr::base
