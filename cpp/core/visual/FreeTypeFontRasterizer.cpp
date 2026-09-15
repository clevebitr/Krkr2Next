
#include "FreeTypeFontRasterizer.h"
#include "LayerBitmapIntf.h"
#include "FreeType.h"
#include "FontBaseline.h"
#if _WIN32
#include <corecrt_math_defines.h>
#else
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#endif
#include "MsgIntf.h"
#include "FontSystem.h"
#include "FontImpl.h"
#include "ConfigManager/IndividualConfigManager.h"
#include <complex>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <spdlog/spdlog.h>

extern void TVPUninitializeFreeFont();
extern FontSystem *TVPFontSystem;
extern const ttstr &TVPGetDefaultFontName();
extern void TVPGetAllFontList(std::vector<ttstr> &list);

// 定义在文件下方；GetTextExtent 在它之前用到。
static bool isUnicodeSpace(char16_t ch);

// ---------------------------------------------------------------------------
// 回退策略：进程级，由设置下发（engine_api 的 font_fallback_mode 选项）
// ---------------------------------------------------------------------------
namespace {
    std::mutex g_font_fallback_mode_mutex;
    FontFallbackMode g_font_fallback_mode = FontFallbackMode::Auto;
    bool g_font_fallback_mode_init = false;

    /**
     * 首次使用时把设置读进来。`IndividualConfigManager` 由 TVPSetCommandLine
     * 写值，
     * 而选项是在引擎起来之前下发的、字体初始化在之后，所以这里读到的一定是最新值。
     * 读一次就冻结：ApplyFont 在渲染线程上跑，不该每帧去碰配置管理器。
     */
    void EnsureFontFallbackModeInit() {
        std::lock_guard<std::mutex> lock(g_font_fallback_mode_mutex);
        if(g_font_fallback_mode_init)
            return;
        g_font_fallback_mode_init = true;
        std::string mode =
            IndividualConfigManager::GetInstance()->GetValue<std::string>(
                "font_fallback_mode", "auto");
        if(mode == "legacy")
            g_font_fallback_mode = FontFallbackMode::Legacy;
        else if(mode == "chain")
            g_font_fallback_mode = FontFallbackMode::Chain;
        else
            g_font_fallback_mode = FontFallbackMode::Auto;
        spdlog::info("font fallback mode = {} (raw='{}')",
                     g_font_fallback_mode == FontFallbackMode::Legacy ? "legacy"
                         : g_font_fallback_mode == FontFallbackMode::Chain
                         ? "chain"
                         : "auto",
                     mode);
    }

    // -----------------------------------------------------------------------
    // 触字宽度（GetTextExtent）缓存
    //
    // 为什么必须有：GetTextExtent 每字每帧都走。chain
    // 模式下主字面缺一个字就要把 已注册字面逐个问一遍（设备上仅 CJK 字体就有 5
    // 个字面），开销会被放大到可见。 原版实现没有这个缓存——它压根不做回退查询。
    //
    // 键必须含「实际解析出的字面名 + 字号 + 标志」，少一项就会拿错值去排版
    // （表现为字宽错、文字挤在一起）。跨对象共享所以要互斥；
    // 上限到了整体清空：字形集是稳定的热点集，简单清空比 LRU 划算且不会无限涨。
    // -----------------------------------------------------------------------
    struct GlyphExtentCacheKey {
        std::string font;
        tjs_char ch = 0;

        bool operator==(const GlyphExtentCacheKey &other) const {
            return ch == other.ch && font == other.font;
        }
    };

    struct GlyphExtentCacheKeyHash {
        std::size_t operator()(const GlyphExtentCacheKey &key) const {
            const auto font_hash = std::hash<std::string>{}(key.font);
            const auto char_hash =
                std::hash<tjs_uint32>{}(static_cast<tjs_uint32>(key.ch));
            return font_hash ^
                (char_hash + 0x9e3779b9u + (font_hash << 6) + (font_hash >> 2));
        }
    };

