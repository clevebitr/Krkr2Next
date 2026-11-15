//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Script/Window Event Handling and Dispatching / System Idle Event
// Delivering
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include "SysInitIntf.h"
#include "EventIntf.h"
#include "WindowIntf.h"
#include "tjsDictionary.h"
#include "MsgIntf.h"
#include "ScriptMgnIntf.h"
#include "TickCount.h"
#include "SystemImpl.h"
#include <spdlog/spdlog.h>

//---------------------------------------------------------------------------
// Event  : script event class
//---------------------------------------------------------------------------
namespace krkr::base {

extern tjs_uint64 EventSequenceNumber;

class Event {
    iTJSDispatch2 *Target;
    iTJSDispatch2 *Source;
    ttstr EventName;
    tjs_uint32 Tag;
    tjs_uint NumArgs;
    tTJSVariant *Args;
    tjs_uint32 Flags;
    tjs_uint64 Sequence;

public:
    Event(iTJSDispatch2 *target, iTJSDispatch2 *source, ttstr &eventname,
              tjs_uint32 tag, tjs_uint numargs, tTJSVariant *args,
              tjs_uint32 flags) {
        // constructor

        // eventname is not a const object but this object only touch
        // to eventname.GetHint()

        Args = nullptr;
        Target = nullptr;
        Source = nullptr;

        Sequence = EventSequenceNumber;
        EventName = eventname;
        NumArgs = numargs;
        Args = new tTJSVariant[NumArgs];
        for(tjs_uint i = 0; i < NumArgs; i++)
            Args[i] = args[i];
        Target = target;
        Source = source;
        Tag = tag;
        Flags = flags;
        if(Target)
            Target->AddRef();
        if(Source)
            Source->AddRef();
    }

    Event(const Event &ref) {
        // copy constructor
        Args = nullptr;
        Target = nullptr;
        Source = nullptr;

        EventName = ref.EventName;
        NumArgs = ref.NumArgs;
        Args = new tTJSVariant[NumArgs];
        for(tjs_uint i = 0; i < NumArgs; i++)
            Args[i] = ref.Args[i];
        Target = ref.Target;
        Source = ref.Source;
        Tag = ref.Tag;
        if(Target)
            Target->AddRef();
        if(Source)
            Source->AddRef();
    }

    ~Event() {
        if(Args)
            delete[] Args;
        if(Target)
            Target->Release();
        if(Source)
            Source->Release();
    }

    void Deliver() {
        if(!TJSIsObjectValid(Target->IsValid(0, nullptr, nullptr, Target)))
            return; // The target had been invalidated
        tTJSVariant **ArgsPtr = new tTJSVariant *[NumArgs];
        for(tjs_uint i = 0; i < NumArgs; i++)
            ArgsPtr[i] = Args + i;
        try {
            Target->FuncCall(0, EventName.c_str(), EventName.GetHint(), nullptr,
                             NumArgs, ArgsPtr, Target);
        } catch(...) {
            delete[] ArgsPtr;
            throw;
        }
        delete[] ArgsPtr;
    }

    iTJSDispatch2 *GetTargetNoAddRef() const { return Target; }

    iTJSDispatch2 *GetSourceNoAddRef() const { return Source; }

    ttstr &GetEventName() { return EventName; }

    tjs_uint32 GetTag() const { return Tag; }

    tjs_uint32 GetFlags() const { return Flags; }

    tjs_uint64 GetSequence() const;
};

//---------------------------------------------------------------------------
tjs_uint64 Event::GetSequence() const { return Sequence; }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// WinUpdateEvent : window update event class
//---------------------------------------------------------------------------
class WinUpdateEvent {
    tTJSNI_BaseWindow *Window;

public:
    WinUpdateEvent(tTJSNI_BaseWindow *window) { Window = window; }

    WinUpdateEvent(const WinUpdateEvent &ref) { Window = ref.Window; }

    ~WinUpdateEvent() = default;

    void Deliver() const {
        if(static_cast<tTJSNI_Window *>(Window)->GetVisible())
            Window->UpdateContent();
    }

    tTJSNI_BaseWindow *GetWindow() const { return Window; }

