//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Main Window (Controller)
//---------------------------------------------------------------------------
#ifndef SystemControlH
#define SystemControlH
//---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include "krkr::utils::Timer.h"
//---------------------------------------------------------------------------
namespace krkr::environ {

class SystemControl {
private: // ÉÜÅ[ÉUÅ[êÈåæ
    bool ContinuousEventCalling;
    bool AutoShowConsoleOnError;

    bool EventEnable;

    uint32_t LastCompactedTick;
    uint32_t LastDeepCompactedTick;
    uint32_t LastAggressiveCompactedTick;
    uint32_t LastIdleCompactTick;
    uint32_t LastMemoryGovernorTick;
    uint32_t LastMemoryLogTick;
    int32_t LastCommandLineGeneration;
    int32_t MemoryProfile;
    int32_t MemoryBudgetMB;
    int32_t MemoryLogIntervalMS;
    uint32_t LastCloseClickedTick;
    uint32_t LastShowModalWindowSentTick;
    uint32_t LastRehashedTick;

    uint32_t MixedIdleTick{};
    uint32_t LastHeapCeilingTick{};

    krkr::utils::Timer SystemWatchTimer;

public:
    SystemControl();

    void InvokeEvents();
    void CallDeliverAllEventsOnIdle();

    void BeginContinuousEvent();
    void EndContinuousEvent();

    void NotifyCloseClicked();
    void NotifyEventDelivered();

    void SetEventEnabled(bool b) { EventEnable = b; }
    [[nodiscard]] bool GetEventEnabled() const { return EventEnable; }

    bool ApplicationIdle();

private:
    void DeliverEvents();
    void ReloadMemoryGovernorConfig(bool force);
    void RunMemoryGovernor(uint32_t tick);

public:
    void SystemWatchTimerTimer();
};
extern SystemControl *GetSystemControl;
extern bool systemControlAlive;

void RegisterPSBCacheInfoCallback(bool (*cb)(size_t &usedBytes,
                                                size_t &limitBytes));

} // namespace krkr::environ

// TODO: 过渡兼容，调用点迁移完成后删除

#endif