    struct GlyphExtentCacheValue {
        tjs_int w = 0;
        tjs_int h = 0;
    };

    std::mutex g_glyph_extent_cache_mutex;
    std::unordered_map<GlyphExtentCacheKey, GlyphExtentCacheValue,
                       GlyphExtentCacheKeyHash>
        g_glyph_extent_cache;
    constexpr std::size_t kGlyphExtentCacheLimit = 32768;
} // namespace

void TVPSetFontFallbackModeFromString(const char *mode) {
    std::lock_guard<std::mutex> lock(g_font_fallback_mode_mutex);
    g_font_fallback_mode_init = true;
    if(mode && std::string(mode) == "legacy")
        g_font_fallback_mode = FontFallbackMode::Legacy;
    else if(mode && std::string(mode) == "chain")
        g_font_fallback_mode = FontFallbackMode::Chain;
    else
        g_font_fallback_mode = FontFallbackMode::Auto;
}

FontFallbackMode TVPGetFontFallbackMode() {
    EnsureFontFallbackModeInit();
    std::lock_guard<std::mutex> lock(g_font_fallback_mode_mutex);
    return g_font_fallback_mode;
}

// ---------------------------------------------------------------------------
// legacy：原版派系的单一回退字面
// ---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFallbackFace() {
    if(!FaceFallback && Face &&
       Face->GetFontName() != TVPGetDefaultFontName()) {
        FaceFallback = new tFreeTypeFace(TVPGetDefaultFontName(), 0);
    }
    if(!FaceFallback)
        return;
    FaceFallback->SetHeight(CurrentFont.Height < 0 ? -CurrentFont.Height
                                                   : CurrentFont.Height);
    if(CurrentFont.Flags & TVP_TF_ITALIC) {
        FaceFallback->SetOption(TVP_TF_ITALIC);
    } else {
        FaceFallback->ClearOption(TVP_TF_ITALIC);
    }
    if(CurrentFont.Flags & TVP_TF_BOLD) {
        FaceFallback->SetOption(TVP_TF_BOLD);
    } else {
        FaceFallback->ClearOption(TVP_TF_BOLD);
    }
    if(CurrentFont.Flags & TVP_TF_UNDERLINE) {
        FaceFallback->SetOption(TVP_TF_UNDERLINE);
    } else {
        FaceFallback->ClearOption(TVP_TF_UNDERLINE);
    }
    if(CurrentFont.Flags & TVP_TF_STRIKEOUT) {
        FaceFallback->SetOption(TVP_TF_STRIKEOUT);
    } else {
        FaceFallback->ClearOption(TVP_TF_STRIKEOUT);
    }
}

// ---------------------------------------------------------------------------
// chain：把所有已注册字面都当回退候选（AetherKiri 派系）
// ---------------------------------------------------------------------------
FontFallbackMode FreeTypeFontRasterizer::ResolveMode() {
    const FontFallbackMode mode = TVPGetFontFallbackMode();
    if(mode != FontFallbackMode::Auto)
        return mode;

    // Auto：主字面是「多字面字体」时用
    // chain。判据不需要猜文件格式——默认字体在同名
    // 下能解析出与主字面**不同**的字面名，就说明这个字面名背后有多于一个字面
    // （TTC/OTC 集合），此时单一回退字面覆盖不住。
    if(Face) {
        const ttstr &primary = Face->GetFontName();
        if(!primary.IsEmpty()) {
            std::vector<ttstr> names;
            tFreeTypeFace probe(primary, 0);
            probe.GetFaceNameList(names);
            for(const auto &n : names) {
                if(n != primary) {
                    spdlog::info("font fallback auto -> chain（字面 '{}' "
                                 "是多字面集合，含 {} 个字面）",
                                 primary.AsNarrowStdString(), names.size());
                    return FontFallbackMode::Chain;
                }
            }
        }
    }
    return FontFallbackMode::Legacy;
}