    void MarkEmpty() { Window = nullptr; }

    bool IsEmpty() const { return Window == nullptr; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// global/static definitions
//---------------------------------------------------------------------------
// event queue must be a globally sequential queue
std::vector<BaseInputEvent *> InputEventQueue;
std::vector<Event *> EventQueue;
std::vector<WinUpdateEvent> WinUpdateEventQueue;
bool ExclusiveEventPosted = false; // true if exclusive event is posted
tjs_uint64 EventSequenceNumber = 0; // event sequence number
tjs_uint64 EventSequenceNumberToProcess = 0;
// current event sequence which must be processed

static void DestroyEventQueue() {
    spdlog::info("at-exit PREPARE[1/4]: DestroyEventQueue begin");
    // delete all event objects
    // deletion of event object may cause other deletion of event
    // objects.
    {
        std::vector<Event *>::iterator i;
        while(EventQueue.size()) {
            i = EventQueue.end() - 1;
            Event *ev = *i;
            EventQueue.erase(i);
            delete ev;
        }
    }
    //--
    {
        std::vector<BaseInputEvent *>::iterator i;
        while(InputEventQueue.size()) {
            i = InputEventQueue.end() - 1;
            BaseInputEvent *ev = *i;
            InputEventQueue.erase(i);
            delete ev;
        }
    }
}

static AtExit DestroyEventQueueAtExit(KRKR_ATEXIT_PRI_PREPARE,
                                             DestroyEventQueue);

bool EventDisabled = false;
bool EventInterrupting = false;

// #define TVP_EVENT_TASK_RETURN_TICK 100000
/* TVP event system once returns to Operation system when
    TVP_EVENT_TASK_RETURN_TICK is elapsed during event delivering. */
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// PostTJSEvent
//---------------------------------------------------------------------------
void PostTJSEvent(iTJSDispatch2 *source, iTJSDispatch2 *target,
                  ttstr &eventname, tjs_uint32 tag, tjs_uint32 flag,
                  tjs_uint numargs, tTJSVariant *args) {
    bool evdisabled = EventDisabled || GetSystemEventDisabledState();

    if((flag & TVP_EPT_DISCARDABLE) && (EventInterrupting || evdisabled))
        return;

    tjs_int method = flag & TVP_EPT_METHOD_MASK;

    if(method == TVP_EPT_IMMEDIATE) {
        // the event is delivered immediately

        if(evdisabled)
            return;

        try {
            try {
                Event(target, source, eventname, tag, numargs, args, flag)
                    .Deliver();
            }
            TJS_CONVERT_TO_TJS_EXCEPTION
        }
        TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("immediate event"));

        return;
    }

    if(method == TVP_EPT_REMOVE_POST) {
        // events in queue that have same target/source/name/tag are
        // to be removed
        std::vector<Event *>::iterator i;
        i = EventQueue.begin();
        while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
            if(source == (*i)->GetSourceNoAddRef() &&
               target == (*i)->GetTargetNoAddRef() &&
               eventname == (*i)->GetEventName() &&
               ((tag == 0) ? true : (tag == (*i)->GetTag()))) {
                Event *ev = *i;
                EventQueue.erase(i);
                i = EventQueue.begin();
                delete ev;
            } else {
                i++;
            }
        }
    }

    // put into queue
    EventQueue.push_back(
        new Event(target, source, eventname, tag, numargs, args, flag));

    // is exclusive?
    if((flag & TVP_EPT_PRIO_MASK) == TVP_EPT_EXCLUSIVE)
        ExclusiveEventPosted = true;

