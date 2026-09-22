#include "tjsCommHead.h"

#include "LoadAMV.h"
#include "AlphaMovieDecoder.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"

#include <turbojpeg.h>
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include <spdlog/spdlog.h>
#include "tjsDictionary.h"

#pragma pack(push, 1)
struct AMVHeader {
    tjs_uint32 magic;
    tjs_uint32 size_of_file;
    tjs_uint32 revision;
    tjs_uint32 qt_size_plus_hdr;
    tjs_uint32 unk;
    tjs_uint32 frame_cnt;
    tjs_uint32 unk2;
    tjs_uint32 frame_rate;
    tjs_uint16 width;
    tjs_uint16 height;
    tjs_uint32 alpha_decode_attr; // 1=JPEG, 2=ZLIB
};

struct AMVZlibFrameHeader {
    tjs_uint32 magic;
    tjs_uint32 size_of_frame;
    tjs_uint32 index;
    // 这 4 个 uint16 是**裁剪矩形** left/top/width/height（上游注释：它们是
    // copyNextImageToTexture 返回的 Rect.left/top 与宽高），不是 alpha 平面
    // 尺寸。历史命名有误导，这里按上游语义取名。
    tjs_uint16 frame_left;
    tjs_uint16 frame_top;
    tjs_uint16 frame_width;
    tjs_uint16 frame_height;
    tjs_uint32 rgb_buffer_size;
};

struct AMVJpegFrameHeader {
    tjs_uint32 magic;
    tjs_uint32 size_of_frame;
    tjs_uint32 index;
    tjs_uint16 frame_left;
    tjs_uint16 frame_top;
    tjs_uint16 frame_width;
    tjs_uint16 frame_height;
};
#pragma pack(pop)

static const tjs_uint32 AMV_MAGIC = 0x4D504A41;
static const tjs_uint32 FRAM_MAGIC = 0x4D415246;

// ---------------------------------------------------------------------------
// JPEG helpers (for alpha_decode_attr == 1)
// ---------------------------------------------------------------------------
static std::vector<unsigned char> BuildDQTSegment(const unsigned char *qtData,
                                                  size_t qtSize) {
    int numTables = (int)(qtSize / 64);
    size_t lq = 2 + numTables * 65;
    std::vector<unsigned char> seg;
    seg.reserve(2 + lq);
    seg.push_back(0xFF);
    seg.push_back(0xDB);
    seg.push_back((unsigned char)((lq >> 8) & 0xFF));
    seg.push_back((unsigned char)(lq & 0xFF));
    for(int t = 0; t < numTables; t++) {
        seg.push_back((unsigned char)t);
        seg.insert(seg.end(), qtData + t * 64, qtData + (t + 1) * 64);
    }
    return seg;
}

static std::vector<unsigned char>
InjectDQT(const unsigned char *jpegData, size_t jpegSize,
          const std::vector<unsigned char> &dqtSeg) {
    bool hasSOI = (jpegSize >= 2 && jpegData[0] == 0xFF && jpegData[1] == 0xD8);
    std::vector<unsigned char> result;
    result.reserve(jpegSize + dqtSeg.size() + 2);
    result.push_back(0xFF);
    result.push_back(0xD8);
    result.insert(result.end(), dqtSeg.begin(), dqtSeg.end());
    size_t srcOff = hasSOI ? 2 : 0;
    result.insert(result.end(), jpegData + srcOff, jpegData + jpegSize);
    return result;
}

static bool FindSecondSOI(const unsigned char *data, size_t len,
                          size_t &colorSize) {
    for(size_t i = 2; i + 1 < len; i++) {
        if(data[i] == 0xFF && data[i + 1] == 0xD8) {
            colorSize = i;
            return true;
        }
    }
    return false;
}

