
#ifndef __FREE_TYPE_FONT_RASTERIZER_H__
#define __FREE_TYPE_FONT_RASTERIZER_H__

#include "tjsCommHead.h"
#include "CharacterData.h"
#include "FontRasterizer.h"
#include <string>
#include <vector>

/**
 * 字体回退策略。两套解析实现并存，由设置页的「字体回退策略」在运行时选择：
 *
 *  - `legacy`：原版派系（KrKr2-Next / PocketKrKr）
 *    只认**一个**回退字面（默认字体）；当主字面本身就是默认字体时**完全不回退**。
 *    缺字时落到 FreeType 的缺字占位符（黑方块，大小随手心变化）。
 *  - `chain` ：AetherKiri 派系
 *    把**所有已注册字面**都作为回退候选，逐个按字查，命中即用；并用
 *    FontBaseline.h 把回退字形对齐回主字面的基线。
 *  - `auto`  ：按主字面能力自选
 *    多字面字体（TTC/OTC）用 `chain`，其余用 `legacy` 以维持原版行为。
 *
 * 两套都保留而不是直接替换：`legacy` 是跑通过的行为基线，换成 `chain` 会改变
 * 字形来源（同一码位可能改从别的字面取形），拿到真机回归之前不宜单方面替换。
 */
enum class FontFallbackMode {
    Auto,
    Legacy,
    Chain,
};

/**
 * 由 engine_api 的 `font_fallback_mode` 选项下发（值 `auto`/`legacy`/`chain`，
 * 经 TVPSetCommandLine 落到 IndividualConfigManager，再由这里读进内存）。
 * 无法识别的字符串按 `auto` 处理，不抛异常——设置项不该让引擎起不来。
 */
void TVPSetFontFallbackModeFromString(const char *mode);
/** 取当前生效的策略；从未设置过时为 `Auto`。 */
FontFallbackMode TVPGetFontFallbackMode();

class FreeTypeFontRasterizer : public FontRasterizer {
    tjs_int RefCount;
    class tFreeTypeFace *Face; //!< Faceオブジェクト
    /** legacy 模式用的单一回退字面；由本对象拥有。 */
    class tFreeTypeFace *FaceFallback = nullptr;
    /** chain 模式用的回退字面表；由本对象拥有（与 OwnedChainFaces 同一批）。 */
    std::vector<class tFreeTypeFace *> FaceFallbacks;
    /** chain 模式建过的字面，析构时逐个删。 */
    std::vector<class tFreeTypeFace *> OwnedChainFaces;
    class tTVPNativeBaseBitmap *LastBitmap;
    tTVPFont CurrentFont;
    /**
     * 触字宽度缓存的键前缀。由「实际解析出的字面名 + 字号 +
     * 标志」拼成；主字面一变 就必须变，否则会拿旧字面的度量去排版。见
     * GetTextExtent()。
     */
    std::string CurrentExtentCacheFontKey;

    void ApplyFallbackFace();
    /** 当前应使用的策略：`Auto` 需先看主字面的多字面能力再决定。 */
    FontFallbackMode ResolveMode();
    void ApplyFallbackFaces();
    void ClearFallbackFaces();
    /** 逐个回退字面查字；命中返回该字面，否则 nullptr。 */
    class tFreeTypeFace *FindChainGlyph(tjs_char ch, tGlyphMetrics &metrics);
    /** 主字面缺字时的回退字形查询；已按主字面基线对齐。 */
    tTVPCharacterData *GetFallbackGlyph(tjs_char ch);

public:
    FreeTypeFontRasterizer();
    ~FreeTypeFontRasterizer() override;
    void AddRef() override;
    void Release() override;
    void ApplyFont(class tTVPNativeBaseBitmap *bmp, bool force) override;
    void ApplyFont(const struct tTVPFont &font) override;
    void GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) override;
    tjs_int GetAscentHeight() override;
    tTVPCharacterData *GetBitmap(const tTVPFontAndCharacterData &font,
                                 tjs_int aofsx, tjs_int aofsy) override;
    void GetGlyphDrawRect(const ttstr &text, struct tTVPRect &area) override;
};

#endif // __FREE_TYPE_FONT_RASTERIZER_H__