void FreeTypeFontRasterizer::ClearFallbackFaces() {
    for(auto *f : OwnedChainFaces)
        delete f;
    OwnedChainFaces.clear();
    FaceFallbacks.clear();
}

void FreeTypeFontRasterizer::ApplyFallbackFaces() {
    if(!Face || !FaceFallbacks.empty())
        return;

    std::vector<ttstr> candidates;
    const ttstr &current = Face->GetFontName();
    const ttstr &default_font = TVPGetDefaultFontName();
    auto append_unique = [&](const ttstr &name) {
        if(name.IsEmpty() || name == current)
            return;
        for(const auto &existing : candidates) {
            if(existing == name)
                return;
        }
        candidates.emplace_back(name);
    };

    append_unique(default_font);
    std::vector<ttstr> all_fonts;
    TVPGetAllFontList(all_fonts);
    for(const auto &name : all_fonts)
        append_unique(name);

    for(const auto &name : candidates) {
        auto *fallback = new tFreeTypeFace(name, 0);
        if(!fallback)
            continue;
        fallback->SetHeight(CurrentFont.Height < 0 ? -CurrentFont.Height
                                                   : CurrentFont.Height);
        if(CurrentFont.Flags & TVP_TF_ITALIC)
            fallback->SetOption(TVP_TF_ITALIC);
        if(CurrentFont.Flags & TVP_TF_BOLD)
            fallback->SetOption(TVP_TF_BOLD);
        if(CurrentFont.Flags & TVP_TF_UNDERLINE)
            fallback->SetOption(TVP_TF_UNDERLINE);
        if(CurrentFont.Flags & TVP_TF_STRIKEOUT)
            fallback->SetOption(TVP_TF_STRIKEOUT);
        OwnedChainFaces.emplace_back(fallback);
        FaceFallbacks.emplace_back(fallback);
    }
    spdlog::info("font fallback chain 建了 {} 个回退字面（主字面 '{}'）",
                 FaceFallbacks.size(), current.AsNarrowStdString());
}

tFreeTypeFace *FreeTypeFontRasterizer::FindChainGlyph(tjs_char ch,
                                                      tGlyphMetrics &metrics) {
    ApplyFallbackFaces();
    for(auto *fallback : FaceFallbacks) {
        if(fallback && fallback->GetGlyphSizeFromCharcode(ch, metrics))
            return fallback;
    }
    return nullptr;
}

