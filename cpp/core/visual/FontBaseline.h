#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

// 逐字回退时的基线对齐工具。
//
// 为什么需要：一行文字的 y
// 只按**主字面**算一次，而缺字可能来自若干个回退字面。各字面 的
// ascender/descender 不同，若直接拿回退字形的 originY
// 去画，相邻字符会在垂直方向 上下跳。这里把回退字形挪回主字面的基线上。
//
// 与 AetherKiri 的同名文件保持同一算法（那边是逐字回退链的配套件）。
namespace krkr::font {

    inline int ComputeLineBaseline(int lineHeight, int ascenderUnits,
                                   int descenderUnits, int pixelsPerEm,
                                   int unitsPerEm) {
        if(lineHeight <= 0 || pixelsPerEm <= 0 || unitsPerEm <= 0)
            return 0;

        const auto scale = [pixelsPerEm, unitsPerEm](int units) {
            return static_cast<int>(static_cast<std::int64_t>(units) *
                                    pixelsPerEm / unitsPerEm);
        };
        const int ascent = scale(ascenderUnits);
        const int descent = std::max(0, -scale(descenderUnits));
        return std::clamp(ascent, 0, std::max(0, lineHeight - descent));
    }

    inline int ComputeFallbackBaselineAdjustment(int requestedBaseline,
                                                 int fallbackBaseline) {
        return requestedBaseline - fallbackBaseline;
    }

    inline int ComputeGlyphOriginY(int lineBaseline, int glyphBearingY) {
        return lineBaseline - glyphBearingY;
    }

    /**
     * 有些脚本助手会在一个小而透明的图层**最顶端**画文字。当字面的设计 ascender 比它的
     * 逻辑行框高时，字形的 top 合法地会是负数 —— 把这一次绘制向下推到裁剪框内，使
     * 字与描边都能看见（共享的行基线不动）。
     *
     * 移植自 AetherKiri `cpp/core/visual/FontBaseline.h`。
     */
    inline int ClampTextOriginToClipTop(int originY, int glyphTop,
                                        int outlineWidth, int clipTop) {
        const int inkTop = originY + glyphTop - std::max(0, outlineWidth);
        return inkTop < clipTop ? originY + (clipTop - inkTop) : originY;
    }

    /**
     * 模糊阴影向四周扩张 `shadowWidth` 后再按偏移移动；返回字形上方需要的额外留白
     * （阴影下移足够多时不需要额外留白）。移植自 AetherKiri 的同名文件。
     */
    inline int ComputeTextShadowTopPadding(int shadowLevel, int shadowWidth,
                                           int shadowOffsetY) {
        if(shadowLevel == 0)
            return 0;

        const std::int64_t width = std::max<std::int64_t>(
            -static_cast<std::int64_t>(shadowWidth),
            static_cast<std::int64_t>(shadowWidth));
        const std::int64_t padding =
            std::max<std::int64_t>(0, width - shadowOffsetY);
        return static_cast<int>(std::min<std::int64_t>(
            padding, std::numeric_limits<int>::max()));
    }

} // namespace krkr::font