    // make sure that the event is to be delivered.
    InvokeTJSEvents();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CancelTJSEvents
//---------------------------------------------------------------------------
tjs_int CancelTJSEvents(iTJSDispatch2 *source, iTJSDispatch2 *target,
                        const ttstr &eventname, tjs_uint32 tag) {
    tjs_int count = 0;
    std::vector<Event *>::iterator i;
    i = EventQueue.begin();
    while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
        if(source == (*i)->GetSourceNoAddRef() &&
           target == (*i)->GetTargetNoAddRef() &&
           eventname == (*i)->GetEventName() &&
           ((tag == 0) ? true : (tag == (*i)->GetTag()))) {
            Event *ev = *i;
            EventQueue.erase(i);
            i = EventQueue.begin();
            delete ev;
            count++;
        } else {
            i++;
        }
    }
    return count;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// AreTJSEventsInQueue
//---------------------------------------------------------------------------
bool AreTJSEventsInQueue(iTJSDispatch2 *source, iTJSDispatch2 *target,
                         const ttstr &eventname, tjs_uint32 tag) {
    std::vector<Event *>::iterator i;
    i = EventQueue.begin();
    while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
        if(source == (*i)->GetSourceNoAddRef() &&
           target == (*i)->GetTargetNoAddRef() &&
           eventname == (*i)->GetEventName() &&
           ((tag == 0) ? true : (tag == (*i)->GetTag())))
            return true;
        i++;
    }
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CountEventsInQueue
//---------------------------------------------------------------------------
tjs_int CountEventsInQueue(iTJSDispatch2 *source, iTJSDispatch2 *target,
                              const ttstr &eventname, tjs_uint32 tag) {
    tjs_int count = 0;
    std::vector<Event *>::iterator i;
    i = EventQueue.begin();
    while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
        if(source == (*i)->GetSourceNoAddRef() &&
           target == (*i)->GetTargetNoAddRef() &&
           eventname == (*i)->GetEventName() &&
           ((tag == 0) ? true : (tag == (*i)->GetTag())))
            count++;
        i++;
    }
    return count;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CancelEventByTag
//---------------------------------------------------------------------------
void CancelEventsByTag(iTJSDispatch2 *source, iTJSDispatch2 *target,
                          tjs_uint32 tag) {
    std::vector<Event *>::iterator i;
    i = EventQueue.begin();
    while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
        if(source == (*i)->GetSourceNoAddRef() &&
           target == (*i)->GetTargetNoAddRef() &&
           ((tag == 0) ? true : (tag == (*i)->GetTag()))) {
            Event *ev = *i;
            EventQueue.erase(i);
            i = EventQueue.begin();
            delete ev;
        } else {
            i++;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CancelSourceEvent
//---------------------------------------------------------------------------
void CancelSourceEvents(iTJSDispatch2 *source) {
    std::vector<Event *>::iterator i;
    i = EventQueue.begin();
    while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
        if(source == (*i)->GetSourceNoAddRef()) {
            Event *ev = *i;
            EventQueue.erase(i);
            i = EventQueue.begin();
            delete ev;
        } else {
            i++;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// DiscardAllDiscardableEvents
//---------------------------------------------------------------------------
void DiscardAllDiscardableEvents() {
    std::vector<Event *>::iterator i;
    i = EventQueue.begin();
    while(/*EventQueue.size() &&*/ i != EventQueue.end()) {
        if((*i)->GetFlags() & TVP_EPT_DISCARDABLE) {
            Event *ev = *i;
            EventQueue.erase(i);
            i = EventQueue.begin();
            delete ev;
        } else {
            i++;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// DeliverAllEvents
//---------------------------------------------------------------------------
static void _TVPDeliverEventByPrio(tjs_uint prio) {
    while(true) {
        Event *e;

        // retrieve item to deliver
        if(EventQueue.size() == 0)
            break;
        std::vector<Event *>::iterator i = EventQueue.begin();
        while(i != EventQueue.end()) {
            if((*i)->GetSequence() <= EventSequenceNumberToProcess &&
               (((*i)->GetFlags() & TVP_EPT_PRIO_MASK) == prio))
                break;
            i++;
        }
        if(i == EventQueue.end())
            break;
        e = *i;
        EventQueue.erase(i);

        // event delivering
        try {
            e->Deliver();
        } catch(...) {
            delete e;
            throw;
        }
        delete e;
    }
}

static bool _TVPDeliverAllEvents2() {
    ExclusiveEventPosted = false;

    // process exclusive events
    _TVPDeliverEventByPrio(TVP_EPT_EXCLUSIVE);

    // check exclusive events
    if(ExclusiveEventPosted)
        return true;

    // process input event queue
    while(true) {
        BaseInputEvent *e;

        // retrieve item to deliver
        if(InputEventQueue.size() == 0)
            break;
        std::vector<BaseInputEvent *>::iterator i =
            InputEventQueue.begin();
        e = *i;
        InputEventQueue.erase(i);

        // event delivering
        try {
            e->Deliver();
        } catch(...) {
            delete e;
            throw;
        }
        delete e;

        // check exclusive events
        if(ExclusiveEventPosted)
            return true;
    }

    // process normal event queue
    _TVPDeliverEventByPrio(TVP_EPT_NORMAL);

    // check exclusive events
    if(ExclusiveEventPosted)
        return true;

    return true;
}

//---------------------------------------------------------------------------
static bool _TVPDeliverAllEvents() {
    // deliver all pending events to targets.
    if(EventDisabled)
        return true;

    // event invokation was received...
    EventReceived();

    // for script event objects

    bool ret_value;

    ret_value = _TVPDeliverAllEvents2();

    return ret_value;
}

//---------------------------------------------------------------------------
void DeliverAllEvents() {
    bool r;

    if(!EventInterrupting) {
        EventSequenceNumberToProcess = EventSequenceNumber;
        EventSequenceNumber++; // increment sequence number
    }

    EventInterrupting = false;
    try {
        try {
            r = _TVPDeliverAllEvents();
        }
        TJS_CONVERT_TO_TJS_EXCEPTION
    }
    TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("event"));

    if(!r) {
        // event processing is to be interrupted
        // XXX: currently this is not functional
        EventInterrupting = true;
        CallDeliverAllTJSEventsOnIdle();
    }

    if(!ExclusiveEventPosted && !EventInterrupting) {
        try {
            try {
                // process idle event queue
                _TVPDeliverEventByPrio(TVP_EPT_IDLE);
            }
            TJS_CONVERT_TO_TJS_EXCEPTION
        }
        TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("idle event"));

        // process continuous events
        if(ProcessContinuousHandlerEventFlag) {
            ProcessContinuousHandlerEventFlag = false; // processed
            // XXX: strictly saying, we need something like
            // InterlockedExchange to look/set this flag, because
            // ProcessContinuousHandlerEventFlag may be accessed by
            // another thread. But I have no dought about that no one
            // does care of missing one event in rare race condition.

            DeliverContinuousEvent();
        }
        try {
            try {
                // for window content updating
                DeliverWindowUpdateEvents();
            }
            TJS_CONVERT_TO_TJS_EXCEPTION
        }
        TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("window update"));
    } else {
    }

    if(EventQueue.size() == 0) {
        EventSequenceNumber = 0; // reset the number
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// PostWindowUpdate
//---------------------------------------------------------------------------
bool WindowUpdateEventsDelivering = false;

void PostWindowUpdate(tTJSNI_BaseWindow *window) {

    if(!WindowUpdateEventsDelivering) {
        if(WinUpdateEventQueue.size()) {
            // since duplication is not allowed ...
            std::vector<WinUpdateEvent>::const_iterator i;
            for(i = WinUpdateEventQueue.begin();
                i != WinUpdateEventQueue.end(); i++) {
                if(!i->IsEmpty() && window == i->GetWindow())
                    return;
            }
        }
    } else {
        if(WinUpdateEventQueue.size()) {
            // duplication is allowed up to two
            tjs_int count = 0;
            std::vector<WinUpdateEvent>::const_iterator i;
            for(i = WinUpdateEventQueue.begin();
                i != WinUpdateEventQueue.end(); i++) {
                if(!i->IsEmpty() && window == i->GetWindow()) {
                    count++;
                    if(count == 2)
                        return;
                }
            }
        }
    }

    // put into queue.
    WinUpdateEventQueue.emplace_back(window);

    // make sure that the event is to be delivered.
    InvokeTJSEvents();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// RemoveWindowUpdate
//---------------------------------------------------------------------------
void RemoveWindowUpdate(tTJSNI_BaseWindow *window) {
    // removes all window update events from queue.
    if(WinUpdateEventQueue.size()) {
        std::vector<WinUpdateEvent>::iterator i;
        for(i = WinUpdateEventQueue.begin();
            i != WinUpdateEventQueue.end(); i++) {
            if(!i->IsEmpty() && window == i->GetWindow())
                i->MarkEmpty();
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// DeliverWindowUpdateEvents
//---------------------------------------------------------------------------
void DeliverWindowUpdateEvents() {
    if(WindowUpdateEventsDelivering)
        return; // does not allow re-entering
    // ── 重绘调度探针（仅 KRKR_RENDER_PROBE）：
    // 二次打开若此处一直有 non-empty queue 却被投递，但 RTProbe 无 blit
    //   -> win update 在投递、失败在 UpdateContent/Show 段；
    // 若此处始终空/不被调用                        ->
    // 游戏/脚本根本没请求重绘（RequestUpdate 未发生）。
    WindowUpdateEventsDelivering = true;

    try {
        for(tjs_uint i = 0; i < WinUpdateEventQueue.size(); i++) {
            if(!WinUpdateEventQueue[i].IsEmpty())
                WinUpdateEventQueue[i].Deliver();
        }
    } catch(...) {
        WinUpdateEventQueue.clear();
        WindowUpdateEventsDelivering = false;
        throw;
    }
    WinUpdateEventQueue.clear();
    WindowUpdateEventsDelivering = false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Input Event related
//---------------------------------------------------------------------------
tjs_int InputEventTagMax = 0;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// PostInputEvent
//---------------------------------------------------------------------------
void PostInputEvent(BaseInputEvent *ev, tjs_uint32 flags) {
    // flag check
    if((flags & TVP_EPT_DISCARDABLE) &&
       (EventDisabled || GetSystemEventDisabledState())) {
        delete ev;
        return;
    }

    if(flags & TVP_EPT_REMOVE_POST) {
        // cancel previously posted events
        CancelInputEvents(ev->GetSource(), ev->GetTag());
    }

    // push into the event queue
    InputEventQueue.push_back(ev);

    // make sure that the event is to be delivered.
    InvokeTJSEvents();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CancelInputEvents
//---------------------------------------------------------------------------
void CancelInputEvents(void *source) {
    // removes all evens which have the same source
    if(InputEventQueue.size()) {
        std::vector<BaseInputEvent *>::iterator i;
        for(i = InputEventQueue.begin(); i != InputEventQueue.end();) {
            if(source == (*i)->GetSource()) {
                BaseInputEvent *ev = *i;
                i = InputEventQueue.erase(i);
                delete ev;
            } else {
                i++;
            }
        }
    }
}

//---------------------------------------------------------------------------
void CancelInputEvents(void *source, tjs_int tag) {
    // removes all evens which have the same source and the same tag
    if(InputEventQueue.size()) {
        std::vector<BaseInputEvent *>::iterator i;
        for(i = InputEventQueue.begin(); i != InputEventQueue.end();) {
            if(source == (*i)->GetSource() && tag == (*i)->GetTag()) {
                BaseInputEvent *ev = *i;
                i = InputEventQueue.erase(i);
                delete ev;
            } else {
                i++;
            }
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// GetInputEventCount
//---------------------------------------------------------------------------
tjs_int GetInputEventCount() { return (tjs_int)InputEventQueue.size(); }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CreateEventObject
//---------------------------------------------------------------------------
iTJSDispatch2 *CreateEventObject(const tjs_char *type,
                                    iTJSDispatch2 *targthis,
                                    iTJSDispatch2 *targ) {
    // create a dictionary object for event dispatching ( to "action"
    // method )
    iTJSDispatch2 *object = TJSCreateDictionaryObject();

    static ttstr type_name(TJS_W("type"));
    static ttstr target_name(TJS_W("target"));

    {
        tTJSVariant val(type);
        if(TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                      type_name.c_str(), type_name.GetHint(),
                                      &val, object)))
            KRKR_THROW_INTERNAL_ERROR;
    }

    {
        tTJSVariant val(targthis, targ);
        if(TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                      target_name.c_str(),
                                      target_name.GetHint(), &val, object)))
            KRKR_THROW_INTERNAL_ERROR;
    }

    return object;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
ttstr ActionName(TJS_W("action"));
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Continuous Event Delivering related
//---------------------------------------------------------------------------
bool ProcessContinuousHandlerEventFlag = false;
static std::vector<ContinuousEventCallbackIntf *> continuousEventVector;
static std::vector<tTJSVariantClosure> continuousHandlerVector;
static bool ContinuousEventProcessing = false;

static void DestroyContinuousHandlerVector() {
    spdlog::info("at-exit PREPARE[2/4]: DestroyContinuousHandlerVector begin");
    std::vector<tTJSVariantClosure>::iterator i;
    for(i = continuousHandlerVector.begin();
        i != continuousHandlerVector.end(); i++) {
        i->Release();
    }
    continuousHandlerVector.clear();
}

static AtExit
    DestroyContinuousHandlerVectorAtExit(KRKR_ATEXIT_PRI_PREPARE,
                                            DestroyContinuousHandlerVector);

//---------------------------------------------------------------------------
void AddContinuousEventHook(ContinuousEventCallbackIntf *cb) {
    BeginContinuousEvent();
    continuousEventVector.push_back(cb);
}

//---------------------------------------------------------------------------
void RemoveContinuousEventHook(ContinuousEventCallbackIntf *cb) {
    std::vector<ContinuousEventCallbackIntf *>::iterator i;
    for(i = continuousEventVector.begin();
        i != continuousEventVector.end();) {
        if(cb == *i)
            *i = nullptr; // simply assign a nullptr
        i++;
    }
}

//---------------------------------------------------------------------------
static void _TVPDeliverContinuousEvent() // internal
{
    krkr::utils::StartTickCount();
    tjs_uint64 tick = krkr::utils::GetTickCount();

    if(continuousEventVector.size()) {
        bool emptyflag = false;
        for(tjs_uint32 i = 0; i < continuousEventVector.size(); i++) {
            // note that the handler can remove itself while the event
            if(continuousEventVector[i])
                continuousEventVector[i]->OnContinuousCallback(tick);
            else
                emptyflag = true;

            if(ExclusiveEventPosted)
                return; // check exclusive events
        }

        if(emptyflag) {
            // the array has empty cell

            // eliminate empty
            std::vector<ContinuousEventCallbackIntf *>::iterator i;
            for(i = continuousEventVector.begin();
                i != continuousEventVector.end();) {
                if(*i == nullptr)
                    i = continuousEventVector.erase(i);
                else
                    i++;
            }
        }
    }

    if(!EventDisabled && continuousHandlerVector.size()) {
        bool emptyflag = false;
        tTJSVariant vtick((tjs_int64)tick);
        tTJSVariant *pvtick = &vtick;
        for(tjs_uint i = 0; i < continuousHandlerVector.size(); i++) {
            if(continuousHandlerVector[i].Object) {
                tjs_error er;
                try {
                    er = continuousHandlerVector[i].FuncCall(
                        0, nullptr, nullptr, nullptr, 1, &pvtick, nullptr);
                } catch(...) {
                    // failed
                    continuousHandlerVector[i].Release();
                    continuousHandlerVector[i].Object =
                        continuousHandlerVector[i].ObjThis = nullptr;
                    throw;
                }
                if(TJS_FAILED(er)) {
                    // failed
                    continuousHandlerVector[i].Release();
                    continuousHandlerVector[i].Object =
                        continuousHandlerVector[i].ObjThis = nullptr;
                    emptyflag = true;
                }
                if(ExclusiveEventPosted)
                    return; // check exclusive events
            } else {
                emptyflag = true;
            }
        }

        if(emptyflag) {
            // the array has empty cell

            // eliminate empty
            std::vector<tTJSVariantClosure>::iterator i;
            for(i = continuousHandlerVector.begin();
                i != continuousHandlerVector.end();) {
                if(!i->Object) {
                    i->Release();
                    i = continuousHandlerVector.erase(i);
                } else {
                    i++;
                }
            }
        }
    }

    if(!continuousEventVector.size() && !continuousHandlerVector.size())
        EndContinuousEvent();
}

//---------------------------------------------------------------------------
void DeliverContinuousEvent() {
    if(ContinuousEventProcessing)
        return;
    // ── 脚本驱动探针（仅
    // KRKR_RENDER_PROBE）：连续处理器/连续事件是否注册并在跑。
    // 二次打开若两向量都空且不再增长 ->
    // 游戏没注册每帧连续驱动（脚本推进/连续重绘未建立）。
    ContinuousEventProcessing = true;
    try {
        try {
            try {
                _TVPDeliverContinuousEvent();
            } catch(...) {
                ContinuousEventProcessing = false;
                throw;
            }
        }
        TJS_CONVERT_TO_TJS_EXCEPTION
    }
    TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("continuous event"));

    ContinuousEventProcessing = false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void AddContinuousHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(continuousHandlerVector.begin(),
                  continuousHandlerVector.end(), clo);
    if(i == continuousHandlerVector.end()) {
        BeginContinuousEvent();
        clo.AddRef();
        continuousHandlerVector.emplace_back(clo);
    }
}

//---------------------------------------------------------------------------
void RemoveContinuousHandler(tTJSVariantClosure clo) {
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(continuousHandlerVector.begin(),
                  continuousHandlerVector.end(), clo);
    if(i != continuousHandlerVector.end()) {
        i->Release();
        i->Object = i->ObjThis = nullptr;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// "Compact" Event Delivering related
//---------------------------------------------------------------------------
// Compact events are to be delivered when:
// 1. the application is in idle state for long duration
// 2. the application had been deactivated ( application has lost the
// focus )
// 3. the application had been minimized
// these are to reduce memory usage, like garbage collection, cache
// cleaning, or etc ...
//---------------------------------------------------------------------------
static std::vector<CompactEventCallbackIntf *> CompactEventVector;
bool enableGlobalHeapCompaction = false;

//---------------------------------------------------------------------------
void AddCompactEventHook(CompactEventCallbackIntf *cb) {
    CompactEventVector.push_back(cb);
}

//---------------------------------------------------------------------------
void RemoveCompactEventHook(CompactEventCallbackIntf *cb) {
    std::vector<CompactEventCallbackIntf *>::iterator i;
    for(i = CompactEventVector.begin(); i != CompactEventVector.end();) {
        if(cb == *i)
            *i = nullptr; // simply assign a nullptr
        i++;
    }
}

//---------------------------------------------------------------------------
// DoSaveSystemVariables 的声明由 SystemImpl.h 提供（已在上面 include）。

void DeliverCompactEvent(tjs_int level) {
    // must be called by each platforms's implementation
    // std::vector<CompactEventCallbackIntf *>::iterator i;
    if(CompactEventVector.size()) {
        bool emptyflag = false;
        for(tjs_uint i = 0; i < CompactEventVector.size(); i++) {
            // note that the handler can remove itself while the event
            try {
                try {
                    if(CompactEventVector[i])
                        CompactEventVector[i]->OnCompact(level);
                    else
                        emptyflag = true;
                }
                TJS_CONVERT_TO_TJS_EXCEPTION
            }
            TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION_FORCE_SHOW_EXCEPTION(
                TJS_W("Compact Event"));
        }

        if(emptyflag) {
            // the array has empty cell

            // eliminate empty
            std::vector<CompactEventCallbackIntf *>::iterator i;
            for(i = CompactEventVector.begin();
                i != CompactEventVector.end();) {
                if(*i == nullptr)
                    i = CompactEventVector.erase(i);
                else
                    i++;
            }
        }
    }
    DoSaveSystemVariables();
#if 0
    if( level >= TVP_COMPACT_LEVEL_MAX && enableGlobalHeapCompaction )
    {	// Do compact CRT and Global Heap
        HANDLE hHeap = ::GetProcessHeap();
        if( hHeap ) {
            ::HeapCompact( hHeap, 0 );
        }
        HANDLE hCrtHeap = (HANDLE)_get_heap_handle();
        if( hCrtHeap && hCrtHeap != hHeap ) {
            ::HeapCompact( hCrtHeap, 0 );
        }
    }
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// AsyncTrigger related
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_AsyncTrigger
//---------------------------------------------------------------------------
tTJSNI_AsyncTrigger::tTJSNI_AsyncTrigger() {
    Owner = nullptr;
    Cached = true;
    IdlePendingCount = 0;
    Mode = atmNormal;
    ActionOwner.Object = ActionOwner.ObjThis = nullptr;
    ActionName = ActionName;
}

//---------------------------------------------------------------------------
tjs_error tTJSNI_AsyncTrigger::Construct(tjs_int numparams, tTJSVariant **param,
                                         iTJSDispatch2 *tjs_obj) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if(TJS_FAILED(hr))
        return hr;

    if(numparams >= 2 && param[1]->Type() != tvtVoid)
        ActionName = *param[1]; // action function to be called

    ActionOwner = param[0]->AsObjectClosure();
    Owner = tjs_obj;

    return TJS_S_OK;
}

//---------------------------------------------------------------------------
void tTJSNI_AsyncTrigger::Invalidate() {
    CancelSourceEvents(Owner);
    Owner = nullptr;

    ActionOwner.Release();
    ActionOwner.ObjThis = ActionOwner.Object = nullptr;

    inherited::Invalidate();
}

//---------------------------------------------------------------------------
void tTJSNI_AsyncTrigger::Trigger() {
    // trigger event
    if(Owner) {
        if(Cached) {
            // remove undelivered events from queue when "Cached" flag
            // is set
            CancelSourceEvents(Owner);
        }
        static ttstr eventname(TJS_W("onFire"));

        tjs_uint32 flags = TVP_EPT_POST;
        if(Mode == atmExclusive)
            flags |= TVP_EPT_EXCLUSIVE; // fire exclusive event
        if(Mode == atmAtIdle)
            flags |= TVP_EPT_IDLE; // fire idle event

        PostTJSEvent(Owner, Owner, eventname, 0, flags, 0, nullptr);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_AsyncTrigger::Cancel() {
    // cancel event
    if(Owner)
        CancelSourceEvents(Owner);
    IdlePendingCount = 0;
}

//---------------------------------------------------------------------------
void tTJSNI_AsyncTrigger::SetCached(bool b) {
    // set cached operation flag.
    // when this flag is set, only one event is delivered at once.
    if(Cached != b) {
        Cached = b;
        Cancel(); // all events are canceled
    }
}

//---------------------------------------------------------------------------
void tTJSNI_AsyncTrigger::SetMode(krkr::utils::AsyncTriggerMode m) {
    if(Mode != m) {
        Mode = m;
        Cancel();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_AsyncTrigger
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_AsyncTrigger::ClassID = -1;

tTJSNC_AsyncTrigger::tTJSNC_AsyncTrigger() :
    inherited(TJS_W("AsyncTrigger")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(AsyncTrigger) // constructor
        TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
                /*var.name*/ _this, /*var.type*/ tTJSNI_AsyncTrigger,
                /*TJS class name*/ AsyncTrigger){ return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ AsyncTrigger)
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ trigger) {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);
    _this->Trigger();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ trigger)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ cancel) {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);
    _this->Cancel();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ cancel)
//----------------------------------------------------------------------

//-- events

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onFire) {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if(obj.Object) {
        ttstr &actionname = _this->GetActionName();
        TVP_ACTION_INVOKE_BEGIN(0, "onFire", objthis);
        TVP_ACTION_INVOKE_END_NAME(
            obj, actionname.IsEmpty() ? nullptr : actionname.c_str(),
            actionname.IsEmpty() ? nullptr : actionname.GetHint());
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onFire)
//----------------------------------------------------------------------

//--properties

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(cached){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);
*result = _this->GetCached();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);
    _this->SetCached(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(cached)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mode){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);
*result = (tjs_int)_this->GetMode();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AsyncTrigger);
    _this->SetMode((krkr::utils::AsyncTriggerMode)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mode)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}

//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_AsyncTrigger::CreateNativeInstance() {
    return new tTJSNI_AsyncTrigger();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTJSNativeClass *CreateNativeClass_AsyncTrigger() {
    return new tTJSNC_AsyncTrigger();
}
//---------------------------------------------------------------------------

} // namespace krkr::base
