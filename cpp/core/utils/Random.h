//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Simple Pseudo Random Generator
//---------------------------------------------------------------------------
#ifndef RandomH
#define RandomH
//---------------------------------------------------------------------------

namespace krkr::utils
{
        TJS_EXP_FUNC_DEF(void, TVPPushEnvironNoise, (const void *buf, tjs_int bufsize));
        TJS_EXP_FUNC_DEF(void, TVPGetRandomBits128, (void *dest));
} // namespace krkr::utils

//TODO:过渡兼容完成后删除
using krkr::utils::TVPPushEnvironNoise;
using krkr::utils::TVPGetRandomBits128;
//---------------------------------------------------------------------------
#endif