static bool TryDecodeJpeg(const unsigned char *data, size_t size,
                          int pixelFormat, int numComponents, int &outW,
                          int &outH, std::vector<unsigned char> &outBuf) {
    tjhandle dec = tjInitDecompress();
    if(!dec)
        return false;
    int w = 0, h = 0, subsamp = 0;
    if(tjDecompressHeader2(dec, const_cast<unsigned char *>(data),
                           (unsigned long)size, &w, &h, &subsamp) != 0) {
#if defined(KRKR_RENDER_PROBE)
        spdlog::info(
            "probe: AMV header 失败：{}（size={}，first={:02X}{:02X}）",
            tjGetErrorStr2(dec), size, size > 0 ? data[0] : 0,
            size > 1 ? data[1] : 0);
#endif
        tjDestroy(dec);
        return false;
    }
    outW = w;
    outH = h;
    outBuf.resize((size_t)w * h * numComponents);
    int ret = tjDecompress2(dec, const_cast<unsigned char *>(data),
                            (unsigned long)size, outBuf.data(), w,
                            w * numComponents, h, pixelFormat, TJFLAG_FASTDCT);
    if(ret != 0) {
        int errCode = tjGetErrorCode(dec);
#if defined(KRKR_RENDER_PROBE)
        spdlog::info("probe: AMV tjDecompress2 失败：code={} msg={} {}x{} "
                     "subsamp={} pf={} ncomp={}",
                     static_cast<int>(errCode), tjGetErrorStr2(dec), w, h,
                     subsamp, pixelFormat, numComponents);
#endif
        if(errCode == TJERR_WARNING) {
            tjDestroy(dec);
            return true;
        }
        tjDestroy(dec);
        return false;
    }
    tjDestroy(dec);
    return true;
}

static bool DecodeJpegWithQT(const unsigned char *jpegData, size_t jpegSize,
                             const std::vector<unsigned char> &dqtSeg,
                             int pixelFormat, int numComponents, int &outW,
                             int &outH, std::vector<unsigned char> &pixelOut) {
    if(TryDecodeJpeg(jpegData, jpegSize, pixelFormat, numComponents, outW, outH,
                     pixelOut))
        return true;
    if(!dqtSeg.empty()) {
        auto patched = InjectDQT(jpegData, jpegSize, dqtSeg);
        const bool ok = TryDecodeJpeg(patched.data(), patched.size(),
                                      pixelFormat, numComponents, outW, outH,
                                      pixelOut);
#if defined(KRKR_RENDER_PROBE)
        spdlog::info("probe: AMV DQT 注入重试 ok={} dqtSeg={}B jpeg={}B",
                     ok ? 1 : 0, dqtSeg.size(), jpegSize);
#endif
        return ok;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Read helpers — read only what we need from the stream
// ---------------------------------------------------------------------------
static void ReadExact(tTJSBinaryStream *src, void *buf, tjs_uint len) {
    if(src->Read(buf, len) != len)
        TVPThrowExceptionMessage(TJS_W("AMV: read error"));
}

// ---------------------------------------------------------------------------
// Main loader
// ---------------------------------------------------------------------------
void TVPLoadAMV(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int32 keyidx,
                tTVPGraphicLoadMode mode) {
    if(mode == glmPalettized)
        TVPThrowExceptionMessage(TJS_W("AMV does not support palettized mode"));

    // --- Read & validate header ---
    AMVHeader hdr;
    ReadExact(src, &hdr, sizeof(hdr));
    if(hdr.magic != AMV_MAGIC)
        TVPThrowExceptionMessage(TJS_W("AMV: invalid magic"));
    if(hdr.frame_cnt == 0)
        TVPThrowExceptionMessage(TJS_W("AMV: zero frames"));

    int imgW = hdr.width;
    int imgH = hdr.height;
    if(imgW <= 0 || imgH <= 0)
        TVPThrowExceptionMessage(TJS_W("AMV: invalid dimensions"));

    bool isZlibMode = (hdr.alpha_decode_attr == 2);
    size_t qtSize = hdr.qt_size_plus_hdr - sizeof(AMVHeader);

    auto logger = spdlog::get("core");
    if(logger)
        logger->debug("AMV: {}x{}, {} frames, mode={}", imgW, imgH,
                      hdr.frame_cnt, isZlibMode ? "zlib" : "jpeg");

    // --- Read QT data (skip over it for zlib mode) ---
    std::vector<unsigned char> qtData;
    if(!isZlibMode && qtSize >= 64) {
        qtData.resize(qtSize);
        ReadExact(src, qtData.data(), (tjs_uint)qtSize);
    } else {
        src->SetPosition(src->GetPosition() + qtSize);
    }

    // --- Push metadata ---
    if(metainfopushcallback) {
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_frames")),
                             ttstr((tjs_int)hdr.frame_cnt));
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_fps")),
                             ttstr((tjs_int)hdr.frame_rate));
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_width")),
                             ttstr((tjs_int)imgW));
        metainfopushcallback(callbackdata, ttstr(TJS_W("amv_height")),
                             ttstr((tjs_int)imgH));
    }

    // --- Read first frame header ---
    int frameLeft = 0, frameTop = 0, frameW = 0, frameH = 0;
    tjs_uint32 sizeOfFrame = 0, rgbBufSize = 0;
    size_t extraHdr;

    if(isZlibMode) {
        AMVZlibFrameHeader fh;
        ReadExact(src, &fh, sizeof(fh));
        if(fh.magic != FRAM_MAGIC)
            TVPThrowExceptionMessage(TJS_W("AMV: invalid frame magic"));
        sizeOfFrame = fh.size_of_frame;
        frameLeft = fh.frame_left;
        frameTop = fh.frame_top;
        frameW = fh.frame_width;
        frameH = fh.frame_height;
        rgbBufSize = fh.rgb_buffer_size;
        extraHdr = sizeof(AMVZlibFrameHeader) - 8;
    } else {
        AMVJpegFrameHeader fh;
        ReadExact(src, &fh, sizeof(fh));
        if(fh.magic != FRAM_MAGIC)
            TVPThrowExceptionMessage(TJS_W("AMV: invalid frame magic"));
        sizeOfFrame = fh.size_of_frame;
        frameLeft = fh.frame_left;
        frameTop = fh.frame_top;
        frameW = fh.frame_width;
        frameH = fh.frame_height;
        extraHdr = sizeof(AMVJpegFrameHeader) - 8;
    }

    if(sizeOfFrame < extraHdr)
        TVPThrowExceptionMessage(TJS_W("AMV: frame data too small"));

    size_t payloadLen = sizeOfFrame - extraHdr;

    // --- Read frame payload (only the first frame) ---
    std::vector<unsigned char> payload(payloadLen);
    ReadExact(src, payload.data(), (tjs_uint)payloadLen);
    const unsigned char *payloadStart = payload.data();

#if defined(KRKR_RENDER_PROBE)
    {
        // 结构探针：这份 AMV 的帧载荷不以 JPEG SOI 开头（首字节非 FFD8），
        // 需要看清 [前缀][JPEG] 的边界、是否含 alpha 段、以及是否以 EOI 收尾。
        //   SOI_count>1  ⇒ 载荷内有 color/alpha 两段 JPEG
        //   tail16 以 FFD9 结尾 ⇒ 最后一段是完整 JPEG，前缀只需跳过
        const int dump = static_cast<int>(std::min(payloadLen, (size_t)32));
        std::string hex;
        hex.reserve(dump * 2);
        for(int i = 0; i < dump; i++) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", payload[i]);
            hex += buf;
        }

        std::string soiList;
        size_t soiCount = 0;
        for(size_t i = 0; i + 1 < payloadLen; i++) {
            if(payload[i] == 0xFF && payload[i + 1] == 0xD8) {
                ++soiCount;
                if(soiCount <= 8) {
                    soiList += std::to_string(i);
                    soiList += ',';
                }
            }
        }

        const int tailN = static_cast<int>(std::min(payloadLen, (size_t)16));
        std::string tailHex;
        tailHex.reserve(tailN * 2);
        for(int i = 0; i < tailN; i++) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X",
                          payload[payloadLen - tailN + i]);
            tailHex += buf;
        }

        spdlog::info(
            "probe: AMV payload first32={} payloadLen={} extraHdr={} "
            "sizeOfFrame={} SOI_count={} SOI_offsets=[{}] tail16={}",
            hex, payloadLen, extraHdr, sizeOfFrame, soiCount, soiList,
            tailHex);
        spdlog::info(
            "probe: AMV variant revision={} qt_size_plus_hdr={} unk={} unk2={} "
            "attr={} frame={}x{} alpha={}x{}",
            hdr.revision, hdr.qt_size_plus_hdr, hdr.unk, hdr.unk2,
            hdr.alpha_decode_attr, imgW, imgH, frameW, frameH);
    }
