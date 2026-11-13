//
// Created by LiDon on 2025/9/11.
//

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <spdlog/spdlog.h>

#include "utils/LogUtil.h"

#include "PSBMedia.h"

#include "PSBFile.h"
#include "EmoteMaskBits.h"
#include "resources/ImageMetadata.h"
#include "MsgIntf.h"
#include "Platform.h"
#include "SysInitIntf.h"
#include "UtilStreams.h"
#include "GraphicsLoaderIntf.h"
#include "../motionplayer/ResourceManager.h"

namespace PSB {
#define LOGGER spdlog::get("plugin")

    namespace {
        // 解析期位域统计（thread_local：PSB 解析发生在加载线程，避免并发解析
        // 互相污染）。由 CollectAllMotionNodeTrees() 在每个 archive 复位一次并在
        // 结束时打印一行汇总。只统计、不影响渲染。
        thread_local Emote::MaskStats g_emoteMaskStats;

        size_t CalcEntryFootprint(const PSBMedia::CacheEntry &entry) {
            size_t total = entry.resource ? entry.resource->data.size() : 0;
            if(entry.convertedImage) {
                total += entry.convertedImage->size();
            }
            return total;
        }

        bool IsSupportedImageHeader(const std::vector<uint8_t> &data) {
            if(data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
               data[2] == 0x4e && data[3] == 0x47) {
                return true;
            }
            if(data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
                return true;
            }
            if(data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 &&
               data[2] == 0xff) {
                return true;
            }
            if(data.size() >= 3 && data[0] == 'T' && data[1] == 'L' &&
               data[2] == 'G') {
                return true;
            }
            return false;
        }

        std::string ArchiveBoundaryKey(const std::string &key) {
            std::string lower = key;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](const unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });

            size_t boundary = std::string::npos;
            for(const auto *extension : { ".mtn/", ".psb/", ".pimg/" }) {
                const auto position = lower.find(extension);
                if(position == std::string::npos)
                    continue;
                const auto candidate = position + std::strlen(extension) - 1;
                if(boundary == std::string::npos || candidate < boundary)
                    boundary = candidate;
            }
            if(boundary != std::string::npos)
                return key.substr(0, boundary);

