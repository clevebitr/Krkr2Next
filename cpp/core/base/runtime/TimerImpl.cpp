//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Timer Object Implementation
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include "EventIntf.h"
#include "TickCount.h"
#include "TimerImpl.h"
#include "SysInitIntf.h"
#include "ThreadIntf.h"
#include "MsgIntf.h"

#include "NativeEventQueue.h"
#include "UserEvent.h"

//---------------------------------------------------------------------------

// TVP Timer class gives ability of triggering event on punctual
// interval. a large quantity of event at once may easily cause freeze
// to system, so we must trigger only porocess-able quantity of the
// event.
#define TVP_LEAST_TIMER_INTERVAL 3
#define INFINITE 0xFFFFFFFF

namespace krkr::utils {

//---------------------------------------------------------------------------
// TimerThread
//---------------------------------------------------------------------------
class TimerThread : public Thread {
    // thread for triggering punctual event.
    // normal Windows timer cannot call the timer callback routine at
    // too short interval ( roughly less than 50ms ).

    std::vector<tTJSNI_Timer *> List;
    std::vector<tTJSNI_Timer *>
        Pending; // timer object which has pending events
    bool PendingEventsAvailable;
    krkr::utils::ThreadEvent Event;

    NativeEventQueue<krkr::utils::TimerThread> EventQueue;

public:
    tTJSCriticalSection TimerCS;

    krkr::utils::TimerThread();

    ~krkr::utils::TimerThread() override;

protected:
    void Execute() override;

private:
    void Proc(NativeEvent &event);

    void AddItem(tTJSNI_Timer *item);

    bool RemoveItem(tTJSNI_Timer *item);

    void RemoveFromPendingItem(tTJSNI_Timer *item);

    void RegisterToPendingItem(tTJSNI_Timer *item);

public:
    void SetEnabled(tTJSNI_Timer *item,
                    bool enabled); // managed by this class
    void SetInterval(tTJSNI_Timer *item,
                     tjs_uint64 interval); // managed by this class

public:
    static void Init();

    static void Uninit();

    static void Add(tTJSNI_Timer *item);

    static void Remove(tTJSNI_Timer *item);

    static void RemoveFromPending(tTJSNI_Timer *item);

