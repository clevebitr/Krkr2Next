//
// E-mote (M2) PSB 位域 / 默认值对照表 —— 本项目自行整理，仅供解析与诊断使用。
//
// 语义对账过两份**互相独立**的来源：
//
//   ① 本项目既有口径（libkrkr2 / AetherKiri 参考实现）
//      PlayerInternal.h:1921-2035（帧内容 mask → 字段）、:1874-1885
//      （mergeFrameContent，缺键=什么都不读）、PlayerUpdateLayers.cpp:4690
//      （entry.sortKey = node.accumulated.posZ）。见 PSBMedia.h / Player.h
//      里各字段的注释。
//
//   ② FreeMote（https://github.com/UlyssesWu/FreeMote）
//      FreeMote/Motion/MmoEnums.cs（MmoFrameMask，位表 + 逐位具名）、
//      FreeMote.PsBuild/MmoBuilder.cs（inheritMask 展开、帧内容默认值表、
//      stencilType 转换、meshSyncChildMask 展开）。
//
// ⚠️ 许可证边界（重要）：FreeMote 是 CC BY-NC-SA 4.0（署名-非商业性使用-
//    相同方式共享），**不可 vendored、不可复制其代码**。本文件是**按格式事实
//    自行书写**的常量表（位号、键名、默认值属于文件格式的事实），既没有复制
//    FreeMote 的代码，也没有把它链接进引擎。参考来源只以"出处注释"的形式出现。
//    如需在文档/交接里引用其结论，同样只写结论 + 注明出处，不要摘抄实现。
//
// 用途：把"魔法数字"集中到一处，并在解析期统计真实数据里到底出现了哪些位，
//      从而决定哪些位值得实现（见 PSBMedia.cpp 的 ParseMaskStats）。
//
#pragma once

#include <cstdint>
#include <string>

namespace PSB::Emote {

    // ---------------------------------------------------------------------
    // content["mask"]：帧内容位域
    //
    // 语义 = "本帧真正携带哪些 content 键"。位为 0 ⇒ 该键等同于**不存在**
    // （不是"等于 0"），解析时必须复位回默认值。krkr spec 的 content 一定带
    // `mask`；bit0 在 krkr 里的含义是"是否使用 ox/oy"（FreeMote 的 spec 转换器
    // 在 krkr→common 时于 ox=oy=0 的情况下会删掉 ox/oy 并清掉 bit0）。
    // ---------------------------------------------------------------------
    namespace ContentMask {
        constexpr int Origin       = 1 << 0;  // ox, oy
        constexpr int Coord        = 1 << 1;  // coord[0]=x, coord[1]=y, coord[2]=z
        constexpr int FlipX        = 1 << 2;  // fx（fx/fy 通常成对出现）
        constexpr int FlipY        = 1 << 3;  // fy
        constexpr int Angle        = 1 << 4;  // angle（度）
        constexpr int ZoomX        = 1 << 5;  // zx
        constexpr int ZoomY        = 1 << 6;  // zy
        constexpr int SlantX       = 1 << 7;  // sx
        constexpr int SlantY       = 1 << 8;  // sy
        constexpr int Color        = 1 << 9;  // color（顶点色/平涂）
        constexpr int Opacity      = 1 << 10; // opa（老数据里也有 "op"）
        constexpr int CoordCurve   = 1 << 11; // ccc：坐标插值曲线
        constexpr int AngleCurve   = 1 << 12; // acc：角度插值曲线
        constexpr int ZoomCurve    = 1 << 13; // zcc：缩放插值曲线
        constexpr int SlantCurve   = 1 << 14; // scc：斜切插值曲线
        constexpr int OpacityCurve = 1 << 15; // occ：透明度/颜色插值曲线
        // 位 16 在①②两份来源里都没有说明（FreeMote 自己标的是 "???"）。
        // 这里保留位号、只做统计，不做任何语义假设。
        constexpr int Unknown16    = 1 << 16;
        constexpr int BlendMode    = 1 << 17; // bm
        // 位 18 未见于任何来源（FreeMote 的位表同样从 17 直接跳到 19）。
        constexpr int Motion       = 1 << 19; // 子运动引用（mdt/mdtgt/mdofst…）
        constexpr int Particle     = 1 << 20; // prt（粒子参数）
        constexpr int Mesh         = 1 << 25; // mesh（变形数据）
        // 位 21..24、26..31：未见于任何来源。

        // 已经实现（= 会复位/门控）的位。其余位只统计并告警，不做语义动作。
        constexpr int kImplemented =
            Origin | Coord | FlipX | FlipY | Angle | ZoomX | ZoomY | SlantX |
            SlantY | Opacity | BlendMode;

        /// 单个位 → 该位管辖的 content 键（用于诊断：位与键是否自洽）。
        /// 返回该键所属的位；键不在表内返回 0。
        inline int BitOfKey(const std::string &key) {
            if(key == "ox" || key == "oy") return Origin;
            if(key == "coord") return Coord;
            if(key == "fx") return FlipX;
            if(key == "fy") return FlipY;
            if(key == "angle") return Angle;
            if(key == "zx") return ZoomX;
            if(key == "zy") return ZoomY;
            if(key == "sx") return SlantX;
            if(key == "sy") return SlantY;
            if(key == "color") return Color;
            if(key == "opa" || key == "op") return Opacity;
            if(key == "ccc") return CoordCurve;
            if(key == "acc") return AngleCurve;
            if(key == "zcc") return ZoomCurve;
            if(key == "scc") return SlantCurve;
            if(key == "occ") return OpacityCurve;
            if(key == "bm") return BlendMode;
            if(key == "motion") return Motion;
            if(key == "prt") return Particle;
            if(key == "mesh") return Mesh;
            return 0;
        }

        /// 位号（0..31）→ 名字，用于日志。
        inline const char *NameOfBit(int bit) {
            switch(bit) {
                case 0: return "ox/oy";
                case 1: return "coord";
                case 2: return "fx";
                case 3: return "fy";
                case 4: return "angle";
                case 5: return "zx";
                case 6: return "zy";
                case 7: return "sx";
                case 8: return "sy";
                case 9: return "color";
                case 10: return "opa";
                case 11: return "ccc";
                case 12: return "acc";
                case 13: return "zcc";
                case 14: return "scc";
                case 15: return "occ";
                case 16: return "?(bit16)";
                case 17: return "bm";
                case 19: return "motion";
                case 20: return "prt";
                case 25: return "mesh";
                default: return "?(unknown)";
            }
        }
    } // namespace ContentMask

    // ---------------------------------------------------------------------
    // layer["inheritMask"]：逐节点的**变换继承**门控（位=1 表示继承父级该项）
    //
    // 位号来自 FreeMote.PsBuild/MmoBuilder.cs（把 inheritMask 展开成具名的
    // inheritXxx 字段）。本项目原先只用 0x1FC 作默认值 + 在 Player.h 里门控
    // 0x020/0x040，这里补全并集中。注意 0x1FC = bits 2..8 = 全部仿射项，
    // 与下表完全吻合（本项目默认值可作独立交叉验证）。
    // ---------------------------------------------------------------------
    namespace InheritMask {
        constexpr int FlipX       = 0x00000004; // bit 2
        constexpr int FlipY       = 0x00000008; // bit 3
        constexpr int Angle       = 0x00000010; // bit 4
        constexpr int ZoomX       = 0x00000020; // bit 5
        constexpr int ZoomY       = 0x00000040; // bit 6
        constexpr int SlantX      = 0x00000080; // bit 7
        constexpr int SlantY      = 0x00000100; // bit 8
        constexpr int ColorWeight = 0x00000200; // bit 9
        constexpr int Opacity     = 0x00000400; // bit 10
        constexpr int Parent      = 0x00400000; // bit 22
        constexpr int Shape       = 0x02000000; // bit 25（mesh shape 同步）

        /// 本项目沿用的"默认全继承仿射项"。
        constexpr int kDefault = FlipX | FlipY | Angle | ZoomX | ZoomY |
                                 SlantX | SlantY;

        inline const char *NameOfBit(int bit) {
            switch(bit) {
                case 2: return "flipX";
                case 3: return "flipY";
                case 4: return "angle";
                case 5: return "zoomX";
                case 6: return "zoomY";
                case 7: return "slantX";
                case 8: return "slantY";
                case 9: return "colorWeight";
                case 10: return "opacity";
                case 22: return "parent";
                case 25: return "shape";
                default: return nullptr;
            }
        }
    } // namespace InheritMask

    // ---------------------------------------------------------------------
    // layer["stencilType"] / layer["type"]
    //
    // type == 12 是"模板/蒙版层"（stencil layer）。stencilType 在 FreeMote 眼里
    // 是**枚举而不是位标志**：其 PSB→MMO 转换明确写着 5 → 1（即 PSB 侧比 MMO
    // 侧多一个常量偏移）。因此本项目原先的 (stencilType & 4) 判据在数据上可能
    // 恒真/恒假，需要用真实取值分布来判断；"是否带作者声明的蒙版层列表"更可靠
    // 的信号是 layer["stencilCompositeMaskLayerList"] 非空（→
    // PSBMotionNode::stencilMaskLabels）。
    // ---------------------------------------------------------------------
    namespace Stencil {
        constexpr int kLayerType = 12;      // layer["type"]
        constexpr int kPsbValueOffset = 4;  // FreeMote: PSB 5 ↔ MMO 1
    } // namespace Stencil

    // ---------------------------------------------------------------------
    // 帧内容的**默认值**（bit 为 0 时要复位回这里）
    //
    // 数值出处：FreeMote.PsBuild/MmoBuilder.cs 的 DefaultFrameListContent /
    // DefaultFrameListContent_Motion / DefaultFrameListContent_Stencil，以及
    // 参考实现的 value_or(...) 默认值。本项目里已经实现的部分见
    // PSBMedia.cpp::ApplyFrameContentMask。
    // ---------------------------------------------------------------------
    namespace Defaults {
        constexpr float Coord = 0.0f;
        constexpr float Origin = 0.0f;
        constexpr float Angle = 0.0f;
        constexpr float Zoom = 1.0f;
        constexpr float Slant = 0.0f;
        constexpr float Opacity = 255.0f;
        /// 帧顶点色"无 tint"的打包值。注意它**不是** 4×8bit 通道：
        /// 参考实现（PlayerInternal.h:1441）注释写明 "not four scalar channels"，
        /// 而本项目目前用 0xFFFFFFFF（= ARGB8 全白）当默认值。两者不可混用：
        /// 这个差异只做对照，实际行为由 kGateColorByMask / kUsePackedVertexColorDefault 控制。
        constexpr std::uint32_t PackedVertexColor = 0xFF808080u;
        /// content["bm"] 缺省值。参考实现按 value_or(0) 取 0，而 FreeMote 的
        /// 帧内容默认表写的是 16 —— 两者不一致，故留成开关做 A/B。
        constexpr int BlendMode = 16;
    } // namespace Defaults

    // ---------------------------------------------------------------------
    // 本轮的**行为开关**：默认全部关闭 = 本轮不改变任何既有渲染行为，
    // 只产出日志。翻成 true 即启用对应语义（便于真机 A/B，一次只翻一个）。
    // ---------------------------------------------------------------------
    namespace Switches {
        /// 位 9 为 0 时把帧顶点色复位为 Defaults::PackedVertexColor。
        constexpr bool kGateColorByMask = false;
        /// 位 11..15 为 0 时清掉对应通道的插值曲线（ccc/acc/zcc/scc/occ）。
        constexpr bool kGateCurvesByMask = false;
        /// 位 17 为 0 时用 Defaults::BlendMode（16）而不是 0。
        constexpr bool kBlendModeDefault16 = false;
    } // namespace Switches

    /// 解析期位域统计：把"真实数据里出现了哪些位"变成可判读的日志。
    /// 只做加法与打印，不参与渲染决策。
    struct MaskStats {
        long frames = 0;               // 统计过的帧数（带 mask 的 content）
        long framesWithMask = 0;       // 真的存在 mask 键的帧数（krkr=全部）
        long maskBitSeen[32] = {};     // 每个位在数据里出现（=1）的次数
        long bitSetKeyMissing[32] = {}; // 位=1 但字典里没有该键（位表 vs 数据的自洽性）
        long bitClearKeyPresent[32] = {}; // 位=0 但字典里有该键（复位口径是否真的生效）
        long inheritMaskSeen[32] = {};  // inheritMask 每个位出现的次数
        long stencilTypeValue[64] = {}; // stencilType 取值直方图（0..63）
        long stencilWithLabels = 0;     // 带 stencilCompositeMaskLayerList 的 stencil 层数
        long stencilWithoutLabels = 0;  // 不带作者声明蒙版列表的 stencil 层数
        long colorBitSet = 0;           // 位 9 置位的帧数
        long colorBitClear = 0;         // 位 9 未置位的帧数
        long unhandledBitSeen[32] = {}; // 出现但尚未实现的位（用于告警）

        void reset() { *this = MaskStats{}; }

        /// 出现过的位（任一帧置位）→ 逗号分隔的位名。
        std::string SeenBits() const {
            std::string out;
            for(int bit = 0; bit < 32; ++bit) {
                if(maskBitSeen[bit] <= 0)
                    continue;
                if(!out.empty())
                    out += ",";
                out += ContentMask::NameOfBit(bit);
                out += "(";
                out += std::to_string(maskBitSeen[bit]);
                out += ")";
            }
            return out.empty() ? std::string("-") : out;
        }

        /// 位=0 但键存在 → 说明"复位口径"在真实数据里确实会被触发。
        std::string ResetTriggeredBits() const {
            std::string out;
            for(int bit = 0; bit < 32; ++bit) {
                if(bitClearKeyPresent[bit] <= 0)
                    continue;
                if(!out.empty())
                    out += ",";
                out += ContentMask::NameOfBit(bit);
                out += "(";
                out += std::to_string(bitClearKeyPresent[bit]);
                out += ")";
            }
            return out.empty() ? std::string("-") : out;
        }

        /// 位=1 但键不存在 → 位表与数据不符（解析口径需要复核）。
        std::string TableMismatchBits() const {
            std::string out;
            for(int bit = 0; bit < 32; ++bit) {
                if(bitSetKeyMissing[bit] <= 0)
                    continue;
                if(!out.empty())
                    out += ",";
                out += ContentMask::NameOfBit(bit);
                out += "(";
                out += std::to_string(bitSetKeyMissing[bit]);
                out += ")";
            }
            return out.empty() ? std::string("-") : out;
        }

        std::string InheritBits() const {
            std::string out;
            for(int bit = 31; bit >= 0; --bit) {
                const char *name = InheritMask::NameOfBit(bit);
                if(!name || inheritMaskSeen[bit] <= 0)
                    continue;
                if(!out.empty())
                    out += ",";
                out += name;
                out += "(";
                out += std::to_string(inheritMaskSeen[bit]);
                out += ")";
            }
            return out.empty() ? std::string("-") : out;
        }

        std::string StencilTypes() const {
            std::string out;
            for(int v = 0; v < 64; ++v) {
                if(stencilTypeValue[v] <= 0)
                    continue;
                if(!out.empty())
                    out += ",";
                out += std::to_string(v);
                out += "(";
                out += std::to_string(stencilTypeValue[v]);
                out += ")";
            }
            out += " labels:";
            out += std::to_string(stencilWithLabels);
            out += "/";
            out += std::to_string(stencilWithLabels + stencilWithoutLabels);
            return out;
        }

        std::string UnhandledBits() const {
            std::string out;
            for(int bit = 0; bit < 32; ++bit) {
                if(unhandledBitSeen[bit] <= 0)
                    continue;
                if(!out.empty())
                    out += ",";
                out += ContentMask::NameOfBit(bit);
                out += "(";
                out += std::to_string(unhandledBitSeen[bit]);
                out += ")";
            }
            return out.empty() ? std::string("-") : out;
        }
    };

} // namespace PSB::Emote
