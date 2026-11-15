
#ifndef __TVP_TIMER_H__
#define __TVP_TIMER_H__

namespace krkr::utils {

class ITimerEvent {
public:
    virtual ~ITimerEvent() {}

    virtual void Handle() = 0;
};

template <typename T>
class TimerEvent : public ITimerEvent {
    void (T::*handler_)();

    T *owner_;

public:
    TimerEvent(T *owner, void (T::*Handler)()) :
        owner_(owner), handler_(Handler) {}

    void Handle() override { (owner_->*handler_)(); }
};

class TimerImpl;

class Timer {
    ITimerEvent *event_;
    int interval_;
    bool enabled_;

    void UpdateTimer();

    void FireEvent() {
        if(event_) {
            event_->Handle();
        }
    }

    friend class TimerImpl;

    TimerImpl *impl_;

public:
    Timer();

    ~Timer();

    template <typename T>
    void SetOnTimerHandler(T *owner, void (T::*Handler)()) {

        delete event_;
        event_ = new TimerEvent<T>(owner, Handler);
        UpdateTimer();
    }

    void SetInterval(int i) {
        if(interval_ != i) {
            interval_ = i;
            UpdateTimer();
        }
    }

    [[nodiscard]] int GetInterval() const { return interval_; }

    void SetEnabled(bool b) {
        if(enabled_ != b) {
            enabled_ = b;
            UpdateTimer();
        }
    }

    [[nodiscard]] bool GetEnable() const { return enabled_; }

    static void ProgressAllTimer();
};

} // namespace krkr::utils

#endif // __TVP_TIMER_H__