#endif

    // --- Decode ---
    std::vector<tjs_uint32> rgba(imgW * imgH, 0);

    if(isZlibMode) {
        if(rgbBufSize > payloadLen)
            TVPThrowExceptionMessage(TJS_W("AMV: rgb_buffer_size overflow"));

        int colorW = frameW > 0 ? frameW : imgW;
        int colorH = frameH > 0 ? frameH : imgH;

        if(rgbBufSize > 0) {
            unsigned long destLen =
                (unsigned long)std::max(colorW * colorH, imgW * imgH);
            std::vector<unsigned char> colorRaw(destLen);
            int zret =
                uncompress(colorRaw.data(), &destLen, payloadStart, rgbBufSize);
            if(zret == Z_OK) {
                int copyW = std::min(colorW, imgW);
                int copyH = std::min(colorH, imgH);
                for(int y = 0; y < copyH; y++) {
                    for(int x = 0; x < copyW; x++) {
                        size_t si = (size_t)y * colorW + x;
                        if(si >= destLen)
                            break;
                        unsigned char v = colorRaw[si];
                        rgba[y * imgW + x] =
                            ((tjs_uint32)v << 24) | 0x00FFFFFFu;
                    }
                }
            } else if(logger) {
                logger->warn("AMV: zlib decompress failed ({})", zret);
            }
        }
    } else {
        // AlphaMovie 变体：载荷不是标准 JPEG（没有 SOI/DHT——Huffman 用标准表，
        // DQT 取自文件头），turbojpeg 会报 Could not determine subsampling level。
        // 先用专用解码器；失败再走标准 JPEG 路径。两种 AMV 变体靠载荷内容区分，
        // 不靠扩展名。
        bool decodedAmv = false;
        {
            krkr::alphamovie::FrameGeometry geo;
            geo.left = static_cast<uint16_t>(frameLeft);
            geo.top = static_cast<uint16_t>(frameTop);
            geo.width = static_cast<uint16_t>(frameW);
            geo.height = static_cast<uint16_t>(frameH);
            uint8_t qtbl[3][64] = {};
            if(qtData.size() >= sizeof(qtbl))
                std::memcpy(qtbl, qtData.data(), sizeof(qtbl));
            std::vector<uint8_t> amvRgba;
            if(krkr::alphamovie::DecodeFrameToRgba(qtbl, false, 0, geo,
                                                   payloadStart, payloadLen,
                                                   amvRgba)) {
                const size_t need =
                    static_cast<size_t>(geo.width) * geo.height * 4;
                const int amvCopyW =
                    std::min(static_cast<int>(geo.width), imgW - geo.left);
                const int amvCopyH =
                    std::min(static_cast<int>(geo.height), imgH - geo.top);
                if(amvRgba.size() >= need && amvCopyW > 0 && amvCopyH > 0) {
                    for(int y = 0; y < amvCopyH; y++) {
                        const uint8_t *s = amvRgba.data() +
                                           static_cast<size_t>(y) *
                                               geo.width * 4;
                        tjs_uint32 *d =
                            rgba.data() +
                            static_cast<size_t>(geo.top + y) * imgW + geo.left;
                        std::memcpy(d, s, static_cast<size_t>(amvCopyW) * 4);
                    }
                    decodedAmv = true;
                }
            }
        }
        auto dqtSeg = BuildDQTSegment(qtData.data(), qtData.size());

        if(!decodedAmv) {
        size_t colorSize = payloadLen;
        size_t alphaDataOffset = payloadLen;
        FindSecondSOI(payloadStart, payloadLen, colorSize);
        alphaDataOffset = colorSize;

        int decW = 0, decH = 0;
        std::vector<unsigned char> rgbPixels;
        if(!DecodeJpegWithQT(payloadStart, colorSize, dqtSeg, TJPF_RGBA, 4,
                             decW, decH, rgbPixels)) {
#if defined(KRKR_RENDER_PROBE)
            // 探针：载荷不以 SOI 开头时，试着从首个 FFD8 起解码，判定
            // “前缀只需跳过”还是“数据本身不是 JPEG（被加密或其它变体）”。
            {
                size_t off = 0;
                bool found = false;
                for(size_t i = 0; i + 1 < payloadLen; i++) {
                    if(payloadStart[i] == 0xFF &&
                       payloadStart[i + 1] == 0xD8) {
                        off = i;
                        found = true;
                        break;
                    }
                }
                if(found) {
                    int pw = 0, ph = 0;
                    std::vector<unsigned char> tmp;
                    const bool ok = TryDecodeJpeg(payloadStart + off,
                                                  payloadLen - off, TJPF_RGBA,
                                                  4, pw, ph, tmp);
                    spdlog::info(
                        "probe: AMV retry-from-SOI off={} len={} ok={} {}x{}",
                        off, payloadLen - off, ok ? 1 : 0, pw, ph);
                } else {
                    spdlog::info("probe: AMV retry-from-SOI 未找到 FFD8");
                }
            }
#endif
            TVPThrowExceptionMessage(TJS_W("AMV: color JPEG decode failed"));
        }

        int copyW = std::min(decW, imgW);
        int copyH = std::min(decH, imgH);
        for(int y = 0; y < copyH; y++) {
            const tjs_uint32 *srcRow =
                reinterpret_cast<const tjs_uint32 *>(rgbPixels.data()) +
                y * decW;
            std::memcpy(rgba.data() + y * imgW, srcRow,
                        copyW * sizeof(tjs_uint32));
        }

        if(alphaDataOffset < payloadLen && frameW > 0 && frameH > 0) {
            const unsigned char *alphaJpeg = payloadStart + alphaDataOffset;
            size_t alphaJpegLen = payloadLen - alphaDataOffset;
            int aW = 0, aH = 0;
            std::vector<unsigned char> grayPixels;
            if(DecodeJpegWithQT(alphaJpeg, alphaJpegLen, dqtSeg, TJPF_GRAY, 1,
                                aW, aH, grayPixels)) {
                int applyW = std::min({ aW, (int)frameW, imgW });
                int applyH = std::min({ aH, (int)frameH, imgH });
                for(int y = 0; y < applyH; y++) {
                    for(int x = 0; x < applyW; x++) {
                        unsigned char a = grayPixels[y * aW + x];
                        tjs_uint32 &px = rgba[y * imgW + x];
                        px = (px & 0x00FFFFFFu) | ((tjs_uint32)a << 24);
                    }
                }
            }
        }
        } // if(!decodedAmv)
    }

    // --- Output to engine ---
    if(mode == glmGrayscale) {
        sizecallback(callbackdata, imgW, imgH, gpfLuminance);
        for(int y = 0; y < imgH; y++) {
            void *scanline = scanlinecallback(callbackdata, y);
            if(!scanline)
                break;
            unsigned char *dst = static_cast<unsigned char *>(scanline);
            for(int x = 0; x < imgW; x++) {
                tjs_uint32 px = rgba[y * imgW + x];
                unsigned char b = (px >> 0) & 0xFF;
                unsigned char g = (px >> 8) & 0xFF;
                unsigned char r = (px >> 16) & 0xFF;
                dst[x] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
            }
            scanlinecallback(callbackdata, -1);
        }
    } else {
        sizecallback(callbackdata, imgW, imgH, gpfRGBA);
        for(int y = 0; y < imgH; y++) {
            void *scanline = scanlinecallback(callbackdata, y);
            if(!scanline)
                break;
            std::memcpy(scanline, rgba.data() + y * imgW,
                        imgW * sizeof(tjs_uint32));
            scanlinecallback(callbackdata, -1);
        }
    }
}

void TVPLoadHeaderAMV(void *formatdata, tTJSBinaryStream *src,
                      class iTJSDispatch2 **dic) {
    AMVHeader hdr;
    if(src->Read(&hdr, sizeof(hdr)) != sizeof(hdr))
        return;
    if(hdr.magic != AMV_MAGIC)
        return;

    if(dic) {
        *dic = TJSCreateDictionaryObject();
        tTJSVariant val;
        val = (tjs_int)hdr.width;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, *dic);
        val = (tjs_int)hdr.height;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val, *dic);
        val = (tjs_int)hdr.frame_cnt;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("frames"), nullptr, &val, *dic);
        val = (tjs_int)hdr.frame_rate;
        (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("fps"), nullptr, &val, *dic);
    }
}
