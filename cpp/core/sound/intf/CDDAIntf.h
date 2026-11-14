//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000-2007 W.Dee <dee@kikyou.info> and
   contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// CD-DA access interface
//---------------------------------------------------------------------------
#ifndef CDDAIntfH
#define CDDAIntfH

#include "tjsNative.h"
#include "SoundBufferBaseIntf.h"

namespace krkr::sound {

class tTJSNI_BaseCDDASoundBuffer : public tTJSNI_SoundBuffer {
    typedef tTJSNI_SoundBuffer inherited;

public:
    tTJSNI_BaseCDDASoundBuffer();

    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *tjs_obj) override;

    void Invalidate() override;

protected:
public:
};

} // namespace krkr::sound

#include "CDDAImpl.h" // must define tTJSNI_CDDASoundBuffer class

namespace krkr::sound {

class tTJSNC_CDDASoundBuffer : public tTJSNativeClass {
public:
    tTJSNC_CDDASoundBuffer();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

extern tTJSNativeClass *TVPCreateNativeClass_CDDASoundBuffer();

} // namespace krkr::sound

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::sound::tTJSNI_BaseCDDASoundBuffer;
using krkr::sound::tTJSNI_CDDASoundBuffer;
using krkr::sound::tTJSNC_CDDASoundBuffer;
using krkr::sound::TVPCreateNativeClass_CDDASoundBuffer;

#endif
