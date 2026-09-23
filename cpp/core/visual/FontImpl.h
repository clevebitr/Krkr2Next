#pragma once
#include "tjs.h"
#include "tjsHashSearch.h"
#include <functional>
#include <freetype/freetype.h>

const FT_Library TVPGetFontLibrary();

void TVPInitFontNames();
int TVPEnumFontsProc(const ttstr &FontPath);
const ttstr &TVPGetDefaultFontName();

/**
 * FONTCHANGER 兼容用的“强制字面”：非空时 `FontSystem::GetBeingFont()` 一律返回它。
 *
 * 为什么需要：汉化补丁普遍带一套 FontChanger（`FONTCHANGER.dll` + `hook.ini`）——
 * 游戏原字体没有中文字形，补丁靠它把全部文字换成自带的字体（如 ShiraYukiNoa.otf）。
 * 该插件是原生 Win32 DLL（hook CreateFontA/CreateFontIndirectA），本引擎加载不了，
 * 于是字体没被换 → 游戏里中文全是方块（真机 2026-09-23 11:46 的 チート緊縛術）。
 * 这里不 hook Win32，只复现“效果”：强制字面 + 注册字体文件（见
 * `cpp/plugins/fontchanger_compat.cpp`）。
 */
void TVPSetForcedFontName(const ttstr &name);
const ttstr &TVPGetForcedFontName();
tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname);
void TVPResetFontImplForRestart();
struct TVPFontNamePathInfo {
    ttstr Path;
    std::function<tTJSBinaryStream *(TVPFontNamePathInfo *)> Getter;
    int Index{};
};
TVPFontNamePathInfo *TVPFindFont(const ttstr &name);

//---------------------------------------------------------------------------
// font enumeration and existence check
//---------------------------------------------------------------------------
class tTVPttstrHash {
public:
    static tjs_uint32 Make(const ttstr &val);
};
extern tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash> TVPFontNames;