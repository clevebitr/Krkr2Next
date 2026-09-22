//
// AlphaMovie（AJPM）帧载荷解码器。
//
// 为什么在 core：.amv 由 core 的 tTVPGraphicType 注册（GraphicsLoaderIntf.cpp:238），
// 图形与头部两条路径都在 core。把解码器放这里，插件与 core 共用一份实现
// （依赖方向：cpp/plugins → cpp/core 合法，反向不允许，见 compat/README.md §2）。
//
// AlphaMovie 的帧载荷不是标准 JPEG：没有 SOI/DHT（Huffman 用标准表，见下表规格），
// DQT 取自文件头 quantaization_table_size_plus_hdr_size 之后的 3x64 字节。
// 所以 turbojpeg 会报 "Could not determine subsampling level"。
//
// 解码原语与 MCU 循环逐字移植自 AetherKiri cpp/plugins/alphamovie.cpp
// （Huffman 表规格 193-258、构建 259-430、DC/AC 解码 436-968、IDCT 969-1240、
//  MCU 循环 2023-2596）；仅把成员变量改为参数、把流读取改为载荷拷贝。
#pragma once

#include "tjsCommHead.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace krkr::alphamovie {

// 一帧的裁剪几何（AMV 帧头里的 4 个 uint16；上游注释明确它们是
// copyNextImageToTexture 返回的 Rect.left/top 与宽高，不是 alpha 平面尺寸）。
struct FrameGeometry {
    uint16_t left = 0;
    uint16_t top = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

// 把一帧载荷解成紧致 RGBA（geo.width * geo.height * 4）。
//   qtbl        文件头之后的 3 张量化表（各 64 字节）
//   isZlib      alpha_decode_attr == 2
//   zlibBufSize zlib 模式下彩色段之前的 alpha 压缩长度
// 失败返回 false（调用方应回退到标准 JPEG 路径）。
bool DecodeFrameToRgba(uint8_t qtbl[3][64], bool isZlib, uint32_t zlibBufSize,
                       const FrameGeometry &geo, const uint8_t *payload,
                       size_t payloadLen, std::vector<uint8_t> &outRgba);

} // namespace krkr::alphamovie
