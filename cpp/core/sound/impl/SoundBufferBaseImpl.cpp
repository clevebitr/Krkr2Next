//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Sound Buffer Base implementation
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include "SoundBufferBaseImpl.h"
#include "WaveImpl.h"

#include "krkr::utils::Timer.h"

namespace krkr::sound {

//---------------------------------------------------------------------------
// Sound Buffer Timer Dispatcher ( for fading or commiting defered
// settings )
//---------------------------------------------------------------------------
static class krkr::utils::Timer *soundBufferTimer = nullptr;

static std::vector<tTJSNI_SoundBuffer *> soundBufferVector;

//---------------------------------------------------------------------------
class SoundBufferTimerDispatcher {
public:
    void Handler() {
        std::vector<tTJSNI_SoundBuffer *>::iterator i;
        for(i = soundBufferVector.begin(); i != soundBufferVector.end();
            i++) {
            (*i)->TimerBeatHandler();
        }
        CommitWaveSoundBufferSettings();
    }
} static soundBufferTimerDispatcher;

//---------------------------------------------------------------------------
void AddSoundBuffer(tTJSNI_SoundBuffer *buf) {
    if(soundBufferVector.empty()) {
        // first buffer
        soundBufferTimer = new krkr::utils::Timer(); // Create Timer Object
        soundBufferTimer->SetInterval(KRKR_SB_BEAT_INTERVAL);
        soundBufferTimer->SetOnTimerHandler(
            &soundBufferTimerDispatcher,
            &SoundBufferTimerDispatcher::Handler);
    }

    soundBufferVector.push_back(buf);
}

//---------------------------------------------------------------------------
void RemoveSoundBuffer(tTJSNI_SoundBuffer *buf) {
    if(!soundBufferVector.empty()) {
        std::vector<tTJSNI_SoundBuffer *>::iterator i;
        i = std::find(soundBufferVector.begin(), soundBufferVector.end(),
                      buf);
        if(i != soundBufferVector.end()) {
            soundBufferVector.erase(i);
        }
    }

    if(soundBufferVector.empty()) {
        // all buffer was removed
        delete soundBufferTimer;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_SoundBuffer
//---------------------------------------------------------------------------
tTJSNI_SoundBuffer::tTJSNI_SoundBuffer() = default;

//---------------------------------------------------------------------------
tjs_error tTJSNI_SoundBuffer::Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj) {
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if(TJS_FAILED(hr))
        return hr;

    AddSoundBuffer(this);

    return TJS_S_OK;
}

//---------------------------------------------------------------------------
void tTJSNI_SoundBuffer::Invalidate() {
    RemoveSoundBuffer(this);

    inherited::Invalidate();
}
//---------------------------------------------------------------------------

} // namespace krkr::sound