tTVPCharacterData *FreeTypeFontRasterizer::GetFallbackGlyph(tjs_char ch) {
    ApplyFallbackFaces();
    for(auto *fallback : FaceFallbacks) {
        if(!fallback)
            continue;
        tTVPCharacterData *data = fallback->GetGlyphFromCharcode(ch);
        if(!data)
            continue;
        // 各回退字面的 ascender 与主字面不同，直接用会让相邻字符上下跳。
        // 把回退字形挪回主字面基线（AetherKiri 的同一处理）。
        const int adjust = krkr::font::ComputeFallbackBaselineAdjustment(
            Face ? Face->GetAscent() : 0, fallback->GetAscent());
        if(adjust != 0)
            data->OriginY += adjust;
        return data;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
FreeTypeFontRasterizer::FreeTypeFontRasterizer() :
    RefCount(0), Face(nullptr), LastBitmap(nullptr) {
    AddRef();
}

FreeTypeFontRasterizer::~FreeTypeFontRasterizer() {
    delete Face;
    Face = nullptr;
    if(FaceFallback) {
        delete FaceFallback;
        FaceFallback = nullptr;
    }
    ClearFallbackFaces();
    TVPUninitializeFreeFont();
}

void FreeTypeFontRasterizer::AddRef() { RefCount++; }
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::Release() {
    RefCount--;
    LastBitmap = nullptr;
    if(RefCount == 0) {
        delete Face;
        Face = nullptr;
        if(FaceFallback) {
            delete FaceFallback;
            FaceFallback = nullptr;
        }
        ClearFallbackFaces();
        delete this;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFont(class tTVPNativeBaseBitmap *bmp,
                                       bool force) {
    if(bmp != LastBitmap || force) {
        ApplyFont(bmp->GetFont());
        LastBitmap = bmp;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFont(const tTVPFont &font) {
    CurrentFont = font;
    ttstr stdname = TVPFontSystem->GetBeingFont(font.Face);

    // TVP_FACE_OPTIONS_NO_ANTIALIASING
    // TVP_FACE_OPTIONS_NO_HINTING
    // TVP_FACE_OPTIONS_FORCE_AUTO_HINTING
    tjs_uint32 opt = 0;
    opt |= (font.Flags & TVP_TF_ITALIC) ? TVP_TF_ITALIC : 0;
    opt |= (font.Flags & TVP_TF_BOLD) ? TVP_TF_BOLD : 0;
    opt |= (font.Flags & TVP_TF_UNDERLINE) ? TVP_TF_UNDERLINE : 0;
    opt |= (font.Flags & TVP_TF_STRIKEOUT) ? TVP_TF_STRIKEOUT : 0;
    opt |= (font.Flags & TVP_TF_FONTFILE) ? TVP_FACE_OPTIONS_FILE : 0;
    bool recreate = false;
    if(Face) {
        if(Face->GetFontName() != stdname) {
            delete Face;
            Face = nullptr;
            Face = new tFreeTypeFace(stdname, opt);
            recreate = true;
            // 换了主字面，之前那批回退字面是按旧字面尺寸/标志建的，必须重建
            if(FaceFallback) {
                delete FaceFallback;
                FaceFallback = nullptr;
            }
            ClearFallbackFaces();
        }
    } else {
        Face = new tFreeTypeFace(stdname, opt);
        recreate = true;
    }
    Face->SetHeight(font.Height < 0 ? -font.Height : font.Height);

#if defined(KRKR_RENDER_PROBE)
    // [Font probe] 打印请求的字面名/字号与 FreeType 实际使用的值。
    // 文字偏小或垂直错位时看这里：heightReq 与 heightPix 不符 =
    // 字形缩放被改过； ascent 与 rawAsc/rawDesc/unitsEM 推不出来 = 基线被放歪。
    if(auto logger = spdlog::get("core")) {
        logger->info(
            "[FontProbe] apply face='{}' being='{}' heightReq={} heightPix={} "
            "ascent={} rawAsc={} rawDesc={} unitsEM={} angle={} fallback={}",
            font.Face.AsNarrowStdString(), stdname.AsNarrowStdString(),
            font.Height, Face->GetPixelHeight(), Face->GetAscent(),
            Face->GetAscender(), Face->GetDescender(), Face->GetUnitsPerEM(),
            font.Angle,
            TVPGetFontFallbackMode() == FontFallbackMode::Chain ? "chain"
                                                                : "legacy");
    }
#endif
    if(recreate == false) {
        if(font.Flags & TVP_TF_ITALIC) {
            Face->SetOption(TVP_TF_ITALIC);
        } else {
            Face->ClearOption(TVP_TF_ITALIC);
        }
        if(font.Flags & TVP_TF_BOLD) {
            Face->SetOption(TVP_TF_BOLD);
        } else {
            Face->ClearOption(TVP_TF_BOLD);
        }
        if(font.Flags & TVP_TF_UNDERLINE) {
            Face->SetOption(TVP_TF_UNDERLINE);
        } else {
            Face->ClearOption(TVP_TF_UNDERLINE);
        }
        if(font.Flags & TVP_TF_STRIKEOUT) {
            Face->SetOption(TVP_TF_STRIKEOUT);
        } else {
            Face->ClearOption(TVP_TF_STRIKEOUT);
        }
    }
    // 触字宽度缓存的键前缀：字面名取「实际解析到的那个」，字号与标志都进键。
    // 主字面一变这里就变，旧条目自然再也不会被命中（不需要显式失效）。
    {
        const ttstr &resolved = Face ? Face->GetFontName() : font.Face;
        const tjs_int h = font.Height < 0 ? -font.Height : font.Height;
        CurrentExtentCacheFontKey = resolved.AsStdString() + "|" +
            std::to_string(h) + "|" + std::to_string(font.Flags);
    }
    LastBitmap = nullptr;
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::GetTextExtent(tjs_char ch, tjs_int &w,
                                           tjs_int &h) {
    if(!Face)
        return;

    // 命中缓存直接返回。注意键里带了字面名/字号/标志，所以 ApplyFont
    // 换了主字面后 旧条目不会再被取到，不需要额外失效。
    const GlyphExtentCacheKey cache_key{ CurrentExtentCacheFontKey, ch };
    {
        std::lock_guard<std::mutex> lock(g_glyph_extent_cache_mutex);
        const auto it = g_glyph_extent_cache.find(cache_key);
        if(it != g_glyph_extent_cache.end()) {
            w = it->second.w;
            h = it->second.h;
            return;
        }
    }

    tjs_int resolved_w = 0;
    tjs_int resolved_h = 0;
    tGlyphMetrics metrics{};
    if(Face->GetGlyphSizeFromCharcode(ch, metrics)) {
        resolved_w = metrics.CellIncX;
        resolved_h = metrics.CellIncY;
    } else if(!isUnicodeSpace(ch) && ResolveMode() == FontFallbackMode::Chain &&
              FindChainGlyph(ch, metrics)) {
        resolved_w = metrics.CellIncX;
        resolved_h = metrics.CellIncY;
    } else {
        // 与改动前一致：既没有字形又不是空格时，退回「字面高度」当宽度。
        resolved_w = Face->GetHeight();
        resolved_h = resolved_w;
    }
    w = resolved_w;
    h = resolved_h;

    std::lock_guard<std::mutex> lock(g_glyph_extent_cache_mutex);
    if(g_glyph_extent_cache.size() >= kGlyphExtentCacheLimit)
        g_glyph_extent_cache.clear();
    g_glyph_extent_cache.emplace(
        cache_key, GlyphExtentCacheValue{ resolved_w, resolved_h });
}
//---------------------------------------------------------------------------
tjs_int FreeTypeFontRasterizer::GetAscentHeight() {
    if(Face)
        return Face->GetAscent();
    return 0;
}
static bool isUnicodeSpace(char16_t ch) {
    return (ch >= 0x0009 && ch <= 0x000D) || ch == 0x0020 || ch == 0x0085 ||
        ch == 0x00A0 || ch == 0x1680 || (ch >= 0x2000 && ch <= 0x200A) ||
        ch == 0x2028 || ch == 0x2029 || ch == 0x202F || ch == 0x205F ||
        ch == 0x3000;
}
//---------------------------------------------------------------------------
tTVPCharacterData *
FreeTypeFontRasterizer::GetBitmap(const tTVPFontAndCharacterData &font,
                                  tjs_int aofsx, tjs_int aofsy) {
    if(!Face)
        return nullptr;
    if(font.Antialiased) {
        Face->ClearOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    } else {
        Face->SetOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    }
    if(font.Hinting) {
        Face->ClearOption(TVP_FACE_OPTIONS_NO_HINTING);
        // Face->SetOption( TVP_FACE_OPTIONS_FORCE_AUTO_HINTING );
    } else {
        Face->SetOption(TVP_FACE_OPTIONS_NO_HINTING);
        // Face->ClearOption( TVP_FACE_OPTIONS_FORCE_AUTO_HINTING );
    }
    tTVPCharacterData *data = Face->GetGlyphFromCharcode(font.Character);
    if(!data && !isUnicodeSpace(font.Character)) {
        if(ResolveMode() == FontFallbackMode::Chain) {
            data = GetFallbackGlyph(font.Character);
        } else {
            ApplyFallbackFace();
            if(FaceFallback) {
                data = FaceFallback->GetGlyphFromCharcode(font.Character);
            }
        }
    }
    if(data == nullptr) {
        data = Face->GetGlyphFromCharcode(Face->GetDefaultChar());
    }
    if(data == nullptr) {
        data = Face->GetGlyphFromCharcode(Face->GetFirstChar());
    }
    if(data == nullptr) {
        // Missing glyphs must not abort the frame; return the same empty-glyph
        // fallback used by the non-FreeType rasterizer.
        // 缺少字形时不能中断当前帧；返回与非 FreeType
        // 光栅器一致的空字形降级结果。
        return new tTVPCharacterData();
    }

    int cx = data->Metrics.CellIncX;
    int cy = data->Metrics.CellIncY;
    if(font.Font.Angle == 0) {
        data->Metrics.CellIncX = cx;
        data->Metrics.CellIncY = 0;
    } else if(font.Font.Angle == 2700) {
        data->Metrics.CellIncX = 0;
        data->Metrics.CellIncY = cx;
    } else {
        double angle = font.Font.Angle * (M_PI / 1800);
        data->Metrics.CellIncX = static_cast<tjs_int>(std::cos(angle) * cx);
        data->Metrics.CellIncY = static_cast<tjs_int>(-std::sin(angle) * cx);
    }

    data->Antialiased = font.Antialiased;
    data->FullColored = false;
    data->Blured = font.Blured;
    data->BlurWidth = font.BlurWidth;
    data->BlurLevel = font.BlurLevel;
    data->OriginX += aofsx; // for vertical text
                            //	data->OriginY += aofsy;

    // apply blur
    if(font.Blured)
        data->Blur(); // nasty ...
    return data;
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::GetGlyphDrawRect(const ttstr &text,
                                              tTVPRect &area) {
    if(!Face) {
        area.left = area.top = area.right = area.bottom = 0;
        return;
    }
    Face->ClearOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    Face->ClearOption(TVP_FACE_OPTIONS_NO_HINTING);

    const bool use_chain = ResolveMode() == FontFallbackMode::Chain;
    area.left = area.top = area.right = area.bottom = 0;
    tjs_int offsetx = 0;
    tjs_int offsety = 0;
    tjs_uint len = text.length();
    for(tjs_uint i = 0; i < len; i++) {
        tjs_char ch = text[i];
        tjs_int ax, ay;
        tTVPRect rt(0, 0, 0, 0);
        bool result = Face->GetGlyphRectFromCharcode(rt, ch, ax, ay);
        if(result == false && !isUnicodeSpace(ch) && use_chain) {
            ApplyFallbackFaces();
            for(auto *fallback : FaceFallbacks) {
                if(!fallback)
                    continue;
                result = fallback->GetGlyphRectFromCharcode(rt, ch, ax, ay);
                if(result) {
                    rt.add_offsets(
                        0,
                        krkr::font::ComputeFallbackBaselineAdjustment(
                            Face->GetAscent(), fallback->GetAscent()));
                    break;
                }
            }
        }
        if(result == false)
            result = Face->GetGlyphRectFromCharcode(rt, Face->GetDefaultChar(),
                                                    ax, ay);
        if(result == false)
            result = Face->GetGlyphRectFromCharcode(rt, Face->GetFirstChar(),
                                                    ax, ay);
        if(result) {
            rt.add_offsets(offsetx, offsety);
            if(i != 0) {
                area.do_union(rt);
            } else {
                area = rt;
            }
        }
        offsetx += ax;
        offsety = 0;
    }
}
