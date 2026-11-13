//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class interface
//---------------------------------------------------------------------------
#ifndef PluginIntfH
#define PluginIntfH

#include "tjsNative.h"

namespace krkr::plugin {

//---------------------------------------------------------------------------
// tTJSNC_Plugins : TJS Plugins class
//---------------------------------------------------------------------------
class tTJSNC_Plugins : public tTJSNativeClass {
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_Plugins();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance *CreateNativeInstance() override;
};

//---------------------------------------------------------------------------
extern tTJSNativeClass *TVPCreateNativeClass_Plugins();
//---------------------------------------------------------------------------

} // namespace krkr::plugin

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::plugin::tTJSNC_Plugins;
using krkr::plugin::TVPCreateNativeClass_Plugins;

#endif
