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

} // namespace krkr::font
