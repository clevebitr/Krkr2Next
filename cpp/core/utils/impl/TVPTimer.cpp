
#include "tjsCommHead.h"
#include "Timer.h"
#include "TickCount.h"

namespace krkr::utils {

struct TimerImpl {
    TimerImpl *Prev = nullptr, *Next = nullptr;
    Timer *pTimer = nullptr;
    uint32_t Index;

    ~TimerImpl() {
        if(Prev)
            Prev->Next = Next;
        if(Next)
            Next->Prev = Prev;
    }

    void SetTimer(Timer *p) { pTimer = p; }

    void Kill() {
        if(Prev)
            Prev->Next = Next;
        if(Next)
            Next->Prev = Prev;
        Next = Prev = nullptr;
    }

    void Set();

    void Set(int idx);

    void Add(TimerImpl *p) {
        p->Next = Next;
        Next = p;
        p->Prev = this;
    }

    void FireNext();
};

static TimerImpl _timer_v1[256], _timer_v2[64], _timer_v3[64],
    _timer_v4[64], _timer_v5[64];

static struct t_timer_idx {
    union {
        struct {
            unsigned int _idx_v1 : 8;
            unsigned int _idx_v2 : 6;
            unsigned int _idx_v3 : 6;
            unsigned int _idx_v4 : 6;
            unsigned int _idx_v5 : 6;
        };
        uint32_t _idx;
    };

    t_timer_idx(uint32_t idx) : _idx(idx) {}
} _timer_idx(GetRoughTickCount32());

void TimerImpl::Set() {
    int interval = pTimer->GetInterval();
    Set(interval);
}

void TimerImpl::Set(int idx) {
    idx += _timer_idx._idx_v1;
    Index = idx;
    if(idx < 256) {
        _timer_v1[idx].Add(this);
        return;
    }
    idx >>= 8;
    idx += _timer_idx._idx_v2;
    if(idx < 64) {
        _timer_v2[idx].Add(this);
        return;
    }
    idx >>= 6;
    idx += _timer_idx._idx_v3;
    if(idx < 64) {
        _timer_v3[idx].Add(this);
        return;
    }
    idx >>= 6;
    idx += _timer_idx._idx_v4;
    if(idx < 64) {
        _timer_v4[idx].Add(this);
        return;
    }
    idx >>= 6;
    idx += _timer_idx._idx_v5;
    _timer_v5[idx].Add(this);
}

static TimerImpl _processedTimer;

void TimerImpl::FireNext() {
    TimerImpl *p = Next;
    if(!p)
        return;
    Next = nullptr;

    p->pTimer->FireEvent();
    p->FireNext();
    _processedTimer.Add(p);
    // 	int interval = p->pTimer->GetInterval();
    // 	p->Set(interval);
}

void Timer::ProgressAllTimer() {
    uint32_t curTick = GetRoughTickCount32();
    int past = curTick - _timer_idx._idx;
    for(int i = 0; i < past; ++i) {
        _timer_v1[_timer_idx._idx_v1].FireNext();
        ++_timer_idx._idx;
        if(_timer_idx._idx_v1 == 0) { // v2 step
            TimerImpl *root = &_timer_v2[_timer_idx._idx_v2];
            TimerImpl *p = root->Next;
            root->Next = nullptr;
            while(p) {
                TimerImpl *next = p->Next;
                p->Next = nullptr;
                p->Index &= 255;
                _timer_v1[p->Index].Add(p);
                p = next;
            }
        }
        if(_timer_idx._idx_v2 == 0) { // v3 step
            TimerImpl *root = &_timer_v3[_timer_idx._idx_v3];
            TimerImpl *p = root->Next;
            root->Next = nullptr;
            while(p) {
                TimerImpl *next = p->Next;
                p->Next = nullptr;
                int idx = p->Index >> 8;
                p->Index &= 255 | (63 << 8);
                _timer_v2[idx].Add(p);
                p = next;
            }
        }
        if(_timer_idx._idx_v3 == 0) { // v4 step
            TimerImpl *root = &_timer_v4[_timer_idx._idx_v4];
            TimerImpl *p = root->Next;
            root->Next = nullptr;
            while(p) {
                TimerImpl *next = p->Next;
                p->Next = nullptr;
                int idx = p->Index >> (8 + 6);
                p->Index &= 255 | (63 << 8) | (63 << (8 + 6));
                _timer_v3[idx].Add(p);
                p = next;
            }
        }
        if(_timer_idx._idx_v4 == 0) { // v5 step
            TimerImpl *root = &_timer_v5[_timer_idx._idx_v5];
            TimerImpl *p = root->Next;
            root->Next = nullptr;
            while(p) {
                TimerImpl *next = p->Next;
                p->Next = nullptr;
                int idx = p->Index >> (8 + 6 + 6);
                p->Index &=
                    255 | (63 << 8) | (63 << (8 + 6)) | (63 << (8 + 6 + 6));
                _timer_v4[idx].Add(p);
                p = next;
            }
        }
    }
    TimerImpl *p = _processedTimer.Next;
    _processedTimer.Next = nullptr;
    while(p) {
        TimerImpl *next = p->Next;
        p->Next = nullptr;
        p->Set();
        p = next;
    }
}

Timer::Timer() : event_(nullptr), interval_(1000), enabled_(true) {
    impl_ = new TimerImpl;
    impl_->pTimer = this;
}

Timer::~Timer() {
    delete impl_;

    delete event_;
}

void Timer::UpdateTimer() {
    impl_->Kill();
    if(interval_ > 0 && enabled_ && event_ != nullptr) {
        impl_->Set();
    }
}

} // namespace krkr::utils
