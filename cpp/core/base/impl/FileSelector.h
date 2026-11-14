//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// File Selector dialog box
//---------------------------------------------------------------------------
#ifndef FileSelectorH
#define FileSelectorH
//---------------------------------------------------------------------------

#include "tjs.h"

namespace krkr::base {

extern bool TVPSelectFile(iTJSDispatch2 *params);

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::TVPSelectFile;

#endif