    static void RegisterToPending(tTJSNI_Timer *item);

} static *timerThread = nullptr;

//---------------------------------------------------------------------------
TimerThread::TimerThread() :
    Thread(true), EventQueue(this, &TimerThread::Proc) {
    PendingEventsAvailable = false;
    SetPriority(krkr::utils::limitTimerCapacity ? ttpNormal : ttpHighest);
    EventQueue.Allocate();
    Resume();
}

//---------------------------------------------------------------------------
TimerThread::~TimerThread() {
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
    EventQueue.Deallocate();
}

//---------------------------------------------------------------------------
void TimerThread::Execute() {
    while(!GetTerminated()) {
        tjs_uint64 step_next = (tjs_uint64)(tjs_int64)-1L; // invalid value
        tjs_uint64 curtick = krkr::utils::GetTickCount() << KRKR_SUBMILLI_FRAC_BITS;
        tjs_uint32 sleeptime;

        { // thread-protected
            tTJSCriticalSectionHolder holder(TimerCS);

            bool any_triggered = false;

            std::vector<tTJSNI_Timer *>::iterator i;
            for(i = List.begin(); i != List.end(); i++) {
                tTJSNI_Timer *item = *i;

                if(!item->GetEnabled() || item->GetInterval() == 0)
                    continue;

                if(item->GetNextTick() < curtick) {
                    tjs_uint n = static_cast<tjs_uint>(
                        (curtick - item->GetNextTick()) / item->GetInterval());
                    n++;
                    if(n > 40) {
                        // too large amount of event at once; discard
                        // rest
                        item->Trigger(1);
                        any_triggered = true;
                        item->SetNextTick(curtick + item->GetInterval());
                    } else {
                        item->Trigger(n);
                        any_triggered = true;
                        item->SetNextTick(item->GetNextTick() +
                                          n * item->GetInterval());
                    }
                }

                tjs_uint64 to_next = item->GetNextTick() - curtick;

                if(step_next == (tjs_uint64)(tjs_int64)-1L) {
                    step_next = to_next;
                } else {
                    if(step_next > to_next)
                        step_next = to_next;
                }
            }

            if(step_next != (tjs_uint64)(tjs_int64)-1L) {
                // too large step_next must be diminished to size of
                // DWORD.
                if(step_next >= 0x80000000)
                    sleeptime = 0x7fffffff; // smaller value than
                                            // step_next is OK
                else
                    sleeptime = static_cast<tjs_uint32>(step_next);
            } else {
                sleeptime = INFINITE;
            }

            if(List.size() == 0)
                sleeptime = INFINITE;

            if(any_triggered) {
                // triggered; post notification message to the
                // UtilWindow
                if(!PendingEventsAvailable) {
                    PendingEventsAvailable = true;
                    EventQueue.PostEvent(NativeEvent(TVP_EV_TIMER_THREAD));
                }
            }

        } // end-of-thread-protected

        // now, sleeptime has sub-milliseconds precision but we need
        // millisecond precision time.
        if(sleeptime != INFINITE)
            sleeptime = (sleeptime >> KRKR_SUBMILLI_FRAC_BITS) +
                (sleeptime & ((1 << KRKR_SUBMILLI_FRAC_BITS) - 1)
                     ? 1
                     : 0); // round up

        // clamp to TVP_LEAST_TIMER_INTERVAL ...
        if(sleeptime != INFINITE && sleeptime < TVP_LEAST_TIMER_INTERVAL)
            sleeptime = TVP_LEAST_TIMER_INTERVAL;

        Event.WaitFor(sleeptime); // wait until sleeptime is elapsed
                                  // or Event->SetEvent() is executed.
    }
}

//---------------------------------------------------------------------------
// void __fastcall krkr::utils::TimerThread::UtilWndProc(Messages::TMessage
// &Msg)
void TimerThread::Proc(NativeEvent &ev) {
    // Window procedure of UtilWindow
    if(ev.Message == TVP_EV_TIMER_THREAD && !GetTerminated()) {
        // pending events occur
        tTJSCriticalSectionHolder holder(TimerCS); // protect the object

        std::vector<tTJSNI_Timer *>::iterator i;
        for(i = Pending.begin(); i != Pending.end(); i++) {
            tTJSNI_Timer *item = *i;
            item->FirePendingEventsAndClear();
        }

        Pending.clear();
        PendingEventsAvailable = false;
    } else {
        EventQueue.HandlerDefault(ev);
    }
}

//---------------------------------------------------------------------------
void TimerThread::AddItem(tTJSNI_Timer *item) {
    tTJSCriticalSectionHolder holder(TimerCS);

    if(std::find(List.begin(), List.end(), item) == List.end())
        List.push_back(item);
}

//---------------------------------------------------------------------------
bool TimerThread::RemoveItem(tTJSNI_Timer *item) {
    tTJSCriticalSectionHolder holder(TimerCS);

    std::vector<tTJSNI_Timer *>::iterator i;

    // remove from the List
    for(i = List.begin(); i != List.end(); /**/) {
        if(*i == item)
            i = List.erase(i);
        else
            i++;
    }

    // also remove from the Pending list
    RemoveFromPendingItem(item);

    return List.size() != 0;
}

//---------------------------------------------------------------------------
void TimerThread::RemoveFromPendingItem(tTJSNI_Timer *item) {
    // remove item from pending list
    std::vector<tTJSNI_Timer *>::iterator i;
    for(i = Pending.begin(); i != Pending.end(); /**/) {
        if(*i == item)
            i = Pending.erase(i);
        else
            i++;
    }

    item->ZeroPendingCount();
}

//---------------------------------------------------------------------------
void TimerThread::RegisterToPendingItem(tTJSNI_Timer *item) {
    // register item to the pending list
    Pending.push_back(item);
}

//---------------------------------------------------------------------------
void TimerThread::SetEnabled(tTJSNI_Timer *item, bool enabled) {
    { // thread-protected
        tTJSCriticalSectionHolder holder(TimerCS);

        item->InternalSetEnabled(enabled);
        if(enabled) {
            item->SetNextTick((krkr::utils::GetTickCount() << KRKR_SUBMILLI_FRAC_BITS) +
                              item->GetInterval());
        } else {
            item->CancelEvents();
            item->ZeroPendingCount();
        }
    } // end-of-thread-protected

    if(enabled)
        Event.Set();
}

//---------------------------------------------------------------------------
void TimerThread::SetInterval(tTJSNI_Timer *item, tjs_uint64 interval) {
    { // thread-protected
        tTJSCriticalSectionHolder holder(TimerCS);

        item->InternalSetInterval(interval);
        if(item->GetEnabled()) {
            item->CancelEvents();
            item->ZeroPendingCount();
            item->SetNextTick((krkr::utils::GetTickCount() << KRKR_SUBMILLI_FRAC_BITS) +
                              item->GetInterval());
        }
    } // end-of-thread-protected

    if(item->GetEnabled())
        Event.Set();
}

//---------------------------------------------------------------------------
void TimerThread::Init() {
    if(!timerThread) {
        krkr::utils::StartTickCount(); // in TickCount.cpp
        timerThread = new krkr::utils::TimerThread();
    }
}

//---------------------------------------------------------------------------
void TimerThread::Uninit() {
    if(timerThread) {
        delete timerThread;
        timerThread = nullptr;
    }
}

//---------------------------------------------------------------------------
static AtExit TimerThreadUninitAtExit(KRKR_ATEXIT_PRI_SHUTDOWN,
                                             TimerThread::Uninit);

//---------------------------------------------------------------------------
void TimerThread::Add(tTJSNI_Timer *item) {
    // at this point, item->GetEnebled() must be false.

    Init();

    timerThread->AddItem(item);
}

//---------------------------------------------------------------------------
void TimerThread::Remove(tTJSNI_Timer *item) {
    if(timerThread) {
        if(!timerThread->RemoveItem(item))
            Uninit();
    }
}

//---------------------------------------------------------------------------
void TimerThread::RemoveFromPending(tTJSNI_Timer *item) {
    if(timerThread) {
        timerThread->RemoveFromPendingItem(item);
    }
}

//---------------------------------------------------------------------------
void TimerThread::RegisterToPending(tTJSNI_Timer *item) {
    if(timerThread) {
        timerThread->RegisterToPendingItem(item);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_Timer
//---------------------------------------------------------------------------
tTJSNI_Timer::tTJSNI_Timer() {
    NextTick = 0;
    Interval = 1000;
    PendingCount = 0;
    Enabled = false;
}

//---------------------------------------------------------------------------
tjs_error tTJSNI_Timer::Construct(tjs_int numparams, tTJSVariant **param,
                                  iTJSDispatch2 *tjs_obj) {
    inherited::Construct(numparams, param, tjs_obj);

    krkr::utils::TimerThread::Add(this);
    return TJS_S_OK;
}

//---------------------------------------------------------------------------
void tTJSNI_Timer::Invalidate() {
    krkr::utils::TimerThread::Remove(this);
    ZeroPendingCount();
    CancelEvents();
    inherited::Invalidate(); // this sets Owner = nullptr
}

//---------------------------------------------------------------------------
void tTJSNI_Timer::SetInterval(tjs_uint64 n) {
    timerThread->SetInterval(this, n);
}

//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_Timer::GetInterval() const { return Interval; }

//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_Timer::GetNextTick() const { return NextTick; }

//---------------------------------------------------------------------------
void tTJSNI_Timer::SetEnabled(bool b) { timerThread->SetEnabled(this, b); }

//---------------------------------------------------------------------------
void tTJSNI_Timer::Trigger(tjs_uint n) {
    // this function is called by sub-thread.
    if(PendingCount == 0)
        krkr::utils::TimerThread::RegisterToPending(this);
    PendingCount += n;
}

//---------------------------------------------------------------------------
void tTJSNI_Timer::FirePendingEventsAndClear() {
    // fire all pending events and clear the pending event count
    if(PendingCount) {
        Fire(PendingCount);
        ZeroPendingCount();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Timer
//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_Timer::CreateNativeInstance() {
    return new tTJSNI_Timer();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Timer
//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Timer() { return new tTJSNC_Timer(); }
//---------------------------------------------------------------------------

} // namespace krkr::utils