            const auto slash = key.find('/');
            return slash == std::string::npos ? key : key.substr(0, slash);
        }

        uint16_t ReadLE16(const uint8_t *src) {
            return static_cast<uint16_t>(src[0]) |
                static_cast<uint16_t>(src[1] << 8);
        }

        uint16_t ReadBE16(const uint8_t *src) {
            return static_cast<uint16_t>(src[1]) |
                static_cast<uint16_t>(src[0] << 8);
        }

        bool DecompressRLPixel(const std::vector<uint8_t> &input,
                               std::vector<uint8_t> &output, size_t align) {
            if(align == 0 || input.empty() || output.empty()) {
                return false;
            }

            constexpr uint8_t kLookAhead = 1u << 7;
            size_t inPos = 0;
            size_t outPos = 0;

            while(inPos < input.size()) {
                const uint8_t cmdByte = input[inPos++];
                if(cmdByte & kLookAhead) {
                    const size_t count =
                        static_cast<size_t>((cmdByte ^ kLookAhead) + 3);
                    if(inPos + align > input.size()) {
                        return false;
                    }
                    if(outPos + count * align > output.size()) {
                        return false;
                    }
                    for(size_t i = 0; i < count; ++i) {
                        memcpy(output.data() + outPos, input.data() + inPos,
                               align);
                        outPos += align;
                    }
                    inPos += align;
                } else {
                    const size_t count =
                        static_cast<size_t>(cmdByte + 1) * align;
                    if(inPos + count > input.size()) {
                        return false;
                    }
                    if(outPos + count > output.size()) {
                        return false;
                    }
                    memcpy(output.data() + outPos, input.data() + inPos, count);
                    inPos += count;
                    outPos += count;
                }
            }

            return outPos == output.size();
        }

        std::shared_ptr<std::vector<uint8_t>>
        BuildBmpFromRaw(const PSBMedia::CachedImageInfo &info,
                        const std::shared_ptr<PSBResource> &resource) {
            if(!resource || info.width <= 0 || info.height <= 0) {
                return nullptr;
            }

            const auto &rawSrc = resource->data;
            const size_t pixelCount = static_cast<size_t>(info.width) *
                static_cast<size_t>(info.height);
            if(pixelCount == 0) {
                return nullptr;
            }

            std::vector<uint8_t> rlDecoded;
            size_t decodedAlign = 0;
            const std::vector<uint8_t> *src = &rawSrc;

            auto inferAlign = [&]() -> size_t {
                const auto typedFormat =
                    Extension::toPSBPixelFormat(info.type, info.spec);
                switch(typedFormat) {
                    case PSBPixelFormat::A8:
                    case PSBPixelFormat::L8:
                    case PSBPixelFormat::A8_SW:
                    case PSBPixelFormat::L8_SW:
                    case PSBPixelFormat::TileA8_SW:
                    case PSBPixelFormat::TileL8_SW:
                    case PSBPixelFormat::CI8:
                    case PSBPixelFormat::CI8_SW:
                    case PSBPixelFormat::CI8_SW_PSP:
                    case PSBPixelFormat::TileCI8:
                        return 1;
                    case PSBPixelFormat::A8L8:
                    case PSBPixelFormat::A8L8_SW:
                    case PSBPixelFormat::TileA8L8_SW:
                    case PSBPixelFormat::LeRGBA4444:
                    case PSBPixelFormat::BeRGBA4444:
                    case PSBPixelFormat::LeRGBA4444_SW:
                    case PSBPixelFormat::TileLeRGBA4444_SW:
                    case PSBPixelFormat::RGBA5650:
                    case PSBPixelFormat::RGBA5650_SW:
                    case PSBPixelFormat::TileRGBA5650_SW:
                    case PSBPixelFormat::CI4:
                    case PSBPixelFormat::CI4_SW:
                    case PSBPixelFormat::CI4_SW_PSP:
                    case PSBPixelFormat::TileCI4:
                    case PSBPixelFormat::RGB5A3:
                        return 2;
                    case PSBPixelFormat::LeRGBA8:
                    case PSBPixelFormat::BeRGBA8:
                    case PSBPixelFormat::LeRGBA8_SW:
                    case PSBPixelFormat::BeRGBA8_SW:
                    case PSBPixelFormat::FlipLeRGBA8_SW:
                    case PSBPixelFormat::FlipBeRGBA8_SW:
                    case PSBPixelFormat::TileLeRGBA8_SW:
                    case PSBPixelFormat::TileBeRGBA8_SW:
                    case PSBPixelFormat::TileBeRGBA8_Rvl:
                        return 4;
                    default:
                        if(!info.palette.empty()) {
                            return 1;
                        }
                        return 0;
                }
            };

            if(info.compress == PSBCompressType::RL) {
                std::vector<size_t> candidateAligns;
                if(const size_t inferred = inferAlign(); inferred != 0) {
                    candidateAligns.push_back(inferred);
                }
                for(size_t align :
                    { static_cast<size_t>(4), static_cast<size_t>(3),
                      static_cast<size_t>(2), static_cast<size_t>(1) }) {
                    if(std::find(candidateAligns.begin(), candidateAligns.end(),
                                 align) == candidateAligns.end()) {
                        candidateAligns.push_back(align);
                    }
                }
                for(const size_t align : candidateAligns) {
                    std::vector<uint8_t> trial(pixelCount * align);
                    if(!DecompressRLPixel(rawSrc, trial, align)) {
                        continue;
                    }
                    rlDecoded = std::move(trial);
                    src = &rlDecoded;
                    decodedAlign = align;
                    break;
                }
                if(src == &rawSrc) {
                    decodedAlign = 0;
                    if(LOGGER)
                        LOGGER->warn("convertImage: key='{}' RL decode failed "
                                     "for {}x{} raw={}B; "
                                     "falling back to raw palette decoding",
                                     info.debugKey, info.width, info.height,
                                     rawSrc.size());
                }
            }

            if(LOGGER &&
               (info.debugKey.rfind("main.psb/", 0) == 0 ||
                info.debugKey.rfind("title.psb/", 0) == 0 ||
                info.debugKey.rfind("chapter.psb/", 0) == 0 ||
                info.debugKey.rfind("autoskip.psb/", 0) == 0)) {
                LOGGER->info("psb build: key={} decodedAlign={} decodedSize={} "
                             "rawSize={}",
                             info.debugKey, decodedAlign, src->size(),
                             rawSrc.size());
            }

            PSBPixelFormat format = Extension::toPSBPixelFormat(
                info.type.empty() ? "RGBA8" : info.type, info.spec);
            const bool assumeBGRA =
                info.type.empty() && decodedAlign == 4 && info.palette.empty();
            if(info.type.empty()) {
                if(!info.palette.empty()) {
                    if(src->size() == pixelCount) {
                        format = PSBPixelFormat::CI8;
                    } else if(src->size() * 2 == pixelCount) {
                        format = PSBPixelFormat::CI4;
                    }
                } else if(decodedAlign == 4 || src->size() == pixelCount * 4) {
                    format = PSBPixelFormat::LeRGBA8;
                } else if(decodedAlign == 3 || src->size() == pixelCount * 3) {
                    format = PSBPixelFormat::None;
                } else if(decodedAlign == 2 || src->size() == pixelCount * 2) {
                    format = PSBPixelFormat::RGBA5650;
                } else if(decodedAlign == 1 || src->size() == pixelCount) {
                    format = PSBPixelFormat::L8;
                }
            }

            const auto paletteFormat =
                Extension::toPSBPixelFormat(info.paletteType, info.spec);

            const auto build32Bmp = [&](auto &&pixelWriter) {
                const size_t pitch = static_cast<size_t>(info.width) * 4;
                const size_t headerSize = sizeof(TVP_WIN_BITMAPFILEHEADER) +
                    sizeof(TVP_WIN_BITMAPINFOHEADER);
                const size_t fileSize =
                    headerSize + pitch * static_cast<size_t>(info.height);

                auto out = std::make_shared<std::vector<uint8_t>>(fileSize);
                auto *dst = out->data();

                TVP_WIN_BITMAPFILEHEADER bfh{};
                bfh.bfType = 'B' + ('M' << 8);
                bfh.bfSize = static_cast<tjs_uint32>(fileSize);
                bfh.bfOffBits = static_cast<tjs_uint32>(headerSize);
                memcpy(dst, &bfh, sizeof bfh);

                TVP_WIN_BITMAPINFOHEADER bih{};
                bih.biSize = sizeof(bih);
                bih.biWidth = info.width;
                bih.biHeight = info.height;
                bih.biPlanes = 1;
                bih.biBitCount = 32;
                bih.biCompression = 0;
                memcpy(dst + sizeof(bfh), &bih, sizeof bih);

                auto *pixelDst = dst + headerSize;
                for(int y = 0; y < info.height; ++y) {
                    auto *rowDst = pixelDst +
                        static_cast<size_t>(info.height - 1 - y) * pitch;
                    for(int x = 0; x < info.width; ++x) {
                        pixelWriter(x, y, rowDst + static_cast<size_t>(x) * 4);
                    }
                }
                return out;
            };

            const auto writePalettePixel = [&](uint32_t paletteIndex,
                                               uint8_t *dstPx) {
                if(info.palette.empty()) {
                    dstPx[0] = 0;
                    dstPx[1] = 0;
                    dstPx[2] = 0;
                    dstPx[3] = 0xff;
                    return;
                }

                switch(paletteFormat == PSBPixelFormat::None
                           ? Extension::defaultPalettePixelFormat(info.spec)
                           : paletteFormat) {
                    case PSBPixelFormat::LeRGBA8:
                    case PSBPixelFormat::BeRGBA8:
                    default: {
                        const size_t index =
                            static_cast<size_t>(paletteIndex) * 4;
                        if(index + 3 >= info.palette.size()) {
                            dstPx[0] = 0;
                            dstPx[1] = 0;
                            dstPx[2] = 0;
                            dstPx[3] = 0xff;
                            return;
                        }
                        dstPx[0] = info.palette[index + 2];
                        dstPx[1] = info.palette[index + 1];
                        dstPx[2] = info.palette[index + 0];
                        dstPx[3] = info.palette[index + 3];
                        return;
                    }
                    case PSBPixelFormat::RGB5A3: {
                        const size_t index =
                            static_cast<size_t>(paletteIndex) * 2;
                        if(index + 1 >= info.palette.size()) {
                            dstPx[0] = 0;
                            dstPx[1] = 0;
                            dstPx[2] = 0;
                            dstPx[3] = 0xff;
                            return;
                        }
                        const uint16_t v = ReadBE16(&info.palette[index]);
                        if(v & 0x8000) {
                            const uint8_t r = static_cast<uint8_t>(
                                ((v >> 10) & 0x1f) * 255 / 31);
                            const uint8_t g = static_cast<uint8_t>(
                                ((v >> 5) & 0x1f) * 255 / 31);
                            const uint8_t b =
                                static_cast<uint8_t>((v & 0x1f) * 255 / 31);
                            dstPx[0] = b;
                            dstPx[1] = g;
                            dstPx[2] = r;
                            dstPx[3] = 0xff;
                        } else {
                            const uint8_t a = static_cast<uint8_t>(
                                ((v >> 12) & 0x7) * 255 / 7);
                            const uint8_t r =
                                static_cast<uint8_t>(((v >> 8) & 0xf) * 17);
                            const uint8_t g =
                                static_cast<uint8_t>(((v >> 4) & 0xf) * 17);
                            const uint8_t b =
                                static_cast<uint8_t>((v & 0xf) * 17);
                            dstPx[0] = b;
                            dstPx[1] = g;
                            dstPx[2] = r;
                            dstPx[3] = a;
                        }
                        return;
                    }
                }
            };

            switch(format) {
                case PSBPixelFormat::LeRGBA8:
                case PSBPixelFormat::BeRGBA8:
                case PSBPixelFormat::LeRGBA8_SW:
                case PSBPixelFormat::BeRGBA8_SW:
                case PSBPixelFormat::FlipLeRGBA8_SW:
                case PSBPixelFormat::FlipBeRGBA8_SW:
                case PSBPixelFormat::TileLeRGBA8_SW:
                case PSBPixelFormat::TileBeRGBA8_SW:
                case PSBPixelFormat::TileBeRGBA8_Rvl:
                case PSBPixelFormat::None: {
                    if(src->size() >= pixelCount * 3 &&
                       src->size() < pixelCount * 4) {
                        return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                            const size_t index =
                                (static_cast<size_t>(y) * info.width + x) * 3;
                            dstPx[0] = (*src)[index + 2];
                            dstPx[1] = (*src)[index + 1];
                            dstPx[2] = (*src)[index + 0];
                            dstPx[3] = 0xff;
                        });
                    }
                    if(src->size() < pixelCount * 4) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 4;
                        if(assumeBGRA) {
                            dstPx[0] = (*src)[index + 0];
                            dstPx[1] = (*src)[index + 1];
                            dstPx[2] = (*src)[index + 2];
                        } else {
                            dstPx[0] = (*src)[index + 2];
                            dstPx[1] = (*src)[index + 1];
                            dstPx[2] = (*src)[index + 0];
                        }
                        dstPx[3] = (*src)[index + 3];
                    });
                }
                case PSBPixelFormat::A8: {
                    if(src->size() < pixelCount) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            static_cast<size_t>(y) * info.width + x;
                        dstPx[0] = 0xff;
                        dstPx[1] = 0xff;
                        dstPx[2] = 0xff;
                        dstPx[3] = (*src)[index];
                    });
                }
                case PSBPixelFormat::L8:
                case PSBPixelFormat::L8_SW:
                case PSBPixelFormat::TileL8_SW: {
                    if(src->size() < pixelCount) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            static_cast<size_t>(y) * info.width + x;
                        dstPx[0] = (*src)[index];
                        dstPx[1] = (*src)[index];
                        dstPx[2] = (*src)[index];
                        dstPx[3] = 0xff;
                    });
                }
                case PSBPixelFormat::A8L8:
                case PSBPixelFormat::A8L8_SW:
                case PSBPixelFormat::TileA8L8_SW: {
                    if(src->size() < pixelCount * 2) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 2;
                        const uint8_t l = (*src)[index + 0];
                        const uint8_t a = (*src)[index + 1];
                        dstPx[0] = l;
                        dstPx[1] = l;
                        dstPx[2] = l;
                        dstPx[3] = a;
                    });
                }
                case PSBPixelFormat::LeRGBA4444:
                case PSBPixelFormat::BeRGBA4444:
                case PSBPixelFormat::LeRGBA4444_SW:
                case PSBPixelFormat::TileLeRGBA4444_SW: {
                    if(src->size() < pixelCount * 2) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 2;
                        const uint16_t v = ReadLE16(&(*src)[index]);
                        const uint8_t r =
                            static_cast<uint8_t>(((v >> 12) & 0x0f) * 17);
                        const uint8_t g =
                            static_cast<uint8_t>(((v >> 8) & 0x0f) * 17);
                        const uint8_t b =
                            static_cast<uint8_t>(((v >> 4) & 0x0f) * 17);
                        const uint8_t a = static_cast<uint8_t>((v & 0x0f) * 17);
                        dstPx[0] = b;
                        dstPx[1] = g;
                        dstPx[2] = r;
                        dstPx[3] = a;
                    });
                }
                case PSBPixelFormat::RGBA5650:
                case PSBPixelFormat::RGBA5650_SW:
                case PSBPixelFormat::TileRGBA5650_SW: {
                    if(src->size() < pixelCount * 2) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 2;
                        const uint16_t v = ReadLE16(&(*src)[index]);
                        const uint8_t r =
                            static_cast<uint8_t>(((v >> 11) & 0x1f) * 255 / 31);
                        const uint8_t g =
                            static_cast<uint8_t>(((v >> 5) & 0x3f) * 255 / 63);
                        const uint8_t b =
                            static_cast<uint8_t>((v & 0x1f) * 255 / 31);
                        dstPx[0] = b;
                        dstPx[1] = g;
                        dstPx[2] = r;
                        dstPx[3] = 0xff;
                    });
                }
                case PSBPixelFormat::CI8:
                case PSBPixelFormat::CI8_SW:
                case PSBPixelFormat::CI8_SW_PSP:
                case PSBPixelFormat::TileCI8: {
                    if(src->size() < pixelCount || info.palette.empty()) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            static_cast<size_t>(y) * info.width + x;
                        writePalettePixel((*src)[index], dstPx);
                    });
                }
                case PSBPixelFormat::CI4:
                case PSBPixelFormat::CI4_SW:
                case PSBPixelFormat::CI4_SW_PSP:
                case PSBPixelFormat::TileCI4: {
                    if(src->size() * 2 < pixelCount || info.palette.empty()) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t pixelIndex =
                            static_cast<size_t>(y) * info.width + x;
                        const uint8_t packed = (*src)[pixelIndex / 2];
                        const uint8_t palIndex = (pixelIndex & 1) == 0
                            ? static_cast<uint8_t>((packed >> 4) & 0x0f)
                            : static_cast<uint8_t>(packed & 0x0f);
                        writePalettePixel(palIndex, dstPx);
                    });
                }
                default:
                    return nullptr;
            }
        }

        size_t ClampSizeT(size_t value, size_t min_value, size_t max_value) {
            return std::max(min_value, std::min(value, max_value));
        }

        float GetPSBFloat(const std::shared_ptr<IPSBValue> &value,
                          float fallback = 0.0f) {
            if(!value)
                return fallback;
            if(const auto num = std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float)
                    return num->getFloatValue();
                if(num->numberType == PSBNumberType::Double)
                    return static_cast<float>(
                        BitConverter::fromByteArray<double>(num->data));
                return static_cast<float>(num->getLongValue());
            }
            return fallback;
        }

        // 按 PSB 帧的 content["mask"] 把“作者标记本帧不存在”的字段复位回默认值。
        //
        // 为什么是“事后复位”而不是“按位读”：两个帧解析点（平坦轨道 / 节点帧）已经
        // 把键全读进来了，逐处改成按位读容易出现两套不同口径的解析；事后统一复位
        // 只有一处口径，且只动本地 frame 结构（不去改共享的 PSB 字典）。语义与参考
        // 实现完全等价：mask 位为 0 时该键等同于不存在。
        // 位表来自 AetherKiri `PlayerInternal.h:1921-2035`（对应 libkrkr2 sub_692AB0）：
        //   0x1 ox/oy、0x2 coord[x,y,z]、0x4 fx、0x8 fy、0x10 angle、
        //   0x20 'z'、0x60 zx/zy、0x80 sx、0x100 sy、0x400 opa/op、0x20000 bm。
        //
        // 位号已集中到 EmoteMaskBits.h（ContentMask），并与 FreeMote 的位表逐位
        // 对账过（见该文件头部的许可证说明：**只参考、未复制代码**）。
        // countStats=true 时把"真实数据里出现了哪些位 / 位与键是否自洽"累加到
        // g_emoteMaskStats；只有节点树口径（CollectMotionNodeFrames）会打开它，
        // 避免平坦轨道口径把同一批帧数两遍。
        void ApplyFrameContentMask(
            const std::shared_ptr<PSBDictionary> &content,
            PSBMedia::PSBMotionFrame &f, bool countStats = false) {
            if(!content)
                return;
            const int mask =
                static_cast<int>(GetPSBFloat((*content)["mask"], 0));
            f.mask = mask;
            if((mask & Emote::ContentMask::Origin) == 0) {
                f.ox = Emote::Defaults::Origin;
                f.oy = Emote::Defaults::Origin;
            }
            if((mask & Emote::ContentMask::Coord) == 0) {
                f.cx = Emote::Defaults::Coord;
                f.cy = Emote::Defaults::Coord;
                f.z = Emote::Defaults::Coord;
                f.scaleFromCoordZ = false;
            }
            if((mask & (Emote::ContentMask::ZoomX |
                        Emote::ContentMask::ZoomY)) == 0) {
                f.scaleX = Emote::Defaults::Zoom;
                f.scaleY = Emote::Defaults::Zoom;
                f.scaleFromCoordZ = false;
            }
            if((mask & Emote::ContentMask::Angle) == 0)
                f.angle = Emote::Defaults::Angle;
            if((mask & Emote::ContentMask::SlantX) == 0)
                f.slantX = Emote::Defaults::Slant;
            if((mask & Emote::ContentMask::SlantY) == 0)
                f.slantY = Emote::Defaults::Slant;
            if((mask & Emote::ContentMask::FlipX) == 0)
                f.flipX = false;
            if((mask & Emote::ContentMask::FlipY) == 0)
                f.flipY = false;
            if((mask & Emote::ContentMask::Opacity) == 0)
                f.opacity = Emote::Defaults::Opacity;
            if((mask & Emote::ContentMask::BlendMode) == 0) {
                // 参考实现按 value_or(0) 取 0；FreeMote 的帧内容默认表写的是 16。
                // 两者不一致，故做成开关：默认保持本轮之前的行为（0）。
                if constexpr(Emote::Switches::kBlendModeDefault16)
                    f.blendMode = Emote::Defaults::BlendMode;
                else
                    f.blendMode = 0;
            }

            // ---- 本轮新增：只统计 / 只告警，默认不改变任何渲染行为 ----
            if(countStats) {
                ++g_emoteMaskStats.frames;
                if((*content).find("mask") != (*content).end())
                    ++g_emoteMaskStats.framesWithMask;
                struct MaskProbe {
                    int index;
                    const char *key;
                    const char *altKey;
                };
                // 位 → 该位管辖的 content 键。用于两个方向的**自洽性检查**：
                //   位=1 但键不存在 ⇒ 位表或解析口径有误；
                //   位=0 但键存在   ⇒ 复位口径在真实数据里确实会被触发（本轮最重要
                //                     的一条数据：它决定 color/曲线这些位值不值得实现）。
                static const MaskProbe kProbes[] = {
                    {0, "ox", "oy"},     {1, "coord", nullptr},
                    {2, "fx", nullptr},   {3, "fy", nullptr},
                    {4, "angle", nullptr},
                    {5, "zx", nullptr},   {6, "zy", nullptr},
                    {7, "sx", nullptr},   {8, "sy", nullptr},
                    {9, "color", nullptr},
                    {10, "opa", "op"},
                    {11, "ccc", nullptr}, {12, "acc", nullptr},
                    {13, "zcc", nullptr}, {14, "scc", nullptr},
                    {15, "occ", nullptr},
                    {17, "bm", nullptr},
                    {19, "motion", nullptr},
                    {20, "prt", nullptr},
                    {25, "mesh", nullptr},
                };
                for(int bit = 0; bit < 32; ++bit) {
                    if((mask & (1 << bit)) == 0)
                        continue;
                    ++g_emoteMaskStats.maskBitSeen[bit];
                    if(((1 << bit) & Emote::ContentMask::kImplemented) == 0)
                        ++g_emoteMaskStats.unhandledBitSeen[bit];
                }
                for(const auto &probe : kProbes) {
                    const bool hasKey =
                        (*content).find(probe.key) != (*content).end() ||
                        (probe.altKey != nullptr &&
                         (*content).find(probe.altKey) != (*content).end());
                    if((mask & (1 << probe.index)) != 0) {
                        if(!hasKey)
                            ++g_emoteMaskStats.bitSetKeyMissing[probe.index];
                    } else if(hasKey) {
                        ++g_emoteMaskStats.bitClearKeyPresent[probe.index];
                    }
                }
                if((mask & Emote::ContentMask::Color) != 0)
                    ++g_emoteMaskStats.colorBitSet;
                else
                    ++g_emoteMaskStats.colorBitClear;
            }

            // ---- A/B 开关（默认全关 = 行为与本轮之前逐字节一致）----
            // 一次只翻一个，翻完看真机日志/截图。
            if constexpr(Emote::Switches::kGateColorByMask) {
                if((mask & Emote::ContentMask::Color) == 0)
                    f.packedColors = {Emote::Defaults::PackedVertexColor,
                                      Emote::Defaults::PackedVertexColor,
                                      Emote::Defaults::PackedVertexColor,
                                      Emote::Defaults::PackedVertexColor};
            }
            if constexpr(Emote::Switches::kGateCurvesByMask) {
                if((mask & Emote::ContentMask::CoordCurve) == 0)
                    f.hasEasing = false;
                if((mask & Emote::ContentMask::AngleCurve) == 0)
                    f.hasAngleEasing = false;
                if((mask & Emote::ContentMask::ZoomCurve) == 0)
                    f.hasScaleEasing = false;
                if((mask & Emote::ContentMask::SlantCurve) == 0)
                    f.hasSlantEasing = false;
                if((mask & Emote::ContentMask::OpacityCurve) == 0)
                    f.hasOpacityEasing = false;
            }
        }

        int GetPSBInt(const std::shared_ptr<IPSBValue> &value,
                      int fallback = 0) {
            if(!value)
                return fallback;
            if(const auto num = std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float)
                    return static_cast<int>(num->getFloatValue());
                if(num->numberType == PSBNumberType::Double)
                    return static_cast<int>(
                        BitConverter::fromByteArray<double>(num->data));
                return static_cast<int>(num->getLongValue());
            }
            return fallback;
        }

        void DumpPSBValue(const std::shared_ptr<spdlog::logger> &logger,
                          const std::string &name,
                          const std::shared_ptr<IPSBValue> &value, int depth,
                          int maxDepth) {
            if(!logger || !value || depth > maxDepth)
                return;
            std::string indent(depth * 2, ' ');
            if(const auto subDict =
                   std::dynamic_pointer_cast<PSBDictionary>(value)) {
                logger->info("{}{}/ (dict, {} keys)", indent, name,
                             subDict->size());
                if(depth < maxDepth) {
                    for(const auto &[key, child] : *subDict) {
                        DumpPSBValue(logger, key, child, depth + 1, maxDepth);
                    }
                }
            } else if(const auto num =
                          std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float)
                    logger->info("{}{}= {} (float)", indent, name,
                                 num->getFloatValue());
                else
                    logger->info("{}{}= {} (int/long)", indent, name,
                                 num->getLongValue());
            } else if(const auto str =
                          std::dynamic_pointer_cast<PSBString>(value)) {
                logger->info("{}{}= \"{}\" (string)", indent, name, str->value);
            } else if(const auto list =
                          std::dynamic_pointer_cast<PSBList>(value)) {
                logger->info("{}{}= [list, {} items]", indent, name,
                             list->size());
                if(depth < maxDepth) {
                    for(int i = 0; i < static_cast<int>(list->size()) && i < 3;
                        i++) {
                        DumpPSBValue(logger, "[" + std::to_string(i) + "]",
                                     (*list)[i], depth + 1, maxDepth);
                    }
                }
            } else {
                logger->info("{}{}= <other>", indent, name);
            }
        }

        std::string MapSrcToResourcePath(const std::string &src) {
            // "src/title/bg" → "source/title/icon/bg"
            if(src.size() > 4 && src.substr(0, 4) == "src/") {
                std::string rest = src.substr(4); // "title/bg"
                auto slashPos = rest.find('/');
                if(slashPos != std::string::npos) {
                    std::string group = rest.substr(0, slashPos); // "title"
                    std::string name = rest.substr(slashPos + 1); // "bg"
                    return "source/" + group + "/icon/" + name;
                }
            }
            return src;
        }

        struct LayerFrameInfo {
            std::string src;
            float ox = 0, oy = 0, cx = 0, cy = 0;
        };

        bool ExtractFrameInfo(
            const std::shared_ptr<PSBDictionary> &layerDict,
            LayerFrameInfo &out,
            const std::shared_ptr<spdlog::logger> &logger = nullptr,
            const std::string &debugLabel = "") {
            auto frameList =
                std::dynamic_pointer_cast<PSBList>((*layerDict)["frameList"]);
            if(!frameList || frameList->size() == 0) {
                KRKR_LOG_ONCE(
                    "psb_extract_no_framelist_" + debugLabel,
                    if(logger) logger->info(
                        "  ExtractFrameInfo[{}]: no frameList", debugLabel));
                return false;
            }
            auto frame0 =
                std::dynamic_pointer_cast<PSBDictionary>((*frameList)[0]);
            if(!frame0) {
                KRKR_LOG_ONCE(
                    "psb_extract_f0_not_dict_" + debugLabel,
                    if(logger) logger->info(
                        "  ExtractFrameInfo[{}]: frame0 not dict", debugLabel));
                return false;
            }
            auto content =
                std::dynamic_pointer_cast<PSBDictionary>((*frame0)["content"]);
            if(!content) {
                KRKR_LOG_ONCE("psb_extract_no_content_" + debugLabel,
                              if(logger) {
                                  std::string keys;
                                  for(const auto &[k, v] : *frame0) {
                                      if(!keys.empty())
                                          keys += ", ";
                                      keys += k;
                                  }
                                  logger->info(
                                      "  ExtractFrameInfo[{}]: no content in "
                                      "frame0 (keys: {})",
                                      debugLabel, keys);
                              });
                return false;
            }
            auto srcStr =
                std::dynamic_pointer_cast<PSBString>((*content)["src"]);
            if(!srcStr || srcStr->value.empty()) {
                KRKR_LOG_ONCE(
                    "psb_extract_no_src_" + debugLabel,
                    if(logger) logger->info(
                        "  ExtractFrameInfo[{}]: no src string in content",
                        debugLabel));
                return false;
            }

            out.src = srcStr->value;
            out.ox = GetPSBFloat((*content)["ox"], 0);
            out.oy = GetPSBFloat((*content)["oy"], 0);
            auto coordList =
                std::dynamic_pointer_cast<PSBList>((*content)["coord"]);
            if(coordList && coordList->size() >= 2) {
                out.cx = GetPSBFloat((*coordList)[0], 0);
                out.cy = GetPSBFloat((*coordList)[1], 0);
            }
            return true;
        }

        void CollectLayersFromMotion(
            const std::shared_ptr<PSBDictionary> &motionDict,
            const std::string &motionName, const std::string &sceneName,
            float parentX, float parentY,
            const std::shared_ptr<PSBDictionary> &objectTree,
            std::vector<PSBMedia::LayerPosition> &positions,
            std::vector<PSBMedia::ButtonBoundInfo> *buttons,
            const std::shared_ptr<spdlog::logger> &logger, int depth = 0) {
            if(depth > 8)
                return;

            auto targetMotion = std::dynamic_pointer_cast<PSBDictionary>(
                (*motionDict)[motionName]);
            if(!targetMotion) {
                if(logger && depth > 0)
                    logger->info("CollectLayers: {}/{} not found (depth={})",
                                 sceneName, motionName, depth);
                return;
            }

            auto layerList =
                std::dynamic_pointer_cast<PSBList>((*targetMotion)["layer"]);
            if(!layerList) {
                if(logger && depth > 0)
                    logger->info(
                        "CollectLayers: {}/{} has no layer list (depth={})",
                        sceneName, motionName, depth);
                return;
            }

            if(logger) {
                logger->info("CollectLayers: {}/{} has {} layers (depth={})",
                             sceneName, motionName, layerList->size(), depth);
            }

            for(int i = 0; i < static_cast<int>(layerList->size()); i++) {
                auto layerDict =
                    std::dynamic_pointer_cast<PSBDictionary>((*layerList)[i]);
                if(!layerDict)
                    continue;

                auto labelVal =
                    std::dynamic_pointer_cast<PSBString>((*layerDict)["label"]);
                std::string label =
                    labelVal ? labelVal->value : ("layer_" + std::to_string(i));

                LayerFrameInfo fi;
                if(!ExtractFrameInfo(layerDict, fi, logger,
                                     sceneName + "/" + motionName + "/" +
                                         label))
                    continue;

                float finalX = parentX + fi.ox + fi.cx;
                float finalY = parentY + fi.oy + fi.cy;

                if(fi.src.substr(0, 7) == "motion/") {
                    std::string ref = fi.src.substr(7);
                    auto slash = ref.find('/');
                    if(slash != std::string::npos && objectTree) {
                        std::string objName = ref.substr(0, slash);
                        std::string subMotion = ref.substr(slash + 1);
                        auto objDict = std::dynamic_pointer_cast<PSBDictionary>(
                            (*objectTree)[objName]);
                        if(objDict) {
                            auto objMotionDict =
                                std::dynamic_pointer_cast<PSBDictionary>(
                                    (*objDict)["motion"]);
                            if(objMotionDict) {
                                if(logger) {
                                    logger->info("follow motion ref: {} → "
                                                 "{}/{} offset=({},{})",
                                                 fi.src, objName, subMotion,
                                                 finalX, finalY);
                                }
                                size_t posBefore = positions.size();
                                CollectLayersFromMotion(
                                    objMotionDict, subMotion, sceneName, finalX,
                                    finalY, objectTree, positions, buttons,
                                    logger, depth + 1);

                                if(buttons) {
                                    std::string newImageKey;
                                    if(positions.size() > posBefore) {
                                        newImageKey =
                                            positions[posBefore].srcPath;
                                    }
                                    bool found = false;
                                    for(auto &existing : *buttons) {
                                        if(existing.buttonName == objName &&
                                           existing.sceneName == sceneName) {
                                            if(existing.imageKey.empty() &&
                                               !newImageKey.empty()) {
                                                existing.imageKey = newImageKey;
                                            }
                                            found = true;
                                            break;
                                        }
                                    }
                                    if(!found) {
                                        PSBMedia::ButtonBoundInfo btn;
                                        btn.sceneName = sceneName;
                                        btn.buttonName = objName;
                                        btn.imageKey = newImageKey;
                                        btn.left = finalX;
                                        btn.top = finalY;
                                        buttons->push_back(std::move(btn));
                                    }
                                }
                            }
                        }
                    }
                } else if(fi.src.substr(0, 4) == "src/") {
                    std::string resourcePath = MapSrcToResourcePath(fi.src);
                    PSBMedia::LayerPosition pos;
                    pos.sceneName = sceneName;
                    pos.layerName = sceneName + "/" + label;
                    pos.srcPath = resourcePath;
                    pos.left = finalX;
                    pos.top = finalY;
                    pos.opacity = 255;
                    pos.visible = true;

                    if(logger) {
                        logger->info("layer: [{}] src={} → res={} "
                                     "final=({},{}) depth={}",
                                     pos.layerName, fi.src, resourcePath,
                                     finalX, finalY, depth);
                    }

                    positions.push_back(std::move(pos));
                } else {
                    if(logger) {
                        logger->info("  unhandled src type: [{}] src={} "
                                     "pos=({},{}) depth={}",
                                     label, fi.src, finalX, finalY, depth);
                    }
                }
            }
        }

        void CollectMotionTracksFromMotion(
            const std::shared_ptr<PSBDictionary> &motionDict,
            const std::string &motionName, const std::string &sceneName,
            std::vector<PSBMedia::PSBMotionLayerTrack> &tracks,
            const std::shared_ptr<spdlog::logger> &logger, int depth = 0) {
            if(depth > 8)
                return;
            auto targetMotion = std::dynamic_pointer_cast<PSBDictionary>(
                (*motionDict)[motionName]);
            if(!targetMotion)
                return;
            auto layerList =
                std::dynamic_pointer_cast<PSBList>((*targetMotion)["layer"]);
            if(!layerList)
                return;
            if(logger)
                logger->debug("CollectTracks: {}/{} {} layers", sceneName,
                              motionName, layerList->size());

            for(int i = 0; i < static_cast<int>(layerList->size()); i++) {
                auto layerDict =
                    std::dynamic_pointer_cast<PSBDictionary>((*layerList)[i]);
                if(!layerDict)
                    continue;
                auto labelVal =
                    std::dynamic_pointer_cast<PSBString>((*layerDict)["label"]);
                std::string label =
                    labelVal ? labelVal->value : ("layer_" + std::to_string(i));

                auto frameList = std::dynamic_pointer_cast<PSBList>(
                    (*layerDict)["frameList"]);
                if(!frameList || frameList->size() == 0)
                    continue;

                PSBMedia::PSBMotionLayerTrack track;
                track.label = sceneName + "/" + motionName + "/" + label;
                track.frames.reserve(frameList->size());
                for(int j = 0; j < static_cast<int>(frameList->size()); j++) {
                    auto frame = std::dynamic_pointer_cast<PSBDictionary>(
                        (*frameList)[j]);
                    if(!frame)
                        continue;
                    PSBMedia::PSBMotionFrame f;
                    f.time = static_cast<int>(GetPSBFloat((*frame)["time"], 0) *
                                              1000.0 / 60.0);
                    f.type = static_cast<int>(GetPSBFloat((*frame)["type"], 0));
                    auto content = std::dynamic_pointer_cast<PSBDictionary>(
                        (*frame)["content"]);
                    if(content && f.type != 0) {
                        auto srcVal = std::dynamic_pointer_cast<PSBString>(
                            (*content)["src"]);
                        if(srcVal)
                            f.src = srcVal->value;
                        f.ox = GetPSBFloat((*content)["ox"], 0);
                        f.oy = GetPSBFloat((*content)["oy"], 0);
                        auto coord = std::dynamic_pointer_cast<PSBList>(
                            (*content)["coord"]);
                        if(coord && coord->size() >= 2) {
                            f.cx = GetPSBFloat((*coord)[0], 0);
                            f.cy = GetPSBFloat((*coord)[1], 0);
                            // coord[2] 是 **Z（深度）**，不是缩放兑底 —— 见 z 字段声明处。
                            if(coord->size() >= 3)
                                f.z = GetPSBFloat((*coord)[2], 0.0f);
                        }
                        {
                            const float zx =
                                GetPSBFloat((*content)["zx"], 0.0f);
                            const float zy =
                                GetPSBFloat((*content)["zy"], 0.0f);
                            if(zx != 0.0f)
                                f.scaleX = zx;
                            if(zy != 0.0f)
                                f.scaleY = zy;
                        }
                        f.blendMode =
                            static_cast<int>(GetPSBFloat((*content)["bm"], 0));
                        f.angle = GetPSBFloat((*content)["angle"], 0.0f);
                        f.slantX = GetPSBFloat((*content)["sx"], 0.0f);
                        f.slantY = GetPSBFloat((*content)["sy"], 0.0f);
                        if(auto clipList = std::dynamic_pointer_cast<PSBList>(
                               (*content)["clip"])) {
                            if(clipList->size() >= 4) {
                                f.hasClip = true;
                                f.clipL = static_cast<int>(
                                    GetPSBFloat((*clipList)[0], 0));
                                f.clipT = static_cast<int>(
                                    GetPSBFloat((*clipList)[1], 0));
                                f.clipR = static_cast<int>(
                                    GetPSBFloat((*clipList)[2], 0));
                                f.clipB = static_cast<int>(
                                    GetPSBFloat((*clipList)[3], 0));
                            }
                        }
                        f.flipX = GetPSBFloat((*content)["fx"], 0.0f) != 0.0f;
                        f.flipY = GetPSBFloat((*content)["fy"], 0.0f) != 0.0f;
                        f.opacity = GetPSBFloat((*content)["opa"], -1.0f);
                        if(f.opacity < 0.0f)
                            f.opacity = GetPSBFloat((*content)["op"], 255.0f);
                    } else {
                        f.visible = false;
                    }
                    ApplyFrameContentMask(content, f);
                    track.frames.push_back(std::move(f));
                }
                if(track.frames.empty())
                    continue;
                std::stable_sort(track.frames.begin(), track.frames.end(),
                                 [](const PSBMedia::PSBMotionFrame &a,
                                    const PSBMedia::PSBMotionFrame &b) {
                                     return a.time < b.time;
                                 });
                tracks.push_back(std::move(track));
            }
        }

        void
        CollectAllMotionTracks(PSBMedia &media, const std::string &archiveKey,
                               const std::shared_ptr<PSBDictionary> &objectTree,
                               const std::shared_ptr<spdlog::logger> &logger) {
            if(!objectTree)
                return;
            for(const auto &[sceneName, sceneVal] : *objectTree) {
                auto sceneDict =
                    std::dynamic_pointer_cast<PSBDictionary>(sceneVal);
                if(!sceneDict)
                    continue;
                auto motionDict = std::dynamic_pointer_cast<PSBDictionary>(
                    (*sceneDict)["motion"]);
                if(!motionDict)
                    continue;
                for(const auto &[motionName, motionVal] : *motionDict) {
                    auto motionDictObj =
                        std::dynamic_pointer_cast<PSBDictionary>(motionVal);
                    if(!motionDictObj)
                        continue;
                    const tjs_int loopTime = static_cast<tjs_int>(
                        GetPSBFloat((*motionDictObj)["loopTime"], 0) * 1000.0 /
                        60.0);
                    if(loopTime > 0) {
                        media.setMotionLoopTime(archiveKey, sceneName,
                                                motionName, loopTime);
                        if(logger)
                            logger->info("motion {}/{} loopTime={}", sceneName,
                                         motionName, loopTime);
                    }
                    std::vector<PSBMedia::PSBMotionLayerTrack> tracks;
                    CollectMotionTracksFromMotion(motionDict, motionName,
                                                  sceneName, tracks, logger);
                    size_t trackCount = tracks.size();
                    if(trackCount > 0) {
                        media.addMotionTracks(archiveKey, sceneName, motionName,
                                              std::move(tracks));
                        if(logger)
                            logger->info("Stored {} tracks for {}/{}",
                                         trackCount, sceneName, motionName);
                    }
                }
            }
        }

        void
        CollectMotionNodeFrames(const std::shared_ptr<PSBDictionary> &layerDict,
                                PSBMedia::PSBMotionNode &node) {
            auto frameList =
                std::dynamic_pointer_cast<PSBList>((*layerDict)["frameList"]);
            if(!frameList)
                return;
            node.frames.reserve(frameList->size());
            for(int j = 0; j < static_cast<int>(frameList->size()); j++) {
                auto frame =
                    std::dynamic_pointer_cast<PSBDictionary>((*frameList)[j]);
                if(!frame)
                    continue;
                PSBMedia::PSBMotionFrame f;
                f.time = static_cast<int>(GetPSBFloat((*frame)["time"], 0) *
                                          1000.0 / 60.0);
                f.type = static_cast<int>(GetPSBFloat((*frame)["type"], 0));
                auto content = std::dynamic_pointer_cast<PSBDictionary>(
                    (*frame)["content"]);
                if(content && f.type != 0) {
                    auto srcVal =
                        std::dynamic_pointer_cast<PSBString>((*content)["src"]);
                    if(srcVal)
                        f.src = srcVal->value;
                    f.ox = GetPSBFloat((*content)["ox"], 0);
                    f.oy = GetPSBFloat((*content)["oy"], 0);
                    auto coord =
                        std::dynamic_pointer_cast<PSBList>((*content)["coord"]);
                    if(coord && coord->size() >= 2) {
                        f.cx = GetPSBFloat((*coord)[0], 0);
                        f.cy = GetPSBFloat((*coord)[1], 0);
                        // coord[2] 是 **Z（深度）**，不是缩放兑底 —— 与 flat
                        // 路径（CollectMotionTracksFromMotion）及参考
                        // PlayerInternal.h:1929-1931 一致。
                        // 【节点树路径（立绘走这条）此前从不读 z】⇒ 累积 posZ
                        // 恒为 0，`kEmoteZSort` 的稳定排序成了空操作，画面与旧行为
                        // 逐像素相同。
                        if(coord->size() >= 3)
                            f.z = GetPSBFloat((*coord)[2], 0.0f);
                    }
                    {
                        const float zx = GetPSBFloat((*content)["zx"], 0.0f);
                        const float zy = GetPSBFloat((*content)["zy"], 0.0f);
                        if(zx != 0.0f)
                            f.scaleX = zx;
                        if(zy != 0.0f)
                            f.scaleY = zy;
                    }
                    f.blendMode =
                        static_cast<int>(GetPSBFloat((*content)["bm"], 0));
                    f.angle = GetPSBFloat((*content)["angle"], 0.0f);
                    f.slantX = GetPSBFloat((*content)["sx"], 0.0f);
                    f.slantY = GetPSBFloat((*content)["sy"], 0.0f);
                    if(auto clipList = std::dynamic_pointer_cast<PSBList>(
                           (*content)["clip"])) {
                        if(clipList->size() >= 4) {
                            f.hasClip = true;
                            f.clipL = static_cast<int>(
                                GetPSBFloat((*clipList)[0], 0));
                            f.clipT = static_cast<int>(
                                GetPSBFloat((*clipList)[1], 0));
                            f.clipR = static_cast<int>(
                                GetPSBFloat((*clipList)[2], 0));
                            f.clipB = static_cast<int>(
                                GetPSBFloat((*clipList)[3], 0));
                        }
                    }
                    f.flipX = GetPSBFloat((*content)["fx"], 0.0f) != 0.0f;
                    f.flipY = GetPSBFloat((*content)["fy"], 0.0f) != 0.0f;
                    f.opacity = GetPSBFloat((*content)["opa"], -1.0f);
                    if(f.opacity < 0.0f)
                        f.opacity = GetPSBFloat((*content)["op"], 255.0f);
                    if(auto cccObj = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["ccc"]))
                        if(auto xl = std::dynamic_pointer_cast<PSBList>(
                               (*cccObj)["x"]),
                           yl = std::dynamic_pointer_cast<PSBList>(
                               (*cccObj)["y"]);
                           xl && yl && xl->size() >= 3 && yl->size() >= 3) {
                            f.hasEasing = true;
                            f.easeX1 = GetPSBFloat((*xl)[1], 0.0f);
                            f.easeY1 = GetPSBFloat((*yl)[1], 0.0f);
                            f.easeX2 = GetPSBFloat((*xl)[2], 1.0f);
                            f.easeY2 = GetPSBFloat((*yl)[2], 1.0f);
                        }
                    // "acc": angle curve.
                    if(auto accObj = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["acc"]))
                        if(auto xl = std::dynamic_pointer_cast<PSBList>(
                               (*accObj)["x"]),
                           yl = std::dynamic_pointer_cast<PSBList>(
                               (*accObj)["y"]);
                           xl && yl && xl->size() >= 3 && yl->size() >= 3) {
                            f.hasAngleEasing = true;
                            f.acX1 = GetPSBFloat((*xl)[1], 0.0f);
                            f.acY1 = GetPSBFloat((*yl)[1], 0.0f);
                            f.acX2 = GetPSBFloat((*xl)[2], 1.0f);
                            f.acY2 = GetPSBFloat((*yl)[2], 1.0f);
                        }
                    // "zcc": scale curve.
                    if(auto zccObj = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["zcc"]))
                        if(auto xl = std::dynamic_pointer_cast<PSBList>(
                               (*zccObj)["x"]),
                           yl = std::dynamic_pointer_cast<PSBList>(
                               (*zccObj)["y"]);
                           xl && yl && xl->size() >= 3 && yl->size() >= 3) {
                            f.hasScaleEasing = true;
                            f.zcX1 = GetPSBFloat((*xl)[1], 0.0f);
                            f.zcY1 = GetPSBFloat((*yl)[1], 0.0f);
                            f.zcX2 = GetPSBFloat((*xl)[2], 1.0f);
                            f.zcY2 = GetPSBFloat((*yl)[2], 1.0f);
                        }
                    // "occ": opacity curve.
                    if(auto occObj = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["occ"]))
                        if(auto xl = std::dynamic_pointer_cast<PSBList>(
                               (*occObj)["x"]),
                           yl = std::dynamic_pointer_cast<PSBList>(
                               (*occObj)["y"]);
                           xl && yl && xl->size() >= 3 && yl->size() >= 3) {
                            f.hasOpacityEasing = true;
                            f.ocX1 = GetPSBFloat((*xl)[1], 0.0f);
                            f.ocY1 = GetPSBFloat((*yl)[1], 0.0f);
                            f.ocX2 = GetPSBFloat((*xl)[2], 1.0f);
                            f.ocY2 = GetPSBFloat((*yl)[2], 1.0f);
                        }
                    // "scc": slant curve.
                    if(auto sccObj = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["scc"]))
                        if(auto xl = std::dynamic_pointer_cast<PSBList>(
                               (*sccObj)["x"]),
                           yl = std::dynamic_pointer_cast<PSBList>(
                               (*sccObj)["y"]);
                           xl && yl && xl->size() >= 3 && yl->size() >= 3) {
                            f.hasSlantEasing = true;
                            f.sccX1 = GetPSBFloat((*xl)[1], 0.0f);
                            f.sccY1 = GetPSBFloat((*yl)[1], 0.0f);
                            f.sccX2 = GetPSBFloat((*xl)[2], 1.0f);
                            f.sccY2 = GetPSBFloat((*yl)[2], 1.0f);
                        }
                    if(auto colorDict =
                           std::dynamic_pointer_cast<PSBDictionary>(
                               (*content)["color"])) {
                        for(int ci = 0; ci < 4; ci++) {
                            const std::string key = std::to_string(ci);
                            const auto val =
                                std::dynamic_pointer_cast<PSBNumber>(
                                    (*colorDict)[key]);
                            if(val) {
                                f.packedColors[ci] = static_cast<std::uint32_t>(
                                    GetPSBInt(val, 0xFFFFFFFF));
                            }
                        }
                    } else if(auto colorVal =
                                  std::dynamic_pointer_cast<PSBNumber>(
                                      (*content)["color"])) {
                        const std::uint32_t packed = static_cast<std::uint32_t>(
                            GetPSBInt(colorVal, 0xFFFFFFFF));
                        f.packedColors = { packed, packed, packed, packed };
                    } else if(auto colorList =
                                  std::dynamic_pointer_cast<PSBList>(
                                      (*content)["color"])) {
                        for(int ci = 0;
                            ci < 4 && ci < static_cast<int>(colorList->size());
                            ci++) {
                            const auto val =
                                std::dynamic_pointer_cast<PSBNumber>(
                                    (*colorList)[ci]);
                            if(val) {
                                f.packedColors[ci] = static_cast<std::uint32_t>(
                                    GetPSBInt(val, 0xFFFFFFFF));
                            }
                        }
                    }

                    if(auto meshObj = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["mesh"])) {
                        std::shared_ptr<PSBList> bp =
                            std::dynamic_pointer_cast<PSBList>(
                                (*meshObj)["bp"]);
                        if(!bp)
                            bp = std::dynamic_pointer_cast<PSBList>(
                                (*meshObj)["b"]);
                        if(bp && bp->size() == 32) {
                            f.meshControlPoints.reserve(32);
                            for(int mi = 0; mi < 32; mi++) {
                                f.meshControlPoints.push_back(
                                    GetPSBFloat((*bp)[mi], 0.0f));
                            }
                        }
                    }

                    if(auto cpDict = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["cp"])) {
                        auto cpxList =
                            std::dynamic_pointer_cast<PSBList>((*cpDict)["x"]);
                        auto cpyList =
                            std::dynamic_pointer_cast<PSBList>((*cpDict)["y"]);
                        auto cptList =
                            std::dynamic_pointer_cast<PSBList>((*cpDict)["t"]);
                        auto cpsList =
                            std::dynamic_pointer_cast<PSBList>((*cpDict)["s"]);
                        if(cpxList && cpyList && cptList) {
                            for(size_t ci = 0; ci < cpxList->size(); ++ci)
                                f.cp.x.push_back(static_cast<double>(
                                    GetPSBFloat((*cpxList)[ci], 0)));
                            for(size_t ci = 0; ci < cpyList->size(); ++ci)
                                f.cp.y.push_back(static_cast<double>(
                                    GetPSBFloat((*cpyList)[ci], 0)));
                            for(size_t ci = 0; ci < cptList->size(); ++ci)
                                f.cp.t.push_back(static_cast<double>(
                                    GetPSBFloat((*cptList)[ci], 0)));
                            if(cpsList) {
                                for(size_t ci = 0; ci < cpsList->size(); ++ci) {
                                    PSB::PSBMotionCpSeg seg;
                                    if(auto segDict = std::dynamic_pointer_cast<
                                           PSBDictionary>((*cpsList)[ci])) {
                                        auto sx =
                                            std::dynamic_pointer_cast<PSBList>(
                                                (*segDict)["x"]);
                                        auto sy =
                                            std::dynamic_pointer_cast<PSBList>(
                                                (*segDict)["y"]);
                                        auto sp =
                                            std::dynamic_pointer_cast<PSBList>(
                                                (*segDict)["p"]);
                                        if(sx)
                                            for(size_t si = 0; si < sx->size();
                                                ++si)
                                                seg.x.push_back(
                                                    static_cast<double>(
                                                        GetPSBFloat((*sx)[si],
                                                                    0)));
                                        if(sy)
                                            for(size_t si = 0; si < sy->size();
                                                ++si)
                                                seg.y.push_back(
                                                    static_cast<double>(
                                                        GetPSBFloat((*sy)[si],
                                                                    0)));
                                        if(sp)
                                            for(size_t si = 0; si < sp->size();
                                                ++si)
                                                seg.p.push_back(
                                                    static_cast<double>(
                                                        GetPSBFloat((*sp)[si],
                                                                    0)));
                                    }
                                    f.cp.s.push_back(std::move(seg));
                                }
                            }
                        }
                    }

                    if(auto md = std::dynamic_pointer_cast<PSBDictionary>(
                           (*content)["motion"])) {
                        const int mm =
                            static_cast<int>(GetPSBFloat((*md)["mask"], 0));
                        if(mm & 0x2)
                            f.motionDt =
                                static_cast<int>(GetPSBFloat((*md)["dt"], 0));
                        if(mm & 0x8)
                            f.motionDofst = GetPSBFloat((*md)["dofst"], 0.0f);
                        if(mm & 0x10)
                            if(auto dtgt = std::dynamic_pointer_cast<PSBString>(
                                   (*md)["dtgt"]))
                                f.motionDtgt = dtgt->value;
                    }
                } else {
                    f.visible = false;
                }
                ApplyFrameContentMask(content, f, true);
                node.frames.push_back(std::move(f));
            }
            std::stable_sort(node.frames.begin(), node.frames.end(),
                             [](const PSBMedia::PSBMotionFrame &a,
                                const PSBMedia::PSBMotionFrame &b) {
                                 return a.time < b.time;
                             });
        }

        void CollectMotionNodesFromLayerList(
            const std::shared_ptr<PSBList> &layerList, int parentIndex,
            std::vector<PSBMedia::PSBMotionNode> &nodes,
            const std::shared_ptr<spdlog::logger> &logger) {
            if(!layerList)
                return;
            for(int i = 0; i < static_cast<int>(layerList->size()); i++) {
                auto layerDict =
                    std::dynamic_pointer_cast<PSBDictionary>((*layerList)[i]);
                if(!layerDict)
                    continue;
                PSBMedia::PSBMotionNode node;
                auto labelVal =
                    std::dynamic_pointer_cast<PSBString>((*layerDict)["label"]);
                node.label =
                    labelVal ? labelVal->value : ("layer_" + std::to_string(i));
                node.parentIndex = parentIndex;
                node.type =
                    static_cast<int>(GetPSBFloat((*layerDict)["type"], 0));
                node.inheritMask = static_cast<int>(GetPSBFloat(
                    (*layerDict)["inheritMask"], Emote::InheritMask::kDefault));
                // 继承位直方图：位表已补全到 11 项（EmoteMaskBits.h::InheritMask），
                // 默认值 kDefault = 0x1FC = bits 2..8 正好是全部仿射项，可作交叉验证。
                for(int ibit = 0; ibit < 32; ++ibit)
                    if((node.inheritMask & (1 << ibit)) != 0)
                        ++g_emoteMaskStats.inheritMaskSeen[ibit];
                if(auto toList = std::dynamic_pointer_cast<PSBList>(
                       (*layerDict)["transformOrder"])) {
                    for(int k = 0;
                        k < 4 && k < static_cast<int>(toList->size()); k++) {
                        node.transformOrder[k] =
                            static_cast<int>(GetPSBFloat((*toList)[k], k));
                    }
                }
                node.width =
                    static_cast<int>(GetPSBFloat((*layerDict)["width"], 0));
                node.height =
                    static_cast<int>(GetPSBFloat((*layerDict)["height"], 0));
                node.meshType = static_cast<int>(
                    GetPSBFloat((*layerDict)["meshTransform"], 0));
                node.meshSyncChildMask = static_cast<int>(
                    GetPSBFloat((*layerDict)["meshSyncChildMask"], 0));
                node.meshDivision = static_cast<int>(
                    GetPSBFloat((*layerDict)["meshDivision"], 0));
                const int stencilTypeValue = static_cast<int>(
                    GetPSBFloat((*layerDict)["stencilType"], 0));
                node.stencilType = stencilTypeValue;
                // 注意：stencilType 在 FreeMote 眼里是**枚举**（PSB 侧比 MMO 侧多一个
                // 常量偏移 4：PSB 5 ↔ MMO 1），而不是位标志。若真实数据里所有
                // type-12 层都带 bit2，那么 `(stencilType & 4)` 这个判据等于恒真、
                // 不具区分度；直方图会直接把这件事暴露出来（见 emote layer stats）。
                // 本轮不改判据，只统计。
                if(node.type == Emote::Stencil::kLayerType &&
                   stencilTypeValue >= 0 && stencilTypeValue < 64)
                    ++g_emoteMaskStats.stencilTypeValue[stencilTypeValue];
                node.hasStencil =
                    (node.type == Emote::Stencil::kLayerType &&
                     (stencilTypeValue & Emote::Stencil::kPsbValueOffset) != 0);
                if(auto maskList = std::dynamic_pointer_cast<PSBList>(
                       (*layerDict)["stencilCompositeMaskLayerList"])) {
                    for(auto &item : *maskList) {
                        if(auto label =
                               std::dynamic_pointer_cast<PSBString>(item)) {
                            if(!label->value.empty())
                                node.stencilMaskLabels.push_back(label->value);
                        }
                    }
                }
                // "是否带作者声明的蒙版层列表"比 stencilType 的位判据更有区分度：
                // 数量对比进 emote layer stats，用来决定合成组判据要不要改。
                if(node.type == Emote::Stencil::kLayerType) {
                    if(node.stencilMaskLabels.empty())
                        ++g_emoteMaskStats.stencilWithoutLabels;
                    else
                        ++g_emoteMaskStats.stencilWithLabels;
                }

                node.groundCorrection =
                    GetPSBFloat((*layerDict)["groundCorrection"], 0.0f) != 0.0f;
                node.parameterizeIndex = static_cast<int>(
                    GetPSBFloat((*layerDict)["parameterize"], -1));
                CollectMotionNodeFrames(layerDict, node);
                if(logger)
                    logger->info(
                        "  node[{}] '{}' parent={} type={} stencil={}/{} "
                        "box={}x{} "
                        "inh=0x{:x} to={},{},{},{} frames={} firstsrc='{}' "
                        "f0scale={}/{} f0zscale={} f0mask=0x{:x}",
                        static_cast<int>(nodes.size()), node.label, parentIndex,
                        node.type, node.stencilType,
                        node.hasStencil ? 1 : 0, node.width, node.height,
                        node.inheritMask,
                        node.transformOrder[0], node.transformOrder[1],
                        node.transformOrder[2], node.transformOrder[3],
                        static_cast<int>(node.frames.size()),
                        node.frames.empty() ? std::string("")
                                            : node.frames.front().src,
                        node.frames.empty() ? 0.0f
                                            : node.frames.front().scaleX,
                        node.frames.empty() ? 0.0f
                                            : node.frames.front().scaleY,
                        node.frames.empty()
                            ? 0
                            : (node.frames.front().scaleFromCoordZ ? 1 : 0),
                        node.frames.empty() ? 0 : node.frames.front().mask);
                const int myIndex = static_cast<int>(nodes.size());
                nodes.push_back(std::move(node));
                auto children = std::dynamic_pointer_cast<PSBList>(
                    (*layerDict)["children"]);
                if(children && children->size() > 0) {
                    CollectMotionNodesFromLayerList(children, myIndex, nodes,
                                                    logger);
                }
            }
        }

        void CollectAllMotionNodeTrees(
            PSBMedia &media, const std::string &archiveKey,
            const std::shared_ptr<PSBDictionary> &objectTree,
            const std::shared_ptr<spdlog::logger> &logger) {
            if(!objectTree)
                return;
            g_emoteMaskStats.reset();
            // sub-motion 清点（HANDOFF-EMOTE §5）：`motion/general_obj_a` 这类
            // sub-motion 的 `layer: []` 是**数据本身为空**还是**解析漏了 key**？
            // 这里直接从原始 PSB 计数，并区分"带 parameter（=参数轴时间轴，空 layer
            // 属正常）"与"真的什么都没有"。
            size_t subMotionTotal = 0;
            size_t subMotionEmpty = 0;
            size_t subMotionEmptyWithParam = 0;
            std::string subMotionEmptyNames;
            for(const auto &[sceneName, sceneVal] : *objectTree) {
                auto sceneDict =
                    std::dynamic_pointer_cast<PSBDictionary>(sceneVal);
                if(!sceneDict)
                    continue;
                auto motionDict = std::dynamic_pointer_cast<PSBDictionary>(
                    (*sceneDict)["motion"]);
                if(!motionDict)
                    continue;
                for(const auto &[motionName, motionVal] : *motionDict) {
                    auto targetMotion =
                        std::dynamic_pointer_cast<PSBDictionary>(motionVal);
                    if(!targetMotion)
                        continue;
                    std::vector<PSBMotionParameter> parameters;
                    if(auto paramList = std::dynamic_pointer_cast<PSBList>(
                           (*targetMotion)["parameter"])) {
                        for(auto &paramItem : *paramList) {
                            auto pd = std::dynamic_pointer_cast<PSBDictionary>(
                                paramItem);
                            if(!pd)
                                continue;
                            PSBMotionParameter info;
                            if(auto id = std::dynamic_pointer_cast<PSBString>(
                                   (*pd)["id"]))
                                info.id = id->value;
                            else if(auto label =
                                        std::dynamic_pointer_cast<PSBString>(
                                            (*pd)["label"]))
                                info.id = label->value;
                            info.discretization =
                                GetPSBFloat((*pd)["discretization"], 0) != 0.0f;
                            info.rangeBegin = static_cast<double>(
                                GetPSBFloat((*pd)["rangeBegin"], 0));
                            info.rangeEnd = static_cast<double>(
                                GetPSBFloat((*pd)["rangeEnd"], 0));
                            const double range =
                                info.rangeEnd - info.rangeBegin;
                            info.division = static_cast<double>(GetPSBFloat(
                                (*pd)["division"],
                                static_cast<float>(range > 0.0 ? range : 1.0)));
                            if(!info.id.empty())
                                parameters.push_back(std::move(info));
                        }
                    }
                    if(auto pz = std::dynamic_pointer_cast<PSBDictionary>(
                           (*targetMotion)["parameterize"])) {
                        if(parameters.empty()) {
                            PSBMotionParameter info;
                            if(auto id = std::dynamic_pointer_cast<PSBString>(
                                   (*pz)["id"]))
                                info.id = id->value;
                            info.discretization =
                                GetPSBFloat((*pz)["discretization"], 0) != 0.0f;
                            info.rangeBegin = static_cast<double>(
                                GetPSBFloat((*pz)["rangeBegin"], 0));
                            info.rangeEnd = static_cast<double>(
                                GetPSBFloat((*pz)["rangeEnd"], 0));
                            const double range =
                                info.rangeEnd - info.rangeBegin;
                            info.division = static_cast<double>(GetPSBFloat(
                                (*pz)["division"],
                                static_cast<float>(range > 0.0 ? range : 1.0)));
                            if(!info.id.empty())
                                parameters.push_back(std::move(info));
                        }
                    }
                    auto layerList = std::dynamic_pointer_cast<PSBList>(
                        (*targetMotion)["layer"]);
                    ++subMotionTotal;
                    if(!layerList || layerList->size() == 0) {
                        ++subMotionEmpty;
                        if(!parameters.empty())
                            ++subMotionEmptyWithParam;
                        if(subMotionEmptyNames.size() < 240) {
                            if(!subMotionEmptyNames.empty())
                                subMotionEmptyNames += ",";
                            subMotionEmptyNames += sceneName + "/" + motionName;
                        }
                    }
                    if(!layerList) {
                        if(!parameters.empty())
                            media.setMotionParameters(archiveKey, sceneName,
                                                      motionName,
                                                      std::move(parameters));
                        continue;
                    }
                    std::vector<PSBMedia::PSBMotionNode> nodes;
                    CollectMotionNodesFromLayerList(layerList, -1, nodes,
                                                    logger);
                    if(!nodes.empty()) {
                        std::unordered_map<std::string, int> nodeIndexByLabel;
                        for(size_t ki = 0; ki < nodes.size(); ++ki)
                            if(!nodes[ki].label.empty())
                                nodeIndexByLabel[nodes[ki].label] =
                                    static_cast<int>(ki);
                        for(auto &nd : nodes) {
                            if(!nd.hasStencil || nd.stencilMaskLabels.empty())
                                continue;
                            for(const auto &mLabel : nd.stencilMaskLabels) {
                                auto found = nodeIndexByLabel.find(mLabel);
                                if(found != nodeIndexByLabel.end())
                                    nd.stencilMaskNodeIndices.push_back(
                                        found->second);
                                else if(logger)
                                    logger->warn("stencil mask label '{}' not "
                                                 "found for '{}'",
                                                 mLabel, nd.label);
                            }
                        }
                        // 合成组清单探针：把每个 type-12 可折叠组的“标签 → 蒙版层
                        // 标签/解析到的节点序号”打出来。立绘错位/穿帮时必须先分清是
                        // ① 蒙版没解析到（组不可折叠，整块按未裁切直画）还是 ② 折了但
                        // 蒙版乘错。只有这行能把这两种情况区分开。
                        if(logger) {
                            int gi = 0;
                            for(const auto &nd : nodes) {
                                if(!nd.hasStencil)
                                    continue;
                                std::string masks;
                                for(size_t mi = 0;
                                    mi < nd.stencilMaskLabels.size() && mi < 8;
                                    ++mi) {
                                    if(!masks.empty())
                                        masks += ",";
                                    masks += nd.stencilMaskLabels[mi];
                                }
                                logger->info(
                                    "stencil group[{}] '{}' type={} "
                                    "labels=[{}] resolved={}",
                                    gi++, nd.label, nd.stencilType, masks,
                                    static_cast<int>(
                                        nd.stencilMaskNodeIndices.size()));
                            }
                        }
                        const size_t storedNodeCount = nodes.size();
                        media.addMotionNodes(archiveKey, sceneName, motionName,
                                             std::move(nodes));
                        if(logger)
                            logger->info(
                                "Stored {} nodes for {}/{} (params={})",
                                storedNodeCount, sceneName, motionName,
                                static_cast<int>(parameters.size()));
                    }
                    if(!parameters.empty())
                        media.setMotionParameters(archiveKey, sceneName,
                                                  motionName,
                                                  std::move(parameters));
                }
            }

            // ===== 位域对照汇总（FreeMote 对照轮，每个 archive 一行）=====
            // frames/bits        ：真实数据里出现过哪些 content mask 位
            // resetTriggered     ：位=0 但键存在 ⇒ 我们的"复位口径"确实在生效
            // tableMismatch      ：位=1 但键不存在 ⇒ 位表/解析口径要复核
            // unhandled          ：出现了但我们尚未实现的位（决定下一步做什么）
            if(logger) {
                logger->info(
                    "emote mask stats [{}]: frames={} withMask={} bits={} "
                    "resetTriggered={} tableMismatch={} unhandled={}",
                    archiveKey, g_emoteMaskStats.frames,
                    g_emoteMaskStats.framesWithMask, g_emoteMaskStats.SeenBits(),
                    g_emoteMaskStats.ResetTriggeredBits(),
                    g_emoteMaskStats.TableMismatchBits(),
                    g_emoteMaskStats.UnhandledBits());
                logger->info(
                    "emote layer stats [{}]: inheritMask={} stencilType={} "
                    "colorKeyBit=[set {} / clear {}]",
                    archiveKey, g_emoteMaskStats.InheritBits(),
                    g_emoteMaskStats.StencilTypes(),
                    g_emoteMaskStats.colorBitSet,
                    g_emoteMaskStats.colorBitClear);
                logger->info(
                    "emote sub-motions [{}]: total={} emptyLayer={} "
                    "emptyWithParam={} names=[{}]",
                    archiveKey, subMotionTotal, subMotionEmpty,
                    subMotionEmptyWithParam, subMotionEmptyNames);
            }
        }

        void CollectLayerPositionsFromMotion(
            std::vector<PSBMedia::LayerPosition> &positions,
            std::vector<PSBMedia::ButtonBoundInfo> &buttons,
            const std::shared_ptr<PSBDictionary> &objectTree,
            const std::shared_ptr<spdlog::logger> &logger) {
            if(!objectTree)
                return;

            for(const auto &[sceneName, sceneVal] : *objectTree) {
                auto sceneDict =
                    std::dynamic_pointer_cast<PSBDictionary>(sceneVal);
                if(!sceneDict)
                    continue;

                auto motionDict = std::dynamic_pointer_cast<PSBDictionary>(
                    (*sceneDict)["motion"]);
                if(!motionDict)
                    continue;

                std::string motionName = "normal";
                if(!std::dynamic_pointer_cast<PSBDictionary>(
                       (*motionDict)[motionName])) {
                    motionName = "show";
                }
                if(!std::dynamic_pointer_cast<PSBDictionary>(
                       (*motionDict)[motionName])) {
                    motionName = "bt";
                }

                CollectLayersFromMotion(motionDict, motionName, sceneName, 0, 0,
                                        objectTree, positions, &buttons,
                                        logger);

                if(!std::dynamic_pointer_cast<PSBDictionary>(
                       (*motionDict)[motionName])) {
                    for(const auto &[altName, altVal] : *motionDict) {
                        if(!std::dynamic_pointer_cast<PSBDictionary>(altVal))
                            continue;
                        CollectLayersFromMotion(motionDict, altName, sceneName,
                                                0, 0, objectTree, positions,
                                                &buttons, logger);
                    }
                }
            }
        }

        void RegisterPSBResourcesIntoMedia(PSBMedia &media, PSBFile &psb,
                                           const std::string &archiveKey) {
            auto logger = LOGGER;
            size_t logged = 0;
            const auto objs = psb.getObjects();
            if(objs) {
                for(const auto &[name, value] : *objs) {
                    const auto resource =
                        std::dynamic_pointer_cast<PSBResource>(value);
                    if(!resource)
                        continue;
                    media.add(archiveKey + "/" + name, resource);
                    if(logger && logged < 40 &&
                       (archiveKey == "main.psb" || archiveKey == "title.psb" ||
                        archiveKey == "chapter.psb" ||
                        archiveKey == "autoskip.psb")) {
                        logger->info("psb register: {}/{}", archiveKey, name);
                        ++logged;
                    }
                }
            }

            auto *handler = psb.getTypeHandler();
            if(!handler)
                return;
            auto resources = handler->collectResources(psb, false);
            for(auto &metadata : resources) {
                auto *image = dynamic_cast<ImageMetadata *>(metadata.get());
                if(!image)
                    continue;
                auto resource = image->getResource();
                if(!resource)
                    continue;
                const std::string name = image->getName();
                if(name.empty())
                    continue;
                media.add(archiveKey + "/" + name, resource, image);
                if(logger && logged < 120 &&
                   (archiveKey == "main.psb" || archiveKey == "title.psb" ||
                    archiveKey == "chapter.psb" ||
                    archiveKey == "autoskip.psb")) {
                    logger->info("psb register: {}/{}", archiveKey, name);
                    ++logged;
                }
            }

            if(objs) {
                auto objectTree =
                    std::dynamic_pointer_cast<PSBDictionary>((*objs)["object"]);
                if(objectTree && logger) {
                    std::string objNames;
                    for(const auto &[k, v] : *objectTree) {
                        if(!objNames.empty())
                            objNames += ", ";
                        objNames += k;
                        auto d = std::dynamic_pointer_cast<PSBDictionary>(v);
                        if(d) {
                            auto m = std::dynamic_pointer_cast<PSBDictionary>(
                                (*d)["motion"]);
                            if(m) {
                                objNames += "[";
                                bool first = true;
                                for(const auto &[mk, mv] : *m) {
                                    if(!first)
                                        objNames += ",";
                                    objNames += mk;
                                    first = false;
                                }
                                objNames += "]";
                            }
                        }
                    }
                    logger->info("PSB objectTree for {}: {}", archiveKey,
                                 objNames);
                }

                if(objectTree) {
                    std::vector<PSBMedia::LayerPosition> positions;
                    std::vector<PSBMedia::ButtonBoundInfo> buttons;
                    CollectLayerPositionsFromMotion(positions, buttons,
                                                    objectTree, logger);
                    if(!positions.empty()) {
                        size_t count = positions.size();
                        media.addLayerPositions(archiveKey,
                                                std::move(positions));
                        if(logger)
                            logger->info("Stored {} layer positions for {}",
                                         count, archiveKey);
                    }
                    if(!buttons.empty()) {
                        size_t count = buttons.size();
                        media.addButtonBounds(archiveKey, std::move(buttons));
                        if(logger)
                            logger->info("Stored {} button bounds for {}",
                                         count, archiveKey);
                    }
                    CollectAllMotionTracks(media, archiveKey, objectTree,
                                           logger);
                    CollectAllMotionNodeTrees(media, archiveKey, objectTree,
                                              logger);
                }
            }
        }
    }

    PSBMedia::PSBMedia() {
        _ref = 1;

        tTJSVariant val;
        if(TVPGetCommandLine(TJS_W("memory_profile"), &val)) {
            ttstr profile = ttstr(val).AsLowerCase();
            if(profile == TJS_W("aggressive") || profile == TJS_W("lowmem")) {
                _configuredMaxEntryCount = 1024;
                _configuredMaxByteSize = 128ULL * 1024ULL * 1024ULL;
            }
        }

        if(TVPGetCommandLine(TJS_W("psb_cache_entries"), &val)) {
            const tjs_int configured = static_cast<tjs_int>(val.AsInteger());
            if(configured > 0) {
                _configuredMaxEntryCount = static_cast<size_t>(configured);
            }
        }
        if(TVPGetCommandLine(TJS_W("psb_cache_mb"), &val)) {
            const tjs_int configured = static_cast<tjs_int>(val.AsInteger());
            if(configured > 0) {
                _configuredMaxByteSize =
                    static_cast<size_t>(configured) * 1024ULL * 1024ULL;
            }
        }

        _configuredMaxEntryCount =
            ClampSizeT(_configuredMaxEntryCount, 128, 8192);
        _configuredMaxByteSize =
            ClampSizeT(_configuredMaxByteSize, 16ULL * 1024ULL * 1024ULL,
                       512ULL * 1024ULL * 1024ULL);
        _maxEntryCount = _configuredMaxEntryCount;
        _maxByteSize = _configuredMaxByteSize;
    }

    void PSBMedia::NormalizeDomainName(ttstr &name) {
        name = name.AsLowerCase();
    }

    void PSBMedia::NormalizePathName(ttstr &name) {
        auto *p = name.Independ();
        while(*p) {
            if(*p == TJS_W('\\')) {
                *p = TJS_W('/');
            }
            ++p;
        }
        name = name.AsLowerCase();
    }

    std::string PSBMedia::canonicalizeKey(const std::string &key) const {
        std::string out;
        out.reserve(key.size());
        for(const char ch : key) {
            if(ch == '\\') {
                out.push_back('/');
            } else if(ch >= 'A' && ch <= 'Z') {
                out.push_back(static_cast<char>(ch - 'A' + 'a'));
            } else {
                out.push_back(ch);
            }
        }

        constexpr const char *kFileScheme = "file://";
        if(out.rfind(kFileScheme, 0) == 0) {
            out.erase(0, 7);
            if(out.rfind("./", 0) == 0) {
                out.erase(0, 1);
            }
        }
        if(out.rfind("./", 0) == 0) {
            out.erase(0, 1);
        }

        std::string compact;
        compact.reserve(out.size());
        bool prevSlash = false;
        for(const char ch : out) {
            if(ch == '/') {
                if(prevSlash) {
                    continue;
                }
                prevSlash = true;
            } else {
                prevSlash = false;
            }
            compact.push_back(ch);
        }
        return compact;
    }

    void PSBMedia::touchLocked(CacheEntry &entry) {
        if(entry.lruIt != _lru.begin()) {
            _lru.splice(_lru.begin(), _lru, entry.lruIt);
            entry.lruIt = _lru.begin();
        }
    }

    void PSBMedia::adaptBudgetByMemoryPressureLocked() {
        const tjs_int self_used_mb = TVPGetSelfUsedMemory();
        const tjs_int free_mb = TVPGetSystemFreeMemory();

        size_t max_entry_count = _configuredMaxEntryCount;
        size_t max_byte_size = _configuredMaxByteSize;

        if((self_used_mb >= 1500) || (free_mb >= 0 && free_mb < 512)) {
            max_entry_count =
                std::min(max_entry_count, static_cast<size_t>(512));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(96ULL * 1024ULL * 1024ULL));
        } else if((self_used_mb >= 1100) || (free_mb >= 0 && free_mb < 800)) {
            max_entry_count =
                std::min(max_entry_count, static_cast<size_t>(768));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(144ULL * 1024ULL * 1024ULL));
        } else if((self_used_mb >= 850) || (free_mb >= 0 && free_mb < 1200)) {
            max_entry_count =
                std::min(max_entry_count, static_cast<size_t>(1024));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(192ULL * 1024ULL * 1024ULL));
        }

        _maxEntryCount = max_entry_count;
        _maxByteSize = max_byte_size;
    }

    PSBMedia::ResourceMap::iterator
    PSBMedia::findBySuffixLocked(const std::string &key) {
        for(auto it = _resources.begin(); it != _resources.end(); ++it) {
            const auto &stored = it->first;
            if(stored.size() < key.size()) {
                continue;
            }
            if(stored.compare(stored.size() - key.size(), key.size(), key) !=
               0) {
                continue;
            }
            if(stored.size() == key.size()) {
                return it;
            }

            const char boundary = stored[stored.size() - key.size() - 1];
            if(boundary == '/' || boundary == '>') {
                return it;
            }
        }
        return _resources.end();
    }

    void PSBMedia::evictIfNeededLocked() {
        adaptBudgetByMemoryPressureLocked();

        size_t evictedCount = 0;
        size_t evictedBytes = 0;
        while((_resources.size() > _maxEntryCount ||
               _bytesInUse > _maxByteSize) &&
              !_lru.empty()) {
            const std::string victimKey = _lru.back();
            _lru.pop_back();

            auto it = _resources.find(victimKey);
            if(it == _resources.end()) {
                continue;
            }

            evictedCount++;
            evictedBytes += it->second.sizeBytes;
            _bytesInUse -= it->second.sizeBytes;
            _resources.erase(it);
        }

        if(evictedCount > 0) {
            LOGGER->debug(
                "PSB media cache evicted: count={} bytes={} remain_count={} "
                "remain_bytes={}",
                evictedCount, evictedBytes, _resources.size(), _bytesInUse);
        }
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        const auto key = canonicalizeKey(name.AsStdString());
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_resources.find(key) != _resources.end()) {
                _hitCount++;
                return true;
            }
            const bool found = findBySuffixLocked(key) != _resources.end();
            if(found)
                _hitCount++;
            else
                _missCount++;
            if(found)
                return true;
        }
        if(!tryLazyLoadArchive(key))
            return false;
        std::lock_guard<std::mutex> lock(_mutex);
        if(_resources.find(key) != _resources.end()) {
            _hitCount++;
            return true;
        }
        const bool found = findBySuffixLocked(key) != _resources.end();
        if(found)
            _hitCount++;
        else
            _missCount++;
        return found;
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32 flags) {
        (void)flags;
        const auto key = canonicalizeKey(name.AsStdString());

        std::shared_ptr<PSBResource> res;
        std::shared_ptr<std::vector<uint8_t>> convertedImage;
        CachedImageInfo imageInfo;
        bool hasImageInfo = false;
        std::string resolvedKey;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _resources.find(key);
            if(it == _resources.end()) {
                it = findBySuffixLocked(key);
                if(it != _resources.end()) {
                    LOGGER->debug("PSB media cache suffix-hit: {} -> {}", key,
                                  it->first);
                }
            }
            if(it != _resources.end() && it->second.resource != nullptr) {
                _hitCount++;
                touchLocked(it->second);
                res = it->second.resource;
                convertedImage = it->second.convertedImage;
                imageInfo = it->second.imageInfo;
                hasImageInfo = it->second.hasImageInfo;
                resolvedKey = it->first;
            }
        }

        if(!res && tryLazyLoadArchive(key)) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _resources.find(key);
            if(it == _resources.end()) {
                it = findBySuffixLocked(key);
                if(it != _resources.end()) {
                    LOGGER->debug(
                        "PSB media cache suffix-hit(after-load): {} -> {}", key,
                        it->first);
                }
            }
            if(it != _resources.end() && it->second.resource != nullptr) {
                _hitCount++;
                touchLocked(it->second);
                res = it->second.resource;
                convertedImage = it->second.convertedImage;
                imageInfo = it->second.imageInfo;
                hasImageInfo = it->second.hasImageInfo;
                resolvedKey = it->first;
            }
        }

        if(!res) {
            std::lock_guard<std::mutex> lock(_mutex);
            _missCount++;
            LOGGER->warn("PSB media cache miss: {}", key);
            TVPThrowExceptionMessage(TJS_W("%1:cannot open psb resource"),
                                     name);
            return nullptr;
        }
        if(LOGGER &&
           (resolvedKey.rfind("main.psb/", 0) == 0 ||
            resolvedKey.rfind("title.psb/", 0) == 0 ||
            resolvedKey.rfind("chapter.psb/", 0) == 0 ||
            resolvedKey.rfind("autoskip.psb/", 0) == 0)) {
            const uint32_t header = res->data.size() >= 4
                ? static_cast<uint32_t>(res->data[0]) |
                    (static_cast<uint32_t>(res->data[1]) << 8) |
                    (static_cast<uint32_t>(res->data[2]) << 16) |
                    (static_cast<uint32_t>(res->data[3]) << 24)
                : 0;
            LOGGER->info("psb open: key={} hasMeta={} w={} h={} type={} "
                         "palType={} pal={} compress={} raw={} header=0x{:08x}",
                         resolvedKey, hasImageInfo ? 1 : 0, imageInfo.width,
                         imageInfo.height, imageInfo.type,
                         imageInfo.paletteType, imageInfo.palette.size(),
                         static_cast<int>(imageInfo.compress), res->data.size(),
                         header);
        }
        if(!convertedImage && hasImageInfo &&
           !IsSupportedImageHeader(res->data)) {
            convertedImage = BuildBmpFromRaw(imageInfo, res);
            if(LOGGER &&
               (resolvedKey.rfind("main.psb/", 0) == 0 ||
                resolvedKey.rfind("title.psb/", 0) == 0 ||
                resolvedKey.rfind("chapter.psb/", 0) == 0 ||
                resolvedKey.rfind("autoskip.psb/", 0) == 0)) {
                LOGGER->info(
                    "psb open: convert key={} ok={} converted={} type={}",
                    resolvedKey, convertedImage ? 1 : 0,
                    convertedImage ? convertedImage->size() : 0,
                    imageInfo.type);
            }
            if(convertedImage) {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _resources.find(resolvedKey);
                if(it != _resources.end()) {
                    _bytesInUse -= it->second.sizeBytes;
                    it->second.convertedImage = convertedImage;
                    it->second.sizeBytes = CalcEntryFootprint(it->second);
                    _bytesInUse += it->second.sizeBytes;
                    touchLocked(it->second);
                    evictIfNeededLocked();
                }
            }
        }

        const auto &streamBytes = convertedImage ? *convertedImage : res->data;
        if(LOGGER &&
           (resolvedKey.rfind("main.psb/", 0) == 0 ||
            resolvedKey.rfind("title.psb/", 0) == 0 ||
            resolvedKey.rfind("chapter.psb/", 0) == 0 ||
            resolvedKey.rfind("autoskip.psb/", 0) == 0)) {
            const uint32_t outHeader = streamBytes.size() >= 4
                ? static_cast<uint32_t>(streamBytes[0]) |
                    (static_cast<uint32_t>(streamBytes[1]) << 8) |
                    (static_cast<uint32_t>(streamBytes[2]) << 16) |
                    (static_cast<uint32_t>(streamBytes[3]) << 24)
                : 0;
            LOGGER->info("psb open: return key={} size={} header=0x{:08x}",
                         resolvedKey, streamBytes.size(), outHeader);
        }
        auto *memoryStream = new tTVPMemoryStream();
        memoryStream->WriteBuffer(streamBytes.data(), streamBytes.size());
        memoryStream->Seek(0, TJS_BS_SEEK_SET);
        return memoryStream;
    }

    bool PSBMedia::tryLazyLoadArchive(const std::string &key) {
        const auto archiveKey = ArchiveBoundaryKey(key);
        if(archiveKey.empty() || archiveKey == key)
            return false;
        bool shouldAttemptLoad = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            shouldAttemptLoad = _loadedArchives.insert(archiveKey).second;
        }
        if(!shouldAttemptLoad)
            return false;

        try {
            ttstr archivePath(archiveKey.c_str());
            if(auto cached =
                   motion::ResourceManager::getLoadedFile(archivePath)) {
                LOGGER->info("PSB lazy-load archive(from cache): {}",
                             archiveKey);
                RegisterPSBResourcesIntoMedia(*this, *cached, archiveKey);
                return true;
            }

            PSBFile psb;
            psb.setSeed(motion::ResourceManager::getDecryptSeed());
            if(!psb.loadPSBFile(archivePath)) {
                std::lock_guard<std::mutex> lock(_mutex);
                _loadedArchives.erase(archiveKey);
                LOGGER->debug("PSB lazy-load failed: {}", archiveKey);
                return false;
            }
            LOGGER->info("PSB lazy-load archive: {}", archiveKey);
            RegisterPSBResourcesIntoMedia(*this, psb, archiveKey);
            return true;
        } catch(const std::exception &e) {
            std::lock_guard<std::mutex> lock(_mutex);
            _loadedArchives.erase(archiveKey);
            LOGGER->warn("PSB lazy-load error: {} ({})", e.what(), archiveKey);
            return false;
        } catch(...) {
            std::lock_guard<std::mutex> lock(_mutex);
            _loadedArchives.erase(archiveKey);
            LOGGER->warn("PSB lazy-load unknown error: {}", archiveKey);
            return false;
        }
    }

    bool PSBMedia::ensureArchiveLoaded(const std::string &archiveKey) {
        return tryLazyLoadArchive(archiveKey + "/_probe");
    }

    void PSBMedia::GetListAt(const ttstr &name, iTVPStorageLister *lister) {
        LOGGER->error("TODO: PSBMedia GetListAt");
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        LOGGER->error("can't get GetLocallyAccessibleName from {}!",
                      name.AsStdString());
    }

    void PSBMedia::add(const std::string &name,
                       const std::shared_ptr<PSBResource> &resource,
                       const ImageMetadata *imageMeta) {
        if(resource == nullptr) {
            return;
        }

        const auto key = canonicalizeKey(name);
        const size_t incomingSize = resource->data.size();

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _resources.find(key);
        if(it != _resources.end()) {
            _bytesInUse -= it->second.sizeBytes;
            it->second.resource = resource;
            it->second.convertedImage.reset();
            it->second.hasImageInfo = imageMeta != nullptr;
            if(imageMeta) {
                it->second.imageInfo.debugKey = key;
                it->second.imageInfo.width = imageMeta->getWidth();
                it->second.imageInfo.height = imageMeta->getHeight();
                it->second.imageInfo.left = imageMeta->getLeft();
                it->second.imageInfo.top = imageMeta->getTop();
                it->second.imageInfo.originX = imageMeta->getOriginX();
                it->second.imageInfo.originY = imageMeta->getOriginY();
                it->second.imageInfo.opacity = imageMeta->getOpacity();
                it->second.imageInfo.visible = imageMeta->getVisible();
                it->second.imageInfo.layerType = imageMeta->getLayerType();
                it->second.imageInfo.type = imageMeta->getType();
                it->second.imageInfo.paletteType = imageMeta->getPalType();
                it->second.imageInfo.spec = imageMeta->getSpec();
                it->second.imageInfo.compress = imageMeta->getCompress();
                it->second.imageInfo.palette = imageMeta->getPalette().data;
            }
            it->second.sizeBytes = CalcEntryFootprint(it->second);
            _bytesInUse += it->second.sizeBytes;
            touchLocked(it->second);
        } else {
            _lru.push_front(key);
            CacheEntry entry{};
            entry.resource = resource;
            entry.hasImageInfo = imageMeta != nullptr;
            if(imageMeta) {
                entry.imageInfo.debugKey = key;
                entry.imageInfo.width = imageMeta->getWidth();
                entry.imageInfo.height = imageMeta->getHeight();
                entry.imageInfo.left = imageMeta->getLeft();
                entry.imageInfo.top = imageMeta->getTop();
                entry.imageInfo.originX = imageMeta->getOriginX();
                entry.imageInfo.originY = imageMeta->getOriginY();
                entry.imageInfo.opacity = imageMeta->getOpacity();
                entry.imageInfo.visible = imageMeta->getVisible();
                entry.imageInfo.layerType = imageMeta->getLayerType();
                entry.imageInfo.type = imageMeta->getType();
                entry.imageInfo.paletteType = imageMeta->getPalType();
                entry.imageInfo.spec = imageMeta->getSpec();
                entry.imageInfo.compress = imageMeta->getCompress();
                entry.imageInfo.palette = imageMeta->getPalette().data;
            }
            entry.sizeBytes = CalcEntryFootprint(entry);
            entry.lruIt = _lru.begin();
            size_t entrySize = entry.sizeBytes;
            _resources.emplace(key, std::move(entry));
            _bytesInUse += entrySize;
        }

        evictIfNeededLocked();
    }

    void PSBMedia::setCacheBudget(size_t maxEntries, size_t maxBytes) {
        std::lock_guard<std::mutex> lock(_mutex);
        _configuredMaxEntryCount = ClampSizeT(maxEntries, 128, 8192);
        _configuredMaxByteSize = ClampSizeT(maxBytes, 16ULL * 1024ULL * 1024ULL,
                                            512ULL * 1024ULL * 1024ULL);
        _maxEntryCount = _configuredMaxEntryCount;
        _maxByteSize = _configuredMaxByteSize;
        evictIfNeededLocked();
    }

    PSBMediaCacheStats PSBMedia::getCacheStats() const {
        std::lock_guard<std::mutex> lock(_mutex);
        PSBMediaCacheStats stats;
        stats.entryCount = _resources.size();
        stats.entryLimit = _maxEntryCount;
        stats.bytesInUse = _bytesInUse;
        stats.byteLimit = _maxByteSize;
        stats.hitCount = _hitCount;
        stats.missCount = _missCount;
        return stats;
    }

    void PSBMedia::removeByPrefix(const std::string &prefix) {
        std::string normalizedPrefix = canonicalizeKey(prefix);
        if(!normalizedPrefix.empty() && normalizedPrefix.back() != '/') {
            normalizedPrefix.push_back('/');
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for(auto it = _resources.begin(); it != _resources.end();) {
            if(it->first.rfind(normalizedPrefix, 0) == 0) {
                _bytesInUse -= it->second.sizeBytes;
                _lru.erase(it->second.lruIt);
                it = _resources.erase(it);
                continue;
            }
            ++it;
        }
    }

    void PSBMedia::clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _resources.clear();
        _lru.clear();
        _bytesInUse = 0;
        _hitCount = 0;
        _missCount = 0;
    }

    std::vector<PSBMedia::ImageInfoEntry>
    PSBMedia::getImagesByPrefix(const std::string &prefix) const {
        std::string norm = canonicalizeKey(prefix);
        if(!norm.empty() && norm.back() != '/')
            norm.push_back('/');
        std::vector<ImageInfoEntry> result;
        std::lock_guard<std::mutex> lock(_mutex);
        for(const auto &[key, entry] : _resources) {
            if(key.rfind(norm, 0) != 0)
                continue;
            if(!entry.hasImageInfo)
                continue;
            result.push_back({ key, entry.imageInfo });
        }
        return result;
    }

    bool PSBMedia::getImageInfo(const std::string &key,
                                CachedImageInfo &outInfo) const {
        std::string norm = canonicalizeKey(key);
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _resources.find(norm);
        if(it == _resources.end() || !it->second.hasImageInfo)
            return false;
        outInfo = it->second.imageInfo;
        return true;
    }

    void PSBMedia::addLayerPositions(const std::string &archiveKey,
                                     std::vector<LayerPosition> positions) {
        std::lock_guard<std::mutex> lock(_mutex);
        _layerPositions[archiveKey] = std::move(positions);
    }

    std::vector<PSBMedia::LayerPosition>
    PSBMedia::getLayerPositions(const std::string &prefix) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _layerPositions.find(prefix);
        if(it != _layerPositions.end())
            return it->second;
        return {};
    }

    void PSBMedia::addButtonBounds(const std::string &archiveKey,
                                   std::vector<ButtonBoundInfo> bounds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _buttonBoundsMap[archiveKey] = std::move(bounds);
    }

    std::vector<PSBMedia::ButtonBoundInfo>
    PSBMedia::getButtonBounds(const std::string &prefix) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _buttonBoundsMap.find(prefix);
        if(it != _buttonBoundsMap.end())
            return it->second;
        return {};
    }

    void PSBMedia::addMotionTracks(const std::string &archiveKey,
                                   const std::string &sceneName,
                                   const std::string &motionName,
                                   std::vector<PSBMotionLayerTrack> tracks) {
        std::lock_guard<std::mutex> lock(_mutex);
        _motionTracks[archiveKey + "|" + sceneName + "|" + motionName] =
            std::move(tracks);
    }

    void PSBMedia::addMotionNodes(const std::string &archiveKey,
                                  const std::string &sceneName,
                                  const std::string &motionName,
                                  std::vector<PSBMotionNode> nodes) {
        std::lock_guard<std::mutex> lock(_mutex);
        _motionNodes[archiveKey + "|" + sceneName + "|" + motionName] =
            std::move(nodes);
    }

    void
    PSBMedia::setMotionParameters(const std::string &archiveKey,
                                  const std::string &sceneName,
                                  const std::string &motionName,
                                  std::vector<PSBMotionParameter> parameters) {
        std::lock_guard<std::mutex> lock(_mutex);
        _motionParameters[archiveKey + "|" + sceneName + "|" + motionName] =
            std::move(parameters);
    }

    std::vector<PSBMotionParameter>
    PSBMedia::getMotionParameters(const std::string &archiveKey,
                                  const std::string &sceneName,
                                  const std::string &motionName) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _motionParameters.find(archiveKey + "|" + sceneName + "|" +
                                         motionName);
        if(it != _motionParameters.end())
            return it->second;
        return {};
    }

    std::vector<PSBMedia::PSBMotionNode>
    PSBMedia::getMotionNodes(const std::string &archiveKey,
                             const std::string &sceneName,
                             const std::string &motionName) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it =
            _motionNodes.find(archiveKey + "|" + sceneName + "|" + motionName);
        if(it != _motionNodes.end())
            return it->second;
        return {};
    }

    void PSBMedia::setMotionLoopTime(const std::string &archiveKey,
                                     const std::string &sceneName,
                                     const std::string &motionName,
                                     tjs_int loopTime) {
        std::lock_guard<std::mutex> lock(_mutex);
        _motionLoopTimes[archiveKey + "|" + sceneName + "|" + motionName] =
            loopTime;
    }

    tjs_int PSBMedia::getMotionLoopTime(const std::string &archiveKey,
                                        const std::string &sceneName,
                                        const std::string &motionName) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _motionLoopTimes.find(archiveKey + "|" + sceneName + "|" +
                                        motionName);
        if(it != _motionLoopTimes.end())
            return it->second;
        return 0;
    }

    std::vector<PSBMedia::PSBMotionLayerTrack>
    PSBMedia::getMotionTracks(const std::string &archiveKey,
                              const std::string &sceneName,
                              const std::string &motionName) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it =
            _motionTracks.find(archiveKey + "|" + sceneName + "|" + motionName);
        if(it != _motionTracks.end())
            return it->second;
        return {};
    }

    std::vector<std::string>
    PSBMedia::getMotionNames(const std::string &archiveKey,
                             const std::string &sceneName) const {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<std::string> names;
        const std::string prefix = archiveKey + "|" + sceneName + "|";
        for(const auto &[key, tracks] : _motionTracks) {
            if(key.size() > prefix.size() &&
               key.compare(0, prefix.size(), prefix) == 0) {
                names.push_back(key.substr(prefix.size()));
            }
        }
        return names;
    }
} // namespace PSB
