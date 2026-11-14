//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000-2007 W.Dee <dee@kikyou.info> and
   contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// MIDI sequencer interface
//---------------------------------------------------------------------------
#ifndef MIDIIntfH
#define MIDIIntfH

#include "tjsNative.h"
#include "SoundBufferBaseIntf.h"

namespace krkr::sound {

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_BaseMIDISoundBuffer
//---------------------------------------------------------------------------
class tTJSNI_BaseMIDISoundBuffer : public tTJSNI_SoundBuffer {
    typedef tTJSNI_SoundBuffer inherited;

public:
    tTJSNI_BaseMIDISoundBuffer();

    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *tjs_obj) override;

    void Invalidate() override;

protected:
public:
};
//---------------------------------------------------------------------------

} // namespace krkr::sound

#include "MIDIImpl.h" // must define tTJSNI_MIDISoundBuffer class

namespace krkr::sound {

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_MIDISoundBuffer : TJS MIDISoundBuffer class
//---------------------------------------------------------------------------
class tTJSNC_MIDISoundBuffer : public tTJSNativeClass {
public:
    tTJSNC_MIDISoundBuffer();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *TVPCreateNativeClass_MIDISoundBuffer();
//---------------------------------------------------------------------------

} // namespace krkr::sound

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::sound::tTJSNI_BaseMIDISoundBuffer;
using krkr::sound::tTJSNI_MIDISoundBuffer;
using krkr::sound::tTJSNC_MIDISoundBuffer;
using krkr::sound::TVPCreateNativeClass_MIDISoundBuffer;

//---------------------------------------------------------------------------
#endif
