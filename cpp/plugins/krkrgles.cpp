#include "tjs.h"
#include "ncbind.hpp"
#include "ScriptMgnIntf.h"
#include "SysInitIntf.h"
#include "EventIntf.h"
#include "LayerImpl.h"
#include "RenderManager.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <png.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

// NDK 的 GLES 头声明了 ES3 的 glGetTexLevelParameteriv，却**没有**定义它要用的两个
// pname（实测 NDK 27：krkrgles.cpp 用 gl3.h 仍报 use of undeclared identifier
// 'GL_TEXTURE_WIDTH'）。这里按 Khronos 的数值补上，供 KTX level-0 完整性校验使用。
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif
#ifndef GL_TEXTURE_HEIGHT
#define GL_TEXTURE_HEIGHT 0x1001
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__SSSE3__)
#include <tmmintrin.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define GLES_LOGI(...)                                                         \
    __android_log_print(ANDROID_LOG_INFO, "krkrgles", __VA_ARGS__)
#define GLES_LOGW(...)                                                         \
    __android_log_print(ANDROID_LOG_WARN, "krkrgles", __VA_ARGS__)
#else
#define GLES_LOGI(...) ((void)0)
#define GLES_LOGW(...) ((void)0)
#endif

#define NCB_MODULE_NAME TJS_W("krkrgles.dll")

#ifdef KRKR2_LIVE2D
// Live2D model's internal FBO — published by krkrlive2d.cpp (仅 SDK 存在时编译)
struct Live2DRenderTarget {
    GLuint fbo;
    GLsizei width;
    GLsizei height;
};
extern Live2DRenderTarget g_live2dRenderTarget;
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// KRKR_RENDER_PROBE：TJS 侧调用面探针
//
// 目的：G2 的「全动画」模式整屏全黑且日志里一条报错都没有——需要先知道游戏到底
// 调用了本插件的哪些接口，才能判断缺的是哪一条绘制原语。
//
// 做法：按「方法名 + 参数个数 + 参数类型序列」去重，只在该签名**首次**出现时打印
// 一次并带上参数值。这些回调会被每帧调用，不去重会刷屏（README 硬约束 4：
// 高频日志必须采样/限频/去重/仅边沿）。默认关闭，靠 -DENABLE_RENDER_PROBE=ON 打开。
// ---------------------------------------------------------------------------
#if defined(KRKR_RENDER_PROBE)
namespace {

    const char *ProbeTypeName(const tTJSVariant *v) {
        if(!v)
            return "null";
        switch(v->Type()) {
            case tvtVoid:   return "void";
            case tvtObject: return "object";
            case tvtString: return "string";
            case tvtInteger:return "int";
            case tvtReal:   return "real";
            case tvtOctet:  return "octet";
            default:        return "?";
        }
    }

    std::string ProbeValue(const tTJSVariant *v) {
        if(!v)
            return "null";
        switch(v->Type()) {
            case tvtInteger:
                return std::to_string(static_cast<long long>(
                    static_cast<tjs_int>(*v)));
            case tvtReal:
                return std::to_string(static_cast<double>(*v));
            case tvtString: {
                std::string s = ttstr(*v).AsStdString();
                if(s.size() > 24)
                    s = s.substr(0, 24) + "...";
                return "\"" + s + "\"";
            }
            case tvtObject:
                return "object";
            default:
                return ProbeTypeName(v);
        }
    }

    // 返回 true 表示这个签名是第一次见到（调用方据此决定要不要打印）。
    bool ProbeFirstSeen(const std::string &sig) {
        static std::unordered_map<std::string, long> counts;
        long &c = counts[sig];
        ++c;
        return c == 1;
    }

    void ProbeCall(const char *who, const char *name, tjs_int n,
                   tTJSVariant **p) {
        std::string sig(name);
        sig += '|';
        sig += std::to_string(static_cast<long long>(n));
        for(tjs_int i = 0; i < n; ++i) {
            sig += ',';
            sig += ProbeTypeName(p ? p[i] : nullptr);
        }
        if(!ProbeFirstSeen(sig))
            return;
        std::string args;
        const tjs_int lim = (n < 10) ? n : 10;
        for(tjs_int i = 0; i < lim; ++i) {
            if(i)
                args += ", ";
            args += ProbeValue(p ? p[i] : nullptr);
        }
        if(n > lim)
            args += ", ...";
        spdlog::info("[probe] {}.{}(n={}) args=[{}]", who, name,
                     static_cast<int>(n), args);
    }

} // namespace

#define KRKR_PROBE_TJS(who, name, n, p) ProbeCall(who, name, n, p)
#else
#define KRKR_PROBE_TJS(who, name, n, p) ((void)0)
#endif

namespace {

    inline tjs_int ToInt(const tTJSVariant &v, tjs_int fallback = 0) {
        switch(v.Type()) {
            case tvtInteger:
                return static_cast<tjs_int>(v);
            case tvtReal:
                return static_cast<tjs_int>(static_cast<tjs_real>(v));
            default:
                return fallback;
        }
    }

    inline tjs_int NormalizeExtent(tjs_int v, tjs_int fb) {
        return v > 0 ? v : fb;
    }

    using ModuleName = std::basic_string<tjs_char>;

    inline ModuleName NormalizeModuleName(tjs_int n, tTJSVariant **p) {
        if(n <= 0 || !p || !p[0] || p[0]->Type() == tvtVoid)
            return TJS_W("live2d");
        ttstr raw(*p[0]);
        ModuleName out(raw.c_str());
        for(auto &ch : out)
            if(ch >= 'A' && ch <= 'Z')
                ch += 32;
        if(out.empty())
            out = TJS_W("live2d");
        return out;
    }

    inline void SetResultObject(tTJSVariant *r, iTJSDispatch2 *o) {
        if(r && o)
            *r = tTJSVariant(o, o);
    }

    inline void SetObjectProperty(iTJSDispatch2 *o, const tjs_char *n,
                                  const tTJSVariant &v) {
        if(!o || !n)
            return;
        tTJSVariant copy(v);
        o->PropSet(TJS_MEMBERENSURE, n, nullptr, &copy, o);
    }

    inline void SetObjectMethod(iTJSDispatch2 *o, const tjs_char *n,
                                tTJSNativeClassMethodCallback cb) {
        if(!o || !n || !cb)
            return;
        iTJSDispatch2 *m = TJSCreateNativeClassMethod(cb);
        if(!m)
            return;
        tTJSVariant v(m, m);
        o->PropSet(TJS_MEMBERENSURE, n, nullptr, &v, o);
        m->Release();
    }

    // ---------------------------------------------------------------------------
    // KTX1 texture loader — uploads compressed or uncompressed KTX to GL
    // ---------------------------------------------------------------------------
    struct KtxHeader {
        uint8_t identifier[12];
        uint32_t endianness;
        uint32_t glType;
        uint32_t glTypeSize;
        uint32_t glFormat;
        uint32_t glInternalFormat;
        uint32_t glBaseInternalFormat;
        uint32_t pixelWidth;
        uint32_t pixelHeight;
        uint32_t pixelDepth;
        uint32_t numberOfArrayElements;
        uint32_t numberOfFaces;
        uint32_t numberOfMipmapLevels;
        uint32_t bytesOfKeyValueData;
    };

    // ---------------------------------------------------------------------------
    // Software BC7 (BPTC) RGBA8 decoder for GL_COMPRESSED_RGBA_BPTC_UNORM
    // Reference: Khronos Data Format Specification §17, Microsoft BC7 Format
    // ---------------------------------------------------------------------------

    struct BC7ModeInfo {
        uint8_t ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2;
    };
    static const BC7ModeInfo kBC7Mode[8] = {
        { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 }, { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
        { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 }, { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
        { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 }, { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
        { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 }, { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
    };

    static const uint16_t kBC7P2[64] = {
        0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80,
        0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000,
        0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE,
        0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C,
        0xAAAA, 0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696, 0xA55A,
        0x73CE, 0x13C8, 0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660,
        0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6, 0x639C,
        0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22,
    };

    static const uint32_t kBC7P3[64] = {
        0xAA685050, 0x6A5A5040, 0x5A5A4200, 0x5450A0A8, 0xA5A50000, 0xA0A05050,
        0x5555A0A0, 0x5A5A5050, 0xAA550000, 0xAA555500, 0xAAAA5500, 0x90906090,
        0x94949494, 0xA4A4A4A4, 0xA9A59450, 0x2A0A4250, 0xA5945040, 0x0A425054,
        0xA5A5A500, 0x55A0A0A0, 0xA8A85454, 0x6A6A4040, 0xA4A45000, 0x1A1A0500,
        0x0050A4A4, 0xAAA59090, 0x14696914, 0x69691400, 0xA08585A0, 0xAA821414,
        0x50A4A450, 0x6A5A0200, 0xA9A58000, 0x5090A0A0, 0xA8A09050, 0x24242424,
        0x00AA5500, 0x24924924, 0x24492424, 0xAA549524, 0x0A0A0A0A, 0xAA985002,
        0x0000A5A5, 0x96960000, 0xA5A5000A, 0xA0A0A5A5, 0x96000000, 0x40804080,
        0xA9A8A9A8, 0xAAAAAA44, 0x2A4A5254, 0x00000000, 0xAAAAAAAA, 0xAA0A0AAA,
        0x0A0A0A00, 0x0000AAAA, 0xAAA0AAA0, 0x0A0A0000, 0x0AA00AA0, 0xAA00AA00,
        0x00AA00AA, 0xA0A00A0A, 0x0A0A0000, 0xAAAA0000,
    };

    static const uint8_t kBC7A2[64] = {
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
        15, 2,  8,  2,  2,  8,  8,  15, 2,  8,  2,  2,  8,  8,  2,  2,
        15, 15, 6,  8,  2,  8,  15, 15, 2,  8,  2,  2,  2,  15, 15, 6,
        6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2,  15,
    };
    static const uint8_t kBC7A3a[64] = {
        3, 3,  15, 15, 8, 3,  15, 15, 8,  8,  6,  6,  6,  5,  3,  3,
        3, 3,  8,  15, 3, 3,  6,  10, 5,  8,  8,  6,  8,  5,  15, 15,
        8, 15, 3,  5,  6, 10, 8,  15, 15, 3,  15, 5,  15, 15, 15, 15,
        3, 15, 5,  5,  5, 8,  5,  10, 5,  10, 8,  13, 15, 12, 3,  3,
    };
    static const uint8_t kBC7A3b[64] = {
        15, 8, 8,  3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,
        15, 8, 15, 3,  15, 8,  15, 8,  3,  15, 6,  10, 15, 15, 10, 8,
        15, 3, 15, 10, 10, 8,  9,  10, 6,  15, 8,  15, 3,  6,  6,  8,
        15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8,
    };

    static const uint8_t kBC7W2[4] = { 0, 21, 43, 64 };
    static const uint8_t kBC7W3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
    static const uint8_t kBC7W4[16] = { 0,  4,  9,  13, 17, 21, 26, 30,
                                        34, 38, 43, 47, 51, 55, 60, 64 };

    static inline uint8_t bc7Lerp(int e0, int e1, int w) {
        return static_cast<uint8_t>(((64 - w) * e0 + w * e1 + 32) >> 6);
    }

    static void DecodeBC7Block(const uint8_t *src, uint8_t out[4][4][4]) {
        uint64_t lo, hi;
        std::memcpy(&lo, src, 8);
        std::memcpy(&hi, src + 8, 8);

        uint32_t modeBits = static_cast<uint32_t>(lo) & 0xFF;
        if(!modeBits) {
            std::memset(out, 0, 64);
            return;
        }
        int mode = __builtin_ctz(modeBits);

        int bp = mode + 1;
        auto rd = [&](int n) -> uint32_t {
            if(!n)
                return 0;
            uint32_t v;
            if(bp < 64) {
                v = static_cast<uint32_t>(lo >> bp);
                if(bp + n > 64)
                    v |= static_cast<uint32_t>(hi << (64 - bp));
            } else {
                v = static_cast<uint32_t>(hi >> (bp - 64));
            }
            bp += n;
            return v & ((1u << n) - 1);
        };

        const BC7ModeInfo &m = kBC7Mode[mode];
        int part = rd(m.pb), rot = rd(m.rb), isel = rd(m.isb);

        int ep[3][2][4] = {};
        for(int c = 0; c < 3; c++)
            for(int s = 0; s < m.ns; s++) {
                ep[s][0][c] = rd(m.cb);
                ep[s][1][c] = rd(m.cb);
            }
        if(m.ab)
            for(int s = 0; s < m.ns; s++) {
                ep[s][0][3] = rd(m.ab);
                ep[s][1][3] = rd(m.ab);
            }

        int numCh = m.ab ? 4 : 3;
        if(m.epb) {
            for(int s = 0; s < m.ns; s++)
                for(int e = 0; e < 2; e++) {
                    int pb = rd(1);
                    for(int c = 0; c < numCh; c++)
                        ep[s][e][c] = (ep[s][e][c] << 1) | pb;
                }
        } else if(m.spb) {
            for(int s = 0; s < m.ns; s++) {
                int pb = rd(1);
                for(int e = 0; e < 2; e++)
                    for(int c = 0; c < numCh; c++)
                        ep[s][e][c] = (ep[s][e][c] << 1) | pb;
            }
        }

        int cbits = m.cb + ((m.epb || m.spb) ? 1 : 0);
        int abits = m.ab ? m.ab + ((m.epb || m.spb) ? 1 : 0) : 0;
        for(int s = 0; s < m.ns; s++)
            for(int e = 0; e < 2; e++) {
                for(int c = 0; c < 3; c++)
                    ep[s][e][c] = (ep[s][e][c] << (8 - cbits)) |
                        (ep[s][e][c] >> (2 * cbits - 8));
                if(m.ab)
                    ep[s][e][3] = (ep[s][e][3] << (8 - abits)) |
                        (ep[s][e][3] >> (2 * abits - 8));
                else
                    ep[s][e][3] = 255;
            }

        int anchors[3] = { 0, 0, 0 };
        if(m.ns >= 2)
            anchors[1] = kBC7A2[part];
        if(m.ns >= 3) {
            anchors[1] = kBC7A3a[part];
            anchors[2] = kBC7A3b[part];
        }

        const uint8_t *wt1 = (m.ib == 2) ? kBC7W2
            : (m.ib == 3)                ? kBC7W3
                                         : kBC7W4;
        int idx1[16];
        for(int i = 0; i < 16; i++) {
            int sub = 0;
            if(m.ns == 2)
                sub = (kBC7P2[part] >> i) & 1;
            else if(m.ns == 3)
                sub = (kBC7P3[part] >> (2 * i)) & 3;
            bool isAnc = (i == anchors[sub]);
            idx1[i] = rd(m.ib - (isAnc ? 1 : 0));
        }

        int idx2[16] = {};
        const uint8_t *wt2 = nullptr;
        if(m.ib2) {
            wt2 = (m.ib2 == 2) ? kBC7W2 : (m.ib2 == 3) ? kBC7W3 : kBC7W4;
            for(int i = 0; i < 16; i++)
                idx2[i] = rd(m.ib2 - (i == 0 ? 1 : 0));
        }

        for(int i = 0; i < 16; i++) {
            int px = i & 3, py = i >> 2;
            int sub = 0;
            if(m.ns == 2)
                sub = (kBC7P2[part] >> i) & 1;
            else if(m.ns == 3)
                sub = (kBC7P3[part] >> (2 * i)) & 3;

            int w1 = wt1[idx1[i]];
            uint8_t r = bc7Lerp(ep[sub][0][0], ep[sub][1][0], w1);
            uint8_t g = bc7Lerp(ep[sub][0][1], ep[sub][1][1], w1);
            uint8_t b = bc7Lerp(ep[sub][0][2], ep[sub][1][2], w1);
            uint8_t a = bc7Lerp(ep[sub][0][3], ep[sub][1][3], w1);

            if(m.ib2) {
                int w2v = wt2[idx2[i]];
                if(isel == 0)
                    a = bc7Lerp(ep[sub][0][3], ep[sub][1][3], w2v);
                else {
                    r = bc7Lerp(ep[sub][0][0], ep[sub][1][0], w2v);
                    g = bc7Lerp(ep[sub][0][1], ep[sub][1][1], w2v);
                    b = bc7Lerp(ep[sub][0][2], ep[sub][1][2], w2v);
                }
            }

            if(rot == 1) {
                uint8_t t = a;
                a = r;
                r = t;
            } else if(rot == 2) {
                uint8_t t = a;
                a = g;
                g = t;
            } else if(rot == 3) {
                uint8_t t = a;
                a = b;
                b = t;
            }

            out[py][px][0] = r;
            out[py][px][1] = g;
            out[py][px][2] = b;
            out[py][px][3] = a;
        }
    }

    static bool DecodeBPTC_RGBA(const uint8_t *data, uint32_t dataSize,
                                uint32_t w, uint32_t h,
                                std::vector<uint8_t> &outRGBA) {
        uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        uint32_t expected = bw * bh * 16;
        if(dataSize < expected)
            return false;

        outRGBA.resize(static_cast<size_t>(w) * h * 4);

        auto decodeRows = [&](uint32_t rowStart, uint32_t rowEnd) {
            for(uint32_t by = rowStart; by < rowEnd; by++) {
                const uint8_t *block = data + static_cast<size_t>(by) * bw * 16;
                for(uint32_t bx = 0; bx < bw; bx++, block += 16) {
                    uint8_t rgba[4][4][4];
                    DecodeBC7Block(block, rgba);
                    uint32_t px0 = bx * 4, py0 = by * 4;
                    if(px0 + 4 <= w && py0 + 4 <= h) {
                        for(int y = 0; y < 4; y++) {
                            uint32_t dstY = py0 + y;
                            size_t off =
                                (static_cast<size_t>(dstY) * w + px0) * 4;
                            std::memcpy(&outRGBA[off], rgba[y], 16);
                        }
                    } else {
                        for(int y = 0; y < 4; y++) {
                            uint32_t srcY = py0 + y;
                            if(srcY >= h)
                                break;
                            uint32_t dstY = srcY;
                            for(int x = 0; x < 4; x++) {
                                uint32_t px = px0 + x;
                                if(px >= w)
                                    break;
                                size_t off =
                                    (static_cast<size_t>(dstY) * w + px) * 4;
                                std::memcpy(&outRGBA[off], rgba[y][x], 4);
                            }
                        }
                    }
                }
            }
        };

        constexpr uint32_t kMinRowsForThreading = 64;
        unsigned numThreads = 1;
        if(bh > kMinRowsForThreading) {
            unsigned hw = std::thread::hardware_concurrency();
            numThreads = std::min(std::max(hw, 1u), 8u);
        }

        auto t0 = std::chrono::steady_clock::now();

        if(numThreads <= 1) {
            decodeRows(0, bh);
        } else {
            std::vector<std::thread> threads;
            threads.reserve(numThreads);
            uint32_t rowsPerThread = bh / numThreads;
            uint32_t remainder = bh % numThreads;
            uint32_t start = 0;
            for(unsigned t = 0; t < numThreads; t++) {
                uint32_t end = start + rowsPerThread + (t < remainder ? 1 : 0);
                threads.emplace_back(decodeRows, start, end);
                start = end;
            }
            for(auto &t : threads)
                t.join();
        }

        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0)
                      .count();
        GLES_LOGI("BC7 decode %ux%u: %lld ms (%u threads, %u block-rows)", w, h,
                  (long long)ms, numThreads, bh);
        spdlog::info("krkrgles: BC7 decode {}x{}: {} ms ({} threads)", w, h, ms,
                     numThreads);

        return true;
    }

    static bool IsBPTCFormat(GLenum fmt) { return fmt == 0x8E8C; }

    // ---------------------------------------------------------------------------
    // Software ETC2 + EAC RGBA8 decoder (fallback when the GPU lacks ETC2/EAC)
    // ---------------------------------------------------------------------------

    const int kETC2ModifierTable[8][4] = {
        { 2, 8, -2, -8 },       { 5, 17, -5, -17 },    { 9, 29, -9, -29 },
        { 13, 42, -13, -42 },   { 18, 60, -18, -60 },  { 24, 80, -24, -80 },
        { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
    };
    const int kEACModifierTable[16][8] = {
        { -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 },
        { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
        { -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 },
        { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
        { -2, -6, -8, -10, 1, 5, 7, 9 },  { -2, -5, -8, -10, 1, 4, 7, 9 },
        { -2, -4, -8, -10, 1, 3, 7, 9 },  { -2, -5, -7, -10, 1, 4, 6, 9 },
        { -3, -4, -7, -10, 2, 3, 6, 9 },  { -1, -2, -3, -10, 0, 1, 2, 9 },
        { -4, -6, -8, -9, 3, 5, 7, 8 },   { -3, -5, -7, -9, 2, 4, 6, 8 }
    };

    inline uint8_t clamp255(int v) {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }

    inline int extend4to8(int v) { return (v << 4) | v; }
    inline int extend5to8(int v) { return (v << 3) | (v >> 2); }
    inline int extend6to8(int v) { return (v << 2) | (v >> 4); }
    inline int extend7to8(int v) { return (v << 1) | (v >> 6); }

    const int kETC2DistTable[8] = { 3, 6, 11, 16, 23, 32, 47, 64 };

    void DecodeETC2Block(const uint8_t *src, uint8_t rgba[4][4][4]) {
        uint32_t hi = (static_cast<uint32_t>(src[0]) << 24) |
            (static_cast<uint32_t>(src[1]) << 16) |
            (static_cast<uint32_t>(src[2]) << 8) |
            static_cast<uint32_t>(src[3]);
        uint32_t lo = (static_cast<uint32_t>(src[4]) << 24) |
            (static_cast<uint32_t>(src[5]) << 16) |
            (static_cast<uint32_t>(src[6]) << 8) |
            static_cast<uint32_t>(src[7]);

        bool diffBit = (hi >> 1) & 1;
        bool flipBit = hi & 1;

        if(!diffBit) {
            // Individual mode
            int r1 = extend4to8((hi >> 28) & 0xF);
            int r2 = extend4to8((hi >> 24) & 0xF);
            int g1 = extend4to8((hi >> 20) & 0xF);
            int g2 = extend4to8((hi >> 16) & 0xF);
            int b1 = extend4to8((hi >> 12) & 0xF);
            int b2 = extend4to8((hi >> 8) & 0xF);
            int tbl1 = (hi >> 5) & 0x7;
            int tbl2 = (hi >> 2) & 0x7;

            for(int y = 0; y < 4; ++y)
                for(int x = 0; x < 4; ++x) {
                    int pixelIdx = x * 4 + y;
                    int msb = (lo >> (31 - pixelIdx)) & 1;
                    int lsb = (lo >> (15 - pixelIdx)) & 1;
                    int idx = (msb << 1) | lsb;
                    bool sb2 = flipBit ? (y >= 2) : (x >= 2);
                    int mod = kETC2ModifierTable[sb2 ? tbl2 : tbl1][idx];
                    rgba[y][x][0] = clamp255((sb2 ? r2 : r1) + mod);
                    rgba[y][x][1] = clamp255((sb2 ? g2 : g1) + mod);
                    rgba[y][x][2] = clamp255((sb2 ? b2 : b1) + mod);
                    rgba[y][x][3] = 255;
                }
            return;
        }

        // Differential mode: check for overflow → T, H, or Planar
        int R = (hi >> 27) & 0x1F;
        int dR = static_cast<int>((hi >> 24) & 0x7);
        if(dR >= 4)
            dR -= 8;
        int G = (hi >> 19) & 0x1F;
        int dG = static_cast<int>((hi >> 16) & 0x7);
        if(dG >= 4)
            dG -= 8;
        int B = (hi >> 11) & 0x1F;
        int dB = static_cast<int>((hi >> 8) & 0x7);
        if(dB >= 4)
            dB -= 8;

        int R2c = R + dR, G2c = G + dG, B2c = B + dB;

        if(R2c < 0 || R2c > 31) {
            // --- T mode (etcpack: THUMB59T) ---
            // R1 = bits[58:57,56:55], G1 = bits[54:51], B1 = bits[50:47]
            // R2 = bits[46:43], G2 = bits[42:39], B2 = bits[38:35], d =
            // bits[34:32]
            int r1 = extend4to8((((src[0] >> 1) & 0x3) << 2) |
                                ((src[0] & 0x1) << 1) | ((src[1] >> 7) & 0x1));
            int g1 = extend4to8((src[1] >> 3) & 0xF);
            int b1 = extend4to8(((src[1] & 0x7) << 1) | ((src[2] >> 7) & 0x1));
            int r2 = extend4to8((src[2] >> 3) & 0xF);
            int g2 = extend4to8(((src[2] & 0x7) << 1) | ((src[3] >> 7) & 0x1));
            int b2 = extend4to8((src[3] >> 3) & 0xF);
            int distIdx = src[3] & 0x7;
            int dist = kETC2DistTable[distIdx];
            int paint[4][3] = { { r1, g1, b1 },
                                { clamp255(r2 + dist), clamp255(g2 + dist),
                                  clamp255(b2 + dist) },
                                { r2, g2, b2 },
                                { clamp255(r2 - dist), clamp255(g2 - dist),
                                  clamp255(b2 - dist) } };
            for(int y = 0; y < 4; ++y)
                for(int x = 0; x < 4; ++x) {
                    int pixelIdx = x * 4 + y;
                    int msb = (lo >> (31 - pixelIdx)) & 1;
                    int lsb = (lo >> (15 - pixelIdx)) & 1;
                    int idx = (msb << 1) | lsb;
                    rgba[y][x][0] = clamp255(paint[idx][0]);
                    rgba[y][x][1] = clamp255(paint[idx][1]);
                    rgba[y][x][2] = clamp255(paint[idx][2]);
                    rgba[y][x][3] = 255;
                }
            return;
        }

        if(G2c < 0 || G2c > 31) {
            // --- H mode (etcpack: THUMB58H) ---
            // Color0 = bits[57:46], Color1 = bits[45:34], d = bits[34:33]
            int r1_4 = ((src[0] & 0x3) << 2) | ((src[1] >> 6) & 0x3);
            int g1_4 = (src[1] >> 2) & 0xF;
            int b1_4 = ((src[1] & 0x3) << 2) | ((src[2] >> 6) & 0x3);
            int r2_4 = (src[2] >> 2) & 0xF;
            int g2_4 = ((src[2] & 0x3) << 2) | ((src[3] >> 6) & 0x3);
            int b2_4 = (src[3] >> 2) & 0xF;
            int r1 = extend4to8(r1_4), g1 = extend4to8(g1_4),
                b1 = extend4to8(b1_4);
            int r2 = extend4to8(r2_4), g2 = extend4to8(g2_4),
                b2 = extend4to8(b2_4);
            int distIdx = (src[3] >> 1) & 0x3;
            int packed0 = (r1_4 << 8) | (g1_4 << 4) | b1_4;
            int packed1 = (r2_4 << 8) | (g2_4 << 4) | b2_4;
            if(packed0 >= packed1)
                distIdx = (distIdx << 1) | 1;
            else
                distIdx = distIdx << 1;
            int dist = kETC2DistTable[distIdx];
            int paint[4][3] = { { clamp255(r1 + dist), clamp255(g1 + dist),
                                  clamp255(b1 + dist) },
                                { clamp255(r1 - dist), clamp255(g1 - dist),
                                  clamp255(b1 - dist) },
                                { clamp255(r2 + dist), clamp255(g2 + dist),
                                  clamp255(b2 + dist) },
                                { clamp255(r2 - dist), clamp255(g2 - dist),
                                  clamp255(b2 - dist) } };
            for(int y = 0; y < 4; ++y)
                for(int x = 0; x < 4; ++x) {
                    int pixelIdx = x * 4 + y;
                    int msb = (lo >> (31 - pixelIdx)) & 1;
                    int lsb = (lo >> (15 - pixelIdx)) & 1;
                    int idx = (msb << 1) | lsb;
                    rgba[y][x][0] = clamp255(paint[idx][0]);
                    rgba[y][x][1] = clamp255(paint[idx][1]);
                    rgba[y][x][2] = clamp255(paint[idx][2]);
                    rgba[y][x][3] = 255;
                }
            return;
        }

        if(B2c < 0 || B2c > 31) {
            // --- Planar mode (etcpack: Planar57) ---
            // RO=bits[62:57], GO=bits[54:48], BO=bits[44:39]
            // RH=bits[38:33], GH=bits[32:26], BH=bits[25:20]
            // RV=bits[19:14], GV=bits[13:7],  BV=bits[6:1]
            int RO = extend6to8((src[0] >> 1) & 0x3F);
            int GO = extend7to8(((src[0] & 0x1) << 6) | ((src[1] >> 2) & 0x3F));
            int BO = extend6to8(((src[2] & 0x1F) << 1) | ((src[3] >> 7) & 0x1));
            int RH = extend6to8((src[3] >> 1) & 0x3F);
            int GH = extend7to8(((src[3] & 0x1) << 6) | ((src[4] >> 2) & 0x3F));
            int BH = extend6to8(((src[4] & 0x3) << 4) | ((src[5] >> 4) & 0xF));
            int RV = extend6to8(((src[5] & 0xF) << 2) | ((src[6] >> 6) & 0x3));
            int GV = extend7to8(((src[6] & 0x3F) << 1) | ((src[7] >> 7) & 0x1));
            int BV = extend6to8((src[7] >> 1) & 0x3F);
            for(int y = 0; y < 4; ++y)
                for(int x = 0; x < 4; ++x) {
                    rgba[y][x][0] = clamp255(
                        (x * (RH - RO) + y * (RV - RO) + 4 * RO + 2) >> 2);
                    rgba[y][x][1] = clamp255(
                        (x * (GH - GO) + y * (GV - GO) + 4 * GO + 2) >> 2);
                    rgba[y][x][2] = clamp255(
                        (x * (BH - BO) + y * (BV - BO) + 4 * BO + 2) >> 2);
                    rgba[y][x][3] = 255;
                }
            return;
        }

        // --- Standard differential mode ---
        int r1 = extend5to8(R), g1 = extend5to8(G), b1 = extend5to8(B);
        int r2 = extend5to8(R2c), g2 = extend5to8(G2c), b2 = extend5to8(B2c);
        int tbl1 = (hi >> 5) & 0x7;
        int tbl2 = (hi >> 2) & 0x7;

        for(int y = 0; y < 4; ++y)
            for(int x = 0; x < 4; ++x) {
                int pixelIdx = x * 4 + y;
                int msb = (lo >> (31 - pixelIdx)) & 1;
                int lsb = (lo >> (15 - pixelIdx)) & 1;
                int idx = (msb << 1) | lsb;
                bool sb2 = flipBit ? (y >= 2) : (x >= 2);
                int mod = kETC2ModifierTable[sb2 ? tbl2 : tbl1][idx];
                rgba[y][x][0] = clamp255((sb2 ? r2 : r1) + mod);
                rgba[y][x][1] = clamp255((sb2 ? g2 : g1) + mod);
                rgba[y][x][2] = clamp255((sb2 ? b2 : b1) + mod);
                rgba[y][x][3] = 255;
            }
    }

    void DecodeEACBlock(const uint8_t *src, uint8_t alpha[4][4]) {
        int base = src[0];
        int multiplier = (src[1] >> 4) & 0xF;
        int tableIdx = src[1] & 0xF;
        if(multiplier == 0)
            multiplier = 1;

        uint64_t bits = 0;
        for(int i = 2; i < 8; ++i)
            bits = (bits << 8) | src[i];

        for(int i = 0; i < 16; ++i) {
            int shift = 45 - i * 3;
            int pixIdx = (bits >> shift) & 0x7;
            int val = base + kEACModifierTable[tableIdx][pixIdx] * multiplier;
            int x = i / 4, y = i % 4;
            alpha[y][x] = clamp255(val);
        }
    }

    bool DecodeETC2RGBA(const uint8_t *compData, uint32_t dataSize, uint32_t w,
                        uint32_t h, std::vector<uint8_t> &outRGBA) {
        uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        uint32_t expectedSize = bw * bh * 16;
        if(dataSize < expectedSize)
            return false;

        outRGBA.resize(static_cast<size_t>(w) * h * 4);
        const uint8_t *block = compData;

        for(uint32_t by = 0; by < bh; ++by) {
            for(uint32_t bx = 0; bx < bw; ++bx, block += 16) {
                uint8_t alpha[4][4];
                uint8_t rgba[4][4][4];
                DecodeEACBlock(block, alpha);
                DecodeETC2Block(block + 8, rgba);

                for(int y = 0; y < 4; ++y) {
                    uint32_t srcY = by * 4 + y;
                    if(srcY >= h)
                        break;
                    uint32_t dstY = srcY;
                    for(int x = 0; x < 4; ++x) {
                        uint32_t px = bx * 4 + x;
                        if(px >= w)
                            break;
                        size_t off = (static_cast<size_t>(dstY) * w + px) * 4;
                        outRGBA[off + 0] = rgba[y][x][0];
                        outRGBA[off + 1] = rgba[y][x][1];
                        outRGBA[off + 2] = rgba[y][x][2];
                        outRGBA[off + 3] = alpha[y][x];
                    }
                }
            }
        }
        return true;
    }

    bool DecodeETC2RGB(const uint8_t *compData, uint32_t dataSize, uint32_t w,
                       uint32_t h, std::vector<uint8_t> &outRGBA) {
        uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        uint32_t expectedSize = bw * bh * 8;
        if(dataSize < expectedSize)
            return false;

        outRGBA.resize(static_cast<size_t>(w) * h * 4);
        const uint8_t *block = compData;

        for(uint32_t by = 0; by < bh; ++by) {
            for(uint32_t bx = 0; bx < bw; ++bx, block += 8) {
                uint8_t rgba[4][4][4];
                DecodeETC2Block(block, rgba);
                for(int y = 0; y < 4; ++y) {
                    uint32_t srcY = by * 4 + y;
                    if(srcY >= h)
                        break;
                    uint32_t dstY = srcY;
                    for(int x = 0; x < 4; ++x) {
                        uint32_t px = bx * 4 + x;
                        if(px >= w)
                            break;
                        size_t off = (static_cast<size_t>(dstY) * w + px) * 4;
                        outRGBA[off + 0] = rgba[y][x][0];
                        outRGBA[off + 1] = rgba[y][x][1];
                        outRGBA[off + 2] = rgba[y][x][2];
                        outRGBA[off + 3] = 255;
                    }
                }
            }
        }
        return true;
    }

    const std::vector<GLint> &GetSupportedCompressedFormats() {
        static std::vector<GLint> cached;
        static bool inited = false;
        if(!inited) {
            GLint n = 0;
            glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &n);
            if(n > 0) {
                cached.resize(n);
                glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, cached.data());
            }
            inited = true;
        }
        return cached;
    }

    bool IsCompressedFormatSupported(GLenum internalFormat) {
        for(GLint f : GetSupportedCompressedFormats()) {
            if(static_cast<GLenum>(f) == internalFormat)
                return true;
        }
        return false;
    }

    bool IsETC2Format(GLenum fmt) {
        return fmt == 0x9274 || fmt == 0x9278 || // RGB8, RGBA8 (EAC)
            fmt == 0x9275 || fmt == 0x9279 || // SRGB8, SRGB8_A8 (EAC)
            fmt == 0x9276 || fmt == 0x9277; // punchthrough alpha
    }

    bool IsETC2WithAlpha(GLenum fmt) { return fmt == 0x9278 || fmt == 0x9279; }

} // end anonymous namespace

extern "C" GLuint LoadKtxTexture(const uint8_t *data, size_t dataSize) {
    if(dataSize < sizeof(KtxHeader))
        return 0;
    const KtxHeader *hdr = reinterpret_cast<const KtxHeader *>(data);

    static const uint8_t KTX_MAGIC[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31,
                                           0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    if(std::memcmp(hdr->identifier, KTX_MAGIC, 12) != 0)
        return 0;

    // glGetError() 返回的是"自上次查询以来的**第一个**错误"。进入本函数之前，系统里
    // 可能还压着别处（GPU 层脚本经 GLESAdaptor 的那批调用）产生的旧错误；不先清掉，
    // 下面按"有没有错误"判成败就会把它算到本次上传头上。
    // 真机实测（2026-09-16，G2+kag）：每个模型**第一次**加载都报
    // `KTX upload GL error 0x0501`、同一份数据**第二次**加载却成功 —— 于是纹理被
    // 误删，krkrlive2d 只能用 1x1 白色占位，立绘退化成黑白多边形方块、没有纹理。
    while(glGetError() != GL_NO_ERROR) {
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if(!tex)
        return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const bool compressed = (hdr->glType == 0 && hdr->glFormat == 0);
    const uint8_t *ptr = data + sizeof(KtxHeader) + hdr->bytesOfKeyValueData;
    uint32_t w = hdr->pixelWidth, h = hdr->pixelHeight;
    uint32_t levels = hdr->numberOfMipmapLevels;
    if(levels == 0)
        levels = 1;

    GLES_LOGI("KTX %ux%u internalFmt=0x%04X fmt=0x%04X type=0x%04X "
              "compressed=%d levels=%u",
              w, h, hdr->glInternalFormat, hdr->glFormat, hdr->glType,
              compressed, levels);
    spdlog::info("krkrgles: KTX {}x{} internalFmt=0x{:04X} fmt=0x{:04X} "
                 "type=0x{:04X} compressed={} levels={}",
                 w, h, hdr->glInternalFormat, hdr->glFormat, hdr->glType,
                 compressed, levels);

    enum DecodeMethod { DECODE_NONE, DECODE_ETC2, DECODE_BPTC };
    DecodeMethod decodeMeth = DECODE_NONE;

    if(compressed && IsBPTCFormat(hdr->glInternalFormat) &&
       !IsCompressedFormatSupported(hdr->glInternalFormat)) {
        GLES_LOGI(
            "BPTC format 0x%04X not supported by GPU -> BC7 software decode",
            hdr->glInternalFormat);
        decodeMeth = DECODE_BPTC;
    } else if(compressed && IsBPTCFormat(hdr->glInternalFormat)) {
        GLES_LOGI("BPTC format 0x%04X supported by GPU -> hardware path",
                  hdr->glInternalFormat);
    } else if(compressed && IsETC2Format(hdr->glInternalFormat) &&
              !IsCompressedFormatSupported(hdr->glInternalFormat)) {
        GLES_LOGI("ETC2 format 0x%04X not supported by GPU -> software decode "
                  "(withAlpha=%d)",
                  hdr->glInternalFormat,
                  IsETC2WithAlpha(hdr->glInternalFormat));
        decodeMeth = DECODE_ETC2;
    } else if(compressed && IsETC2Format(hdr->glInternalFormat)) {
        GLES_LOGI("ETC2 format 0x%04X supported by GPU -> hardware path",
                  hdr->glInternalFormat);
    } else if(compressed &&
              !IsCompressedFormatSupported(hdr->glInternalFormat)) {
        GLES_LOGW("compressed format 0x%04X not supported, texture skipped",
                  hdr->glInternalFormat);
        spdlog::warn("krkrgles: compressed format 0x{:04X} not supported",
                     hdr->glInternalFormat);
        glDeleteTextures(1, &tex);
        return 0;
    }

    if(decodeMeth != DECODE_NONE) {
        uint32_t mw = w, mh = h;
        for(uint32_t level = 0; level < levels; ++level) {
            if(ptr + 4 > data + dataSize)
                break;
            uint32_t imageSize;
            std::memcpy(&imageSize, ptr, 4);
            ptr += 4;
            if(ptr + imageSize > data + dataSize)
                break;

            std::vector<uint8_t> decoded;
            bool ok = false;
            if(decodeMeth == DECODE_BPTC)
                ok = DecodeBPTC_RGBA(ptr, imageSize, mw, mh, decoded);
            else if(IsETC2WithAlpha(hdr->glInternalFormat))
                ok = DecodeETC2RGBA(ptr, imageSize, mw, mh, decoded);
            else
                ok = DecodeETC2RGB(ptr, imageSize, mw, mh, decoded);

            if(ok) {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA,
                             static_cast<GLsizei>(mw), static_cast<GLsizei>(mh),
                             0, GL_RGBA, GL_UNSIGNED_BYTE, decoded.data());
                GLES_LOGI("  level %u: %ux%u decoded OK (dataSize=%u "
                          "decodedSize=%zu)",
                          level, mw, mh, imageSize, decoded.size());
                // 逐级归因：出问题时报出是哪一级，而不是最后一把抓。
                const GLenum lvlErr = glGetError();
                if(lvlErr != GL_NO_ERROR) {
                    GLES_LOGW("  level %u: %ux%u upload GL error 0x%04X", level,
                              mw, mh, lvlErr);
                    spdlog::warn("krkrgles: KTX level {} ({}x{}) 上传错误 0x{:04X}",
                                 level, mw, mh, static_cast<unsigned>(lvlErr));
                }
            } else {
                GLES_LOGW("  level %u: %ux%u decode FAILED (dataSize=%u)",
                          level, mw, mh, imageSize);
            }
            ptr += (imageSize + 3) & ~3u;
            mw = (mw > 1) ? mw / 2 : 1;
            mh = (mh > 1) ? mh / 2 : 1;
        }
    } else {
        uint32_t mw = w, mh = h;
        for(uint32_t level = 0; level < levels; ++level) {
            if(ptr + 4 > data + dataSize)
                break;
            uint32_t imageSize;
            std::memcpy(&imageSize, ptr, 4);
            ptr += 4;
            if(ptr + imageSize > data + dataSize)
                break;

            if(compressed) {
                glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                                       hdr->glInternalFormat,
                                       static_cast<GLsizei>(mw),
                                       static_cast<GLsizei>(mh), 0,
                                       static_cast<GLsizei>(imageSize), ptr);
            } else {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                             static_cast<GLint>(hdr->glInternalFormat),
                             static_cast<GLsizei>(mw), static_cast<GLsizei>(mh),
                             0, hdr->glFormat, hdr->glType, ptr);
            }
            ptr += (imageSize + 3) & ~3u;
            const GLenum lvlErr = glGetError();
            if(lvlErr != GL_NO_ERROR) {
                GLES_LOGW("  level %u: %ux%u upload GL error 0x%04X", level, mw,
                          mh, lvlErr);
                spdlog::warn("krkrgles: KTX level {} ({}x{}) 上传错误 0x{:04X}",
                             level, mw, mh, static_cast<unsigned>(lvlErr));
            }
            mw = (mw > 1) ? mw / 2 : 1;
            mh = (mh > 1) ? mh / 2 : 1;
        }
    }

    // 成功判据是"level 0 确实上去了"，**不是**"没有任何 GL 错误"：后者会把别处残留
    // 的错误算进来，把一张已经上传成功的纹理删掉（真机实测：同一份数据第一次加载
    // 报 0x0501 被删、第二次却成功）。这里直接向 GL 问 level-0 的实际尺寸。
    GLint baseW = 0, baseH = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &baseW);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &baseH);
    if(baseW <= 0 || baseH <= 0) {
        GLES_LOGW("KTX base level missing (%dx%d), deleting texture", baseW,
                  baseH);
        spdlog::warn("krkrgles: KTX level 0 缺失（{}x{}），纹理作废", baseW,
                     baseH);
        glDeleteTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        return 0;
    }

    GLES_LOGI("KTX texture loaded OK: texId=%u", tex);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ---------------------------------------------------------------------------
// PNG texture loader — decode from memory, upload as RGBA8
// ---------------------------------------------------------------------------
namespace {
    struct PngMemReader {
        const uint8_t *data;
        size_t size;
        size_t offset;
    };
    static void pngReadMemory(png_structp png, png_bytep out,
                              png_size_t count) {
        auto *r = static_cast<PngMemReader *>(png_get_io_ptr(png));
        if(r->offset + count > r->size) {
            png_error(png, "read past end");
            return;
        }
        std::memcpy(out, r->data + r->offset, count);
        r->offset += count;
    }
} // namespace

extern "C" GLuint LoadPngTexture(const uint8_t *data, size_t dataSize) {
    if(dataSize < 8 || png_sig_cmp(data, 0, 8) != 0)
        return 0;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                             nullptr, nullptr);
    if(!png)
        return 0;
    png_infop info = png_create_info_struct(png);
    if(!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return 0;
    }

    std::vector<uint8_t> pixels;
    uint32_t w = 0, h = 0;

    if(setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return 0;
    }

    PngMemReader reader{ data, dataSize, 0 };
    png_set_read_fn(png, &reader, pngReadMemory);
    png_read_info(png, info);

    w = png_get_image_width(png, info);
    h = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    if(bitDepth == 16)
        png_set_strip_16(png);
    if(colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if(colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if(png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if(colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
       colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if(colorType == PNG_COLOR_TYPE_GRAY ||
       colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    size_t rowBytes = png_get_rowbytes(png, info);
    pixels.resize(rowBytes * h);
    std::vector<png_bytep> rows(h);
    for(uint32_t y = 0; y < h; ++y)
        rows[y] = pixels.data() + y * rowBytes;
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if(!tex)
        return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w),
                 static_cast<GLsizei>(h), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());

    GLenum err = glGetError();
    if(err != GL_NO_ERROR) {
        GLES_LOGW("PNG upload GL error 0x%04X", err);
        glDeleteTextures(1, &tex);
        return 0;
    }

    GLES_LOGI("PNG texture loaded OK: %ux%u texId=%u", w, h, tex);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

namespace {

    // ---------------------------------------------------------------------------
    // FBO manager — offscreen render target for Live2D
    // ---------------------------------------------------------------------------
    class OffscreenFBO {
    public:
        bool EnsureSize(GLsizei w, GLsizei h) {
            if(fbo_ && w == width_ && h == height_)
                return true;
            Destroy();
            width_ = w;
            height_ = h;

            glGenFramebuffers(1, &fbo_);
            glGenTextures(1, &colorTex_);
            glBindTexture(GL_TEXTURE_2D, colorTex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, colorTex_, 0);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            if(status != GL_FRAMEBUFFER_COMPLETE) {
                spdlog::error("krkrgles: FBO incomplete (status=0x{:04X})",
                              status);
                Destroy();
                return false;
            }
            spdlog::debug("krkrgles: FBO created {}x{}", w, h);
            return true;
        }

        void Bind(GLint currentFbo = -1) {
            if(!fbo_)
                return;
            if(currentFbo >= 0)
                prevFbo_ = currentFbo;
            else
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glViewport(0, 0, width_, height_);
            glClearColor(0.f, 0.f, 0.f, 0.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        void Unbind() {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
        }

        GLint GetPrevFbo() const { return prevFbo_; }

        bool ReadPixels(std::vector<uint8_t> &buf) {
            if(!fbo_)
                return false;
            buf.resize(static_cast<size_t>(width_) * height_ * 4);
            GLint prev;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                         buf.data());
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev));
            return true;
        }

        void Destroy() {
            if(colorTex_) {
                glDeleteTextures(1, &colorTex_);
                colorTex_ = 0;
            }
            if(fbo_) {
                glDeleteFramebuffers(1, &fbo_);
                fbo_ = 0;
            }
            width_ = height_ = 0;
        }

        GLuint GetFBO() const { return fbo_; }
        GLsizei GetWidth() const { return width_; }
        GLsizei GetHeight() const { return height_; }

        ~OffscreenFBO() { Destroy(); }

    private:
        GLuint fbo_ = 0;
        GLuint colorTex_ = 0;
        GLsizei width_ = 0, height_ = 0;
        GLint prevFbo_ = 0;
    };

} // namespace

// ---------------------------------------------------------------------------
// Find the first Layer object among the callback parameters.
// The game may pass (intFlag, layer, ...) instead of (layer).
// ---------------------------------------------------------------------------
static iTJSDispatch2 *FindLayerInParams(tjs_int n, tTJSVariant **p) {
    if(!p)
        return nullptr;
    for(tjs_int i = 0; i < n; ++i) {
        if(p[i] && p[i]->Type() == tvtObject) {
            iTJSDispatch2 *obj = p[i]->AsObjectNoAddRef();
            if(!obj)
                continue;
            tTJSVariant test;
            if(TJS_SUCCEEDED(
                   obj->PropGet(0, TJS_W("imageWidth"), nullptr, &test, obj)))
                return obj;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GPU fast path: blit FBO → Layer's native GL texture via glBlitFramebuffer.
// Returns true if GPU path was used, false if not available.
// Uses native C++ calls instead of TJS dispatch for performance.
// ---------------------------------------------------------------------------
static bool CopyFBOToLayerGPU(GLuint srcFbo, GLsizei srcW, GLsizei srcH,
                              tTJSNI_Layer *layerNI, GLint prevFbo) {
    tTVPBaseTexture *img = layerNI->GetMainImage();
    if(!img)
        return false;
    auto *tex = img->GetTexture();
    if(!tex)
        return false;

    GLuint layerTexId = tex->GetNativeGLTextureId();
    if(layerTexId == 0)
        return false;

    GLsizei intW = static_cast<GLsizei>(tex->GetInternalWidth());
    GLsizei intH = static_cast<GLsizei>(tex->GetInternalHeight());
    if(intW <= 0 || intH <= 0)
        return false;

    GLsizei layerW = static_cast<GLsizei>(layerNI->GetWidth());
    GLsizei layerH = static_cast<GLsizei>(layerNI->GetHeight());

    static GLuint s_dstFbo = 0;
    static GLuint s_lastAttachedTex = 0;

    if(!s_dstFbo)
        glGenFramebuffers(1, &s_dstFbo);

    if(layerTexId != s_lastAttachedTex) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_dstFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, layerTexId, 0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
           GL_FRAMEBUFFER_COMPLETE) {
            s_lastAttachedTex = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            return false;
        }
        s_lastAttachedTex = layerTexId;
    } else {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_dstFbo);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);

    GLsizei blitW = (layerW < srcW) ? layerW : srcW;
    GLsizei blitH = (layerH < srcH) ? layerH : srcH;

    // 清掉此前积压的错误位，下面那次 glGetError 才能**只**反映这次 blit。
    while(glGetError() != GL_NO_ERROR) {
    }

#if defined(__ANDROID__)
    // Android: no Y flip so Live2D appears right-side up
    glBlitFramebuffer(0, 0, blitW, blitH, 0, 0, blitW, blitH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
#else
    glBlitFramebuffer(0, 0, blitW, blitH, 0, blitH, blitW, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif

    // glBlitFramebuffer 在 GLES3 下会因**读写格式不兼容**静默失败（只置错误位、
    // 什么都不画）。此前这里无条件 return true，于是 GPU 路径"成功"了、CPU 兜底
    // 永远轮不到，画面空着却一条日志都没有——"有声音、整屏黑、零报错"正是这种
    // 静默失败最难查的地方。这里显式检查，失败就让位给 CPU 路径。
    const GLenum blitErr = glGetError();
    if(blitErr != GL_NO_ERROR) {
#if defined(KRKR_RENDER_PROBE)
        static bool s_blitFailLogged = false;
        if(!s_blitFailLogged) {
            s_blitFailLogged = true;
            spdlog::warn("krkrgles: glBlitFramebuffer failed (err=0x{:04X}, "
                         "src={}x{} layer={}x{} internal={}x{}) — CPU fallback",
                         static_cast<unsigned>(blitErr), static_cast<int>(srcW),
                         static_cast<int>(srcH), static_cast<int>(layerW),
                         static_cast<int>(layerH), static_cast<int>(intW),
                         static_cast<int>(intH));
        }
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        return false;
    }

#if defined(KRKR_RENDER_PROBE)
    {
        static bool s_gpuOkLogged = false;
        if(!s_gpuOkLogged) {
            s_gpuOkLogged = true;
            spdlog::info("[probe] krkrgles: CopyFBOToLayer GPU ok src={}x{} "
                         "dst={}x{} internal={}x{}",
                         static_cast<int>(srcW), static_cast<int>(srcH),
                         static_cast<int>(layerW), static_cast<int>(layerH),
                         static_cast<int>(intW), static_cast<int>(intH));
        }
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    tex->InvalidatePixelCache();
    layerNI->SetImageModified(true);

    tTVPRect rc(0, 0, blitW, blitH);
    layerNI->Update(rc);
    return true;
}

// ---------------------------------------------------------------------------
// Convert RGBA (bottom-up) source buffer to BGRA (top-down) layer buffer.
// ---------------------------------------------------------------------------
static void ConvertRGBA_to_BGRA(const uint8_t *src, GLsizei srcW, GLsizei srcH,
                                uint8_t *dst, tjs_int pitch, tjs_int copyW,
                                tjs_int copyH) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const int simdWidth = 16;
    const int simdCopyW = copyW & ~(simdWidth - 1);
#endif
    for(tjs_int y = 0; y < copyH; ++y) {
#if defined(__ANDROID__)
        const size_t srcRowIdx =
            static_cast<size_t>(y) * srcW * 4; // no Y flip on Android
#else
        const size_t srcRowIdx = static_cast<size_t>(srcH - 1 - y) * srcW * 4;
#endif
        const uint8_t *srcRow = src + srcRowIdx;
        uint8_t *dstRow = dst + static_cast<size_t>(y) * pitch;
        tjs_int x = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        for(; x < simdCopyW; x += simdWidth) {
            uint8x16x4_t px = vld4q_u8(srcRow + x * 4);
            uint8x16_t tmp = px.val[0];
            px.val[0] = px.val[2];
            px.val[2] = tmp;
            vst4q_u8(dstRow + x * 4, px);
        }
#elif defined(__SSSE3__)
        const __m128i shuffleMask =
            _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
        for(; x + 4 <= copyW; x += 4) {
            __m128i v = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(srcRow + x * 4));
            v = _mm_shuffle_epi8(v, shuffleMask);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(dstRow + x * 4), v);
        }
#endif
        for(; x < copyW; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
        }
    }
}

// ---------------------------------------------------------------------------
// PBO double-buffer state for async glReadPixels.
// Uses two PBOs: one receives the current frame's async read while the
// other provides the previous frame's data via glMapBufferRange.
// Introduces one frame of latency but eliminates GPU pipeline stalls.
// ---------------------------------------------------------------------------
struct PBOState {
    GLuint pbo[2] = {};
    int idx = 0;
    GLsizei w = 0, h = 0;
    bool primed = false;
    bool disabled = false;

    bool EnsureSize(GLsizei newW, GLsizei newH) {
        if(disabled)
            return false;
        if(newW == w && newH == h && pbo[0])
            return true;
        if(pbo[0]) {
            glDeleteBuffers(2, pbo);
            pbo[0] = pbo[1] = 0;
        }
        while(glGetError() != GL_NO_ERROR) {
        }
        glGenBuffers(2, pbo);
        size_t sz = static_cast<size_t>(newW) * newH * 4;
        for(int i = 0; i < 2; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(sz),
                         nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if(glGetError() != GL_NO_ERROR) {
            if(pbo[0])
                glDeleteBuffers(2, pbo);
            pbo[0] = pbo[1] = 0;
            disabled = true;
            GLES_LOGW("PBO creation failed, falling back to sync glReadPixels");
            return false;
        }
        w = newW;
        h = newH;
        primed = false;
        return true;
    }
};

// ---------------------------------------------------------------------------
// CPU fallback: read pixels from GL FBO → TJS Layer bitmap.
// GL outputs RGBA bottom-up; krkr2 Layer CPU buffer uses BGRA top-down.
// Uses PBO double-buffering when available (GLES 3.0+) to avoid stalls.
// ---------------------------------------------------------------------------
static bool CopyFBOToLayerCPU(GLuint fbo, GLsizei srcW, GLsizei srcH,
                              tTJSNI_Layer *layerNI, GLint prevFbo) {
    tjs_int layerW = static_cast<tjs_int>(layerNI->GetWidth());
    tjs_int layerH = static_cast<tjs_int>(layerNI->GetHeight());
    auto *dst =
        reinterpret_cast<uint8_t *>(layerNI->GetMainImagePixelBufferForWrite());
    tjs_int pitch = layerNI->GetMainImagePixelBufferPitch();
    if(!dst || layerW <= 0 || layerH <= 0)
        return false;

    tjs_int copyW = (layerW < srcW) ? layerW : srcW;
    tjs_int copyH = (layerH < srcH) ? layerH : srcH;

    static PBOState s_pbo;
    const uint8_t *srcPixels = nullptr;
    bool mapped = false;

    if(s_pbo.EnsureSize(srcW, srcH)) {
        int readIdx = s_pbo.idx;
        int mapIdx = 1 - s_pbo.idx;

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, s_pbo.pbo[readIdx]);
        glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

        if(s_pbo.primed) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s_pbo.pbo[mapIdx]);
            srcPixels = static_cast<const uint8_t *>(glMapBufferRange(
                GL_PIXEL_PACK_BUFFER, 0,
                static_cast<GLsizeiptr>(srcW) * srcH * 4, GL_MAP_READ_BIT));
            if(srcPixels) {
                mapped = true;
            } else {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            }
        }

        s_pbo.primed = true;
        s_pbo.idx = 1 - s_pbo.idx;
    }

    if(!srcPixels) {
        static std::vector<uint8_t> s_rgba;
        size_t needed = static_cast<size_t>(srcW) * srcH * 4;
        if(s_rgba.size() < needed)
            s_rgba.resize(needed);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE,
                     s_rgba.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        srcPixels = s_rgba.data();
    }

    ConvertRGBA_to_BGRA(srcPixels, srcW, srcH, dst, pitch, copyW, copyH);

#if defined(KRKR_RENDER_PROBE)
    // 三个中心像素一起看，直接判定是哪一段错：
    //   fbo  —— 现读一次 FBO（真相）
    //   src  —— 读回通道产出的（PBO 异步路径会给上一帧的内容）
    //   dst  —— 转换后真正写进图层缓冲的
    // 只在头两次拷贝打，避免每帧 glReadPixels（README 硬约束 4）。
    {
        static int s_copySamples = 0;
        if(s_copySamples < 2) {
            ++s_copySamples;
            unsigned char fboCtr[4] = { 0, 0, 0, 0 };
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glReadPixels(srcW / 2, srcH / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                         fboCtr);
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            const unsigned char *srcC =
                srcPixels + static_cast<size_t>(srcW) * (srcH / 2) * 4u +
                static_cast<size_t>(srcW / 2) * 4u;
            const unsigned char *dstC =
                dst + static_cast<size_t>(pitch) * (layerH / 2) +
                static_cast<size_t>(layerW / 2) * 4u;
            spdlog::info("[probe] krkrgles: copy path={} fbo0=({},{},{},{}) "
                         "src=({},{},{},{}) dst=({},{},{},{}) pitch={} "
                         "copy={}x{} err=0x{:04X}",
                         mapped ? "pbo-async" : "sync-read", fboCtr[0],
                         fboCtr[1], fboCtr[2], fboCtr[3], srcC[0], srcC[1],
                         srcC[2], srcC[3], dstC[0], dstC[1], dstC[2], dstC[3],
                         static_cast<int>(pitch), static_cast<int>(copyW),
                         static_cast<int>(copyH),
                         static_cast<unsigned>(glGetError()));
        }
    }
#endif

    if(mapped) {
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    tTVPRect rc(0, 0, copyW, copyH);
    layerNI->Update(rc);
    return true;
}

// ---------------------------------------------------------------------------
// Copy FBO → Layer with automatic GPU/CPU path selection.
// Resolves native Layer instance once and passes to GPU/CPU paths.
// ---------------------------------------------------------------------------

// capture() 回调是否正在执行。Live2D 插件据此决定要不要把模型额外画到
// "调用方当前绑定的目标"上：官方插件是"画在当前 FBO"，而 G2 的全动画正是在
// capture 回调里调 model.render()，靠这一步把立绘合成进捕获目标。
static bool g_captureActive = false;

extern "C" bool KrkrGLES_IsCaptureActive() {
    return g_captureActive;
}

// capture 回调期间置位。**必须异常安全**：回调是 TJS，一旦它抛异常，
// 原先"置位 → 调用 → 复位"的写法会漏掉复位，标志永久卡在 true，此后每一帧
// `krkrlive2d` 的 render() 都会多 blit 一层到调用方目标（花屏/错层的来源）。
// 用 RAII 保证任何退出路径（含异常）都复位。
namespace {
    class CaptureScope {
    public:
        CaptureScope() {
            if(g_captureActive) {
                // 正常不该发生：说明上一次没复位（旧版本的泄漏）或 capture 被重入。
                static bool s_reported = false;
                if(!s_reported) {
                    s_reported = true;
                    spdlog::warn("[probe] krkrgles: capture 进入时标志已置位"
                                 "（重入或上一次未复位）");
                }
            }
            g_captureActive = true;
        }
        ~CaptureScope() { g_captureActive = false; }
        CaptureScope(const CaptureScope &) = delete;
        CaptureScope &operator=(const CaptureScope &) = delete;
    };
} // namespace

bool CopyFBOToLayer(GLuint fbo, GLsizei srcW, GLsizei srcH,
                    iTJSDispatch2 *layer, GLint prevFbo) {
    if(!fbo || srcW <= 0 || srcH <= 0 || !layer)
        return false;
    if(prevFbo < 0)
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    tTJSNI_Layer *layerNI = nullptr;
    if(TJS_FAILED(layer->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           (iTJSNativeInstance **)&layerNI)) ||
       !layerNI)
        return false;

    if(CopyFBOToLayerGPU(fbo, srcW, srcH, layerNI, prevFbo))
        return true;
    return CopyFBOToLayerCPU(fbo, srcW, srcH, layerNI, prevFbo);
}

// Global registered Layer — set by entryUpdateObject, accessed by
// krkrlive2d.cpp
static iTJSDispatch2 *g_registeredLayer = nullptr;
iTJSDispatch2 *KrkrGLES_GetRegisteredLayer() { return g_registeredLayer; }

namespace { // reopen anonymous namespace

    // ---------------------------------------------------------------------------
    // TJS expression helpers
    // ---------------------------------------------------------------------------
    static tjs_error CreateObjectByExpression(tTJSVariant *result,
                                              const tjs_char *expr,
                                              const char *tag) {
        if(!result || !expr)
            return TJS_E_FAIL;
        try {
            TVPExecuteExpression(ttstr(expr), result);
        } catch(...) {
            spdlog::error("krkrgles: {} eval '{}' failed", tag,
                          ttstr(expr).AsStdString());
            result->Clear();
            return TJS_E_FAIL;
        }
        if(result->Type() != tvtObject ||
           result->AsObjectNoAddRef() == nullptr) {
            result->Clear();
            return TJS_E_FAIL;
        }
        return TJS_S_OK;
    }

    static void InvokeLoadIfPresent(tTJSVariant &obj, tjs_int n,
                                    tTJSVariant **p, const char *tag) {
        if(n <= 0 || !p)
            return;
        iTJSDispatch2 *d = obj.AsObjectNoAddRef();
        if(!d)
            return;
        tjs_uint hint = 0;
        d->FuncCall(0, TJS_W("load"), &hint, nullptr, n, p, d);
    }

    // ---------------------------------------------------------------------------
    // Capture-callback invoker (capture → callback → render → copyLayer)
    // ---------------------------------------------------------------------------
    static tjs_error InvokeCaptureCallback(const char *tag, tjs_int w,
                                           tjs_int h, tjs_int n,
                                           tTJSVariant **p) {
        if(n <= 1 || !p || !p[1] || p[1]->Type() != tvtObject)
            return TJS_S_OK;
        tTJSVariantClosure cb = p[1]->AsObjectClosureNoAddRef();
        if(!cb.Object)
            return TJS_S_OK;
        tTJSVariant target;
        if(n > 0 && p[0])
            target = *p[0];
        tTJSVariant wv(w), hv(h), uv, fv;
        if(n > 2 && p[2])
            uv = *p[2];
        if(n > 3 && p[3])
            fv = *p[3];
        tTJSVariant *args[] = { &target, &wv, &hv, &uv, &fv };
        tjs_int argc = (n > 3) ? 5 : 4;
        return cb.FuncCall(0, nullptr, nullptr, nullptr, argc, args, nullptr);
    }

    // ---------------------------------------------------------------------------
    // Fallback dictionary module (when Cubism adaptor unavailable)
    // ---------------------------------------------------------------------------
    static tjs_error ReturnTrueCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error ReturnFirstArgOrTrueCb(tTJSVariant *r, tjs_int n,
                                            tTJSVariant **p, iTJSDispatch2 *) {
        if(!r)
            return TJS_S_OK;
        *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
        return TJS_S_OK;
    }

    static tjs_error DictSetScreenSizeCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, iTJSDispatch2 *obj) {
        if(obj && p) {
            if(n > 0)
                SetObjectProperty(obj, TJS_W("screenWidth"), *p[0]);
            if(n > 1)
                SetObjectProperty(obj, TJS_W("screenHeight"), *p[1]);
        }
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error DictCreateModelCb(tTJSVariant *r, tjs_int n,
                                       tTJSVariant **p, iTJSDispatch2 *) {
        tTJSVariant model;
        tjs_error er = CreateObjectByExpression(
            &model, TJS_W("new Live2DModel()"), "fallback.createModel");
        if(TJS_FAILED(er)) {
            if(r)
                r->Clear();
            return er;
        }
        InvokeLoadIfPresent(model, n, p, "fallback.createModel");
        if(r)
            *r = model;
        return TJS_S_OK;
    }

    static tjs_error DictCreateMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"),
                                        "fallback.createMatrix");
    }

    static tjs_error DictCreateDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *) {
        return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"),
                                        "fallback.createDevice");
    }

    static tjs_error DictEntryUpdateObjectCb(tTJSVariant *r, tjs_int n,
                                             tTJSVariant **p, iTJSDispatch2 *) {
        if(n > 0 && p) {
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
            if(layer)
                g_registeredLayer = layer;
        }
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error CreateFallbackModuleObject(tTJSVariant *result, tjs_int w,
                                                tjs_int h) {
        spdlog::warn("krkrgles: using fallback module ({}x{})", w, h);
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict) {
            if(result)
                result->Clear();
            return TJS_E_FAIL;
        }
        SetObjectProperty(dict, TJS_W("screenWidth"), tTJSVariant(w));
        SetObjectProperty(dict, TJS_W("screenHeight"), tTJSVariant(h));
        SetObjectMethod(dict, TJS_W("entryUpdateObject"),
                        DictEntryUpdateObjectCb);
        SetObjectMethod(dict, TJS_W("setScreenSize"), DictSetScreenSizeCb);
        SetObjectMethod(dict, TJS_W("makeCurrent"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("beginScene"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("endScene"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("finalize"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("render"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("glesEntry"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("glesRemove"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("capture"), ReturnFirstArgOrTrueCb);
        SetObjectMethod(dict, TJS_W("captureScreen"), ReturnFirstArgOrTrueCb);
        SetObjectMethod(dict, TJS_W("glesCapture"), ReturnFirstArgOrTrueCb);
        SetObjectMethod(dict, TJS_W("glesCaptureScreen"),
                        ReturnFirstArgOrTrueCb);
        SetObjectMethod(dict, TJS_W("copyLayer"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("glesCopyLayer"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("drawLayer"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("glesDrawLayer"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("drawAffineGLES"), ReturnTrueCb);
        SetObjectMethod(dict, TJS_W("createModel"), DictCreateModelCb);
        SetObjectMethod(dict, TJS_W("createMatrix"), DictCreateMatrixCb);
        SetObjectMethod(dict, TJS_W("createDevice"), DictCreateDeviceCb);
        SetResultObject(result, dict);
        dict->Release();
        return TJS_S_OK;
    }

    // ---------------------------------------------------------------------------
    // GLESModule — holds per-module FBO + rendering state
    // ---------------------------------------------------------------------------
    class GLESModule {
    public:
        GLESModule() = default;
        ~GLESModule() { fbo_.Destroy(); }

        OffscreenFBO &GetFBO() { return fbo_; }

        static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "entryUpdateObject", n, p);
            if(n > 0 && p) {
                iTJSDispatch2 *layer = FindLayerInParams(n, p);
                if(layer) {
                    g_registeredLayer = layer;
                }
            }
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESModule *s) {
            if(!s || !p)
                return TJS_S_OK;
            if(n > 0)
                s->screenWidth_ = ToInt(*p[0], s->screenWidth_);
            if(n > 1)
                s->screenHeight_ = ToInt(*p[1], s->screenHeight_);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       GLESModule *) {
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error beginSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "beginScene", n, p);
            if(s) {
                tjs_int w = NormalizeExtent(s->screenWidth_, 1920);
                tjs_int h = NormalizeExtent(s->screenHeight_, 1080);
                s->fbo_.EnsureSize(static_cast<GLsizei>(w),
                                   static_cast<GLsizei>(h));
                s->fbo_.Bind();
                s->sceneActive_ = true;
            }
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error endSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "endScene", n, p);
            if(s) {
                s->fbo_.Unbind();
                s->sceneActive_ = false;
            }
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error finalizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "finalize", n, p);
            if(s)
                s->fbo_.Destroy();
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "capture", n, p);
            tjs_int w = NormalizeExtent(s ? s->screenWidth_ : 0, 1920);
            tjs_int h = NormalizeExtent(s ? s->screenHeight_ : 0, 1080);
            InvokeCaptureCallback("GLESModule.capture", w, h, n, p);
            if(r)
                *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
            return TJS_S_OK;
        }

        static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n,
                                       tTJSVariant **p, GLESModule *s) {
            return captureCb(r, n, p, s);
        }

        static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESModule *s) {
            return captureCb(r, n, p, s);
        }

        static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n,
                                             tTJSVariant **p, GLESModule *s) {
            return captureCb(r, n, p, s);
        }

        static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "copyLayer", n, p);
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
            if(layer) {
                if(s && s->fbo_.GetFBO()) {
                    GLint prevFbo = s->fbo_.GetPrevFbo();
                    CopyFBOToLayer(s->fbo_.GetFBO(), s->fbo_.GetWidth(),
                                   s->fbo_.GetHeight(), layer, prevFbo);
                }
#ifdef KRKR2_LIVE2D
                else if(g_live2dRenderTarget.fbo) {
                    CopyFBOToLayer(g_live2dRenderTarget.fbo,
                                   g_live2dRenderTarget.width,
                                   g_live2dRenderTarget.height, layer, -1);
                }
#endif
            }
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "glesCopyLayer", n, p);
            return copyLayerCb(r, n, p, s);
        }

        static tjs_error drawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "drawLayer", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "glesDrawLayer", n, p);
            return drawLayerCb(r, n, p, s);
        }

        static tjs_error drawAffineCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "drawAffine", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "drawAffineGLES", n, p);
            return drawAffineCb(r, n, p, s);
        }

        static tjs_error renderCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "render", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error setMatrixCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESModule *s) {
            KRKR_PROBE_TJS("GLESModule", "setMatrix", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error createModelCb(tTJSVariant *r, tjs_int n,
                                       tTJSVariant **p, GLESModule *) {
            tTJSVariant model;
            tjs_error er = CreateObjectByExpression(
                &model, TJS_W("new Live2DModel()"), "createModel");
            if(TJS_FAILED(er)) {
                if(r)
                    r->Clear();
                return er;
            }
            InvokeLoadIfPresent(model, n, p, "createModel");
            if(r)
                *r = model;
            return TJS_S_OK;
        }

        static tjs_error createMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        GLESModule *) {
            return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"),
                                            "createMatrix");
        }

        static tjs_error createDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        GLESModule *) {
            return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"),
                                            "createDevice");
        }

        tjs_int getScreenWidth() const { return screenWidth_; }
        void setScreenWidth(tjs_int v) { screenWidth_ = v; }
        tjs_int getScreenHeight() const { return screenHeight_; }
        void setScreenHeight(tjs_int v) { screenHeight_ = v; }

        bool isSceneActive() const { return sceneActive_; }
        OffscreenFBO &getFBO() { return fbo_; }

    private:
        tjs_int screenWidth_ = 0;
        tjs_int screenHeight_ = 0;
        bool sceneActive_ = false;
        OffscreenFBO fbo_;
    };

    // ---------------------------------------------------------------------------
    // Module object creation
    // ---------------------------------------------------------------------------
    static tjs_error CreateModuleObject(tTJSVariant *result, tjs_int w = 0,
                                        tjs_int h = 0) {
        auto *mod = new GLESModule();
        spdlog::debug("krkrgles: GLES module created {}x{}", w, h);
        mod->setScreenWidth(w);
        mod->setScreenHeight(h);
        iTJSDispatch2 *obj = ncbInstanceAdaptor<GLESModule>::CreateAdaptor(mod);
        if(!obj) {
            delete mod;
            return CreateFallbackModuleObject(result, w, h);
        }
        SetResultObject(result, obj);
        obj->Release();
        return TJS_S_OK;
    }

    extern "C" tjs_error TVPKrkrGLESCreateModuleObject(tTJSVariant *result,
                                                       tjs_int w, tjs_int h) {
        return CreateModuleObject(result, w, h);
    }

    // ---------------------------------------------------------------------------
    // DrawDevice getModule callback
    // ---------------------------------------------------------------------------
    static tjs_error DrawDeviceGetModuleCb(tTJSVariant *r, tjs_int n,
                                           tTJSVariant **p,
                                           iTJSDispatch2 *objthis) {
        static std::unordered_map<uintptr_t,
                                  std::unordered_map<ModuleName, tTJSVariant>>
            s_mod;
        const ModuleName mn = NormalizeModuleName(n, p);
        const uintptr_t key = reinterpret_cast<uintptr_t>(objthis);
        if(key) {
            auto dit = s_mod.find(key);
            if(dit != s_mod.end()) {
                auto mit = dit->second.find(mn);
                if(mit != dit->second.end() &&
                   mit->second.Type() == tvtObject &&
                   mit->second.AsObjectNoAddRef()) {
                    if(r)
                        *r = mit->second;
                    return TJS_S_OK;
                }
            }
        }
        tTJSVariant created;
        tjs_error er = CreateModuleObject(&created);
        if(TJS_FAILED(er)) {
            if(r)
                r->Clear();
            return er;
        }
        if(key && created.Type() == tvtObject && created.AsObjectNoAddRef())
            s_mod[key][mn] = created;
        if(r)
            *r = created;
        return TJS_S_OK;
    }

    // ---------------------------------------------------------------------------
    // GLESAdaptor — per-window adaptor that proxies to the module
    // ---------------------------------------------------------------------------
    class GLESAdaptor {
    public:
        GLESAdaptor() = default;

        GLESModule *FindModule() {
            if(cachedModule_)
                return cachedModule_;
            tTJSVariant mv;
            if(TJS_SUCCEEDED(GLESAdaptor::getModuleCb(&mv, 0, nullptr, this))) {
                iTJSDispatch2 *obj = mv.AsObjectNoAddRef();
                if(obj)
                    cachedModule_ =
                        ncbInstanceAdaptor<GLESModule>::GetNativeInstance(obj);
            }
            return cachedModule_;
        }

        static tjs_error getModuleCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "getModule", n, p);
            static std::unordered_map<
                uintptr_t, std::unordered_map<ModuleName, tTJSVariant>>
                sm;
            const ModuleName mn = NormalizeModuleName(n, p);
            const uintptr_t key = reinterpret_cast<uintptr_t>(s);
            if(key) {
                auto dit = sm.find(key);
                if(dit != sm.end()) {
                    auto mit = dit->second.find(mn);
                    if(mit != dit->second.end() &&
                       mit->second.Type() == tvtObject &&
                       mit->second.AsObjectNoAddRef()) {
                        if(r)
                            *r = mit->second;
                        return TJS_S_OK;
                    }
                }
            }
            tTJSVariant created;
            tjs_error er = s ? CreateModuleObject(&created, s->screenWidth_,
                                                  s->screenHeight_)
                             : CreateModuleObject(&created);
            if(TJS_FAILED(er)) {
                if(r)
                    r->Clear();
                return er;
            }
            if(key && created.Type() == tvtObject && created.AsObjectNoAddRef())
                sm[key][mn] = created;
            if(r)
                *r = created;
            return TJS_S_OK;
        }

        static tjs_error setScreenSizeCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESAdaptor *s) {
            if(!s || !p)
                return TJS_S_OK;
            if(n > 0)
                s->screenWidth_ = ToInt(*p[0], s->screenWidth_);
            if(n > 1)
                s->screenHeight_ = ToInt(*p[1], s->screenHeight_);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error makeCurrentCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       GLESAdaptor *) {
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error beginSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "beginScene", n, p);
            auto *mod = s ? s->FindModule() : nullptr;
            if(mod)
                return GLESModule::beginSceneCb(r, n, p, mod);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error endSceneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "endScene", n, p);
            auto *mod = s ? s->FindModule() : nullptr;
            if(mod)
                return GLESModule::endSceneCb(r, n, p, mod);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error entryUpdateObjectCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "entryUpdateObject", n, p);
            if(n > 0 && p) {
                iTJSDispatch2 *layer = FindLayerInParams(n, p);
                if(layer)
                    g_registeredLayer = layer;
            }
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error captureCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "capture", n, p);
            tjs_int w = NormalizeExtent(s ? s->screenWidth_ : 0, 1920);
            tjs_int h = NormalizeExtent(s ? s->screenHeight_ : 0, 1080);

            // 官方契约（krkrgles 手册 / wamsoft 参考实现）：
            //   capture(layer, callback, param, color)
            //     1) 绑定自己的离屏 FBO，并按 color 清屏
            //     2) 以 (width, height, param) 调用回调——回调里的绘制落在该 FBO 上
            //     3) 把 FBO 的内容交给 layer（「結果格納先レイヤ」）
            //
            // 此前这里**只做了第 2 步**：既没有离屏渲染目标，也从没把结果交给图层。
            // 于是那个目标图层一直是新建时的全黑，盖住整屏——G2「全动画」整屏全黑、
            // 有声音、且日志零报错的直接原因（宿主探针实测：引擎画了 140 个图层，
            // 合成结果 nonBlack=0/25、全黑不透明）。
            iTJSDispatch2 *layer = FindLayerInParams(n, p);
#if defined(KRKR_RENDER_PROBE)
            // 探针：捕获目标到底是谁——是不是一个真正的 Layer、多大、透明度多少。
            // 全动画黑屏的最后一段就卡在"内容写进了哪一层、那一层有没有上屏"。
            {
                static std::unordered_map<void *, bool> s_seen;
                if(layer && s_seen.size() < 8 &&
                   s_seen.find(static_cast<void *>(layer)) == s_seen.end()) {
                    s_seen[static_cast<void *>(layer)] = true;
                    tTJSVariant wv, hv, ov;
                    layer->PropGet(0, TJS_W("imageWidth"), nullptr, &wv, layer);
                    layer->PropGet(0, TJS_W("imageHeight"), nullptr, &hv, layer);
                    layer->PropGet(0, TJS_W("opacity"), nullptr, &ov, layer);
                    const bool isLayer =
                        layer->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"),
                                            nullptr) == TJS_S_TRUE;
                    spdlog::info("[probe] krkrgles: capture target #{} ptr={} "
                                 "isLayer={} image={}x{} opacity={}",
                                 static_cast<int>(s_seen.size()),
                                 static_cast<const void *>(layer), isLayer,
                                 static_cast<int>(ToInt(wv, 0)),
                                 static_cast<int>(ToInt(hv, 0)),
                                 static_cast<int>(ToInt(ov, 255)));
                } else if(!layer) {
                    static bool s_reported = false;
                    if(!s_reported) {
                        s_reported = true;
                        spdlog::warn("[probe] krkrgles: capture 参数里没找到图层"
                                     "（p0 类型={}）——结果无处可放",
                                     ProbeTypeName(n > 0 ? p[0] : nullptr));
                    }
                }
            }
#endif
            if(s && layer && s->fbo_.EnsureSize(static_cast<GLsizei>(w),
                                                static_cast<GLsizei>(h))) {
                // Bind() 自带"绑定 + 设视口 + 清成透明"；游戏传的 color=0 正是这个值，
                // 非 0 时再按 ARGB 覆盖一次（与参考实现的清屏口径一致）。
                s->fbo_.Bind();
                const tjs_uint32 color =
                    (n > 3 && p && p[3]) ? static_cast<tjs_uint32>(ToInt(*p[3], 0))
                                         : 0u;
                if(color) {
                    glClearColor(((color >> 16) & 0xff) / 255.0f,
                                 ((color >> 8) & 0xff) / 255.0f,
                                 (color & 0xff) / 255.0f,
                                 ((color >> 24) & 0xff) / 255.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
                // 回调期间置位：Live2D 的 render() 据此把模型也画进这个捕获 FBO。
                // RAII 保证回调抛异常时也会复位（否则标志会永久卡在 true）。
                {
                    CaptureScope captureScope;
                    InvokeCaptureCallback("GLESAdaptor.capture", w, h, n, p);
                }
#if defined(KRKR_RENDER_PROBE)
                // 探针：回调结束后、拷贝之前，捕获 FBO 里到底有什么。
                // 与下面"图层 CPU 缓冲"那处配套：前者判"模型有没有进捕获 FBO"，
                // 后者判"内容有没有落进图层"。5 个点、仅首次，够分辨且不改结果。
                {
                    static bool s_fboSampled = false;
                    if(!s_fboSampled) {
                        s_fboSampled = true;
                        GLint keepFbo = 0;
                        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &keepFbo);
                        const GLsizei sx = static_cast<GLsizei>(w);
                        const GLsizei sy = static_cast<GLsizei>(h);
                        const GLsizei xs[5] = { sx / 2, sx / 8, sx * 7 / 8,
                                                sx / 8, sx * 7 / 8 };
                        const GLsizei ys[5] = { sy / 2, sy / 8, sy / 8,
                                                sy * 7 / 8, sy * 7 / 8 };
                        int aNonZero = 0, whiteZero = 0;
                        unsigned char ctr[4] = { 0, 0, 0, 0 };
                        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_.GetFBO());
                        for(int i = 0; i < 5; ++i) {
                            unsigned char px[4] = { 0, 0, 0, 0 };
                            glReadPixels(xs[i], ys[i], 1, 1, GL_RGBA,
                                         GL_UNSIGNED_BYTE, px);
                            if(px[3] > 0)
                                ++aNonZero;
                            if(px[3] == 0 && px[0] == 255 && px[1] == 255 &&
                               px[2] == 255)
                                ++whiteZero;
                            if(i == 0) {
                                ctr[0] = px[0];
                                ctr[1] = px[1];
                                ctr[2] = px[2];
                                ctr[3] = px[3];
                            }
                        }
                        glBindFramebuffer(GL_FRAMEBUFFER,
                                          static_cast<GLuint>(keepFbo));
                        spdlog::info("[probe] krkrgles: capture FBO {}x{} "
                                     "samples=5 a>0={} white+transparent={} "
                                     "center=({},{},{},{})",
                                     static_cast<int>(sx), static_cast<int>(sy),
                                     aNonZero, whiteZero, ctr[0], ctr[1],
                                     ctr[2], ctr[3]);
                    }
                }
#endif
                // 结果交给图层。官方参考实现的落点是图层的 **CPU 像素缓冲**
                // （setSize + mainImageBufferForWrite），而不是只写 GL 纹理：
                // 引擎随后按 CPU 位图重传纹理会把"只写在纹理上"的内容覆盖回去。
                // 所以 capture 优先走 CPU 路径，失败再退 GPU blit。
                tTJSNI_Layer *layerNI = nullptr;
                if(TJS_FAILED(layer->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(&layerNI))) ||
                   !layerNI) {
                    layerNI = nullptr;
                }
                bool copied = false;
                if(layerNI) {
                    copied = CopyFBOToLayerCPU(s->fbo_.GetFBO(),
                                               static_cast<GLsizei>(w),
                                               static_cast<GLsizei>(h), layerNI,
                                               s->fbo_.GetPrevFbo());
                }
                if(!copied) {
                    CopyFBOToLayer(s->fbo_.GetFBO(), static_cast<GLsizei>(w),
                                   static_cast<GLsizei>(h), layer,
                                   s->fbo_.GetPrevFbo());
                }
#if defined(KRKR_RENDER_PROBE)
                if(layerNI) {
                    static bool s_sampled = false;
                    if(!s_sampled) {
                        s_sampled = true;
                        const auto *buf = reinterpret_cast<const unsigned char *>(
                            layerNI->GetMainImagePixelBufferForWrite());
                        const tjs_int pitch =
                            layerNI->GetMainImagePixelBufferPitch();
                        const tjs_int lw = layerNI->GetWidth();
                        const tjs_int lh = layerNI->GetHeight();
                        if(buf && pitch > 0 && lw > 2 && lh > 2) {
                            // 3x3 采样：只看中心会误判——中心可能正好落在模型的
                            // 透明区，而别处有内容。同时统计"白而透明"
                            // (255,255,255,0)，它在预乘口径下是非法像素，
                            // 能区分"空"与"被写坏"。copied 用来确认走了哪条路径。
                            int aNonZero = 0, whiteZero = 0, sampled = 0;
                            unsigned char ctr[4] = { 0, 0, 0, 0 };
                            for(int gy = 1; gy <= 3; ++gy) {
                                for(int gx = 1; gx <= 3; ++gx) {
                                    const unsigned char *c =
                                        buf + static_cast<size_t>(pitch) *
                                                  (lh * gy / 4) +
                                        static_cast<size_t>(lw * gx / 4) * 4u;
                                    ++sampled;
                                    if(c[3] > 0)
                                        ++aNonZero;
                                    if(c[3] == 0 && c[0] == 255 && c[1] == 255 &&
                                       c[2] == 255)
                                        ++whiteZero;
                                    if(gx == 2 && gy == 2) {
                                        ctr[0] = c[0];
                                        ctr[1] = c[1];
                                        ctr[2] = c[2];
                                        ctr[3] = c[3];
                                    }
                                }
                            }
                            spdlog::info(
                                "[probe] krkrgles: layer CPU buffer copied={} "
                                "{}x{} samples={} a>0={} white+transparent={} "
                                "center=({},{},{},{})",
                                copied ? 1 : 0, static_cast<int>(lw),
                                static_cast<int>(lh), sampled, aNonZero, whiteZero,
                                ctr[0], ctr[1], ctr[2], ctr[3]);
                        }
                    }
                }
#endif
                s->fbo_.Unbind();
            } else {
                InvokeCaptureCallback("GLESAdaptor.capture", w, h, n, p);
            }
            if(r)
                *r = (n > 0 && p) ? *p[0] : tTJSVariant(true);
            return TJS_S_OK;
        }

        static tjs_error glesCaptureCb(tTJSVariant *r, tjs_int n,
                                       tTJSVariant **p, GLESAdaptor *s) {
            return captureCb(r, n, p, s);
        }

        static tjs_error captureScreenCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, GLESAdaptor *s) {
            return captureCb(r, n, p, s);
        }

        static tjs_error glesCaptureScreenCb(tTJSVariant *r, tjs_int n,
                                             tTJSVariant **p, GLESAdaptor *s) {
            return captureCb(r, n, p, s);
        }

        static tjs_error copyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "copyLayer", n, p);
            auto *mod = s ? s->FindModule() : nullptr;
            if(mod)
                return GLESModule::copyLayerCb(r, n, p, mod);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error glesCopyLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "glesCopyLayer", n, p);
            return copyLayerCb(r, n, p, s);
        }

        static tjs_error drawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "drawLayer", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error glesDrawLayerCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "glesDrawLayer", n, p);
            return drawLayerCb(r, n, p, s);
        }

        static tjs_error drawAffineCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "drawAffine", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error drawAffineGLESCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "drawAffineGLES", n, p);
            return drawAffineCb(r, n, p, s);
        }

        static tjs_error renderCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "render", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error setMatrixCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "setMatrix", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error createModelCb(tTJSVariant *r, tjs_int n,
                                       tTJSVariant **p, GLESAdaptor *) {
            tTJSVariant model;
            tjs_error er = CreateObjectByExpression(
                &model, TJS_W("new Live2DModel()"), "GLESAdaptor.createModel");
            if(TJS_FAILED(er)) {
                if(r)
                    r->Clear();
                return er;
            }
            InvokeLoadIfPresent(model, n, p, "GLESAdaptor.createModel");
            if(r)
                *r = model;
            return TJS_S_OK;
        }

        static tjs_error createMatrixCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        GLESAdaptor *) {
            return CreateObjectByExpression(r, TJS_W("new Live2DMatrix()"),
                                            "GLESAdaptor.createMatrix");
        }

        static tjs_error createDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        GLESAdaptor *) {
            return CreateObjectByExpression(r, TJS_W("new Live2DDevice()"),
                                            "GLESAdaptor.createDevice");
        }

        static tjs_error finalizeCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "finalize", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error glesEntryCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "glesEntry", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        static tjs_error glesRemoveCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, GLESAdaptor *s) {
            KRKR_PROBE_TJS("GLESAdaptor", "glesRemove", n, p);
            if(r)
                *r = true;
            return TJS_S_OK;
        }

        tjs_int getScreenWidth() const { return screenWidth_; }
        void setScreenWidth(tjs_int v) { screenWidth_ = v; }
        tjs_int getScreenHeight() const { return screenHeight_; }
        void setScreenHeight(tjs_int v) { screenHeight_ = v; }

    private:
        tjs_int screenWidth_ = 0;
        tjs_int screenHeight_ = 0;
        GLESModule *cachedModule_ = nullptr;
        // capture() 的离屏渲染目标：回调内的绘制落在它上面，回调结束后整块交给目标图层。
        OffscreenFBO fbo_;
    };

    // -----------------------------------------------------------------------
    // krkrz 的 OGLDrawDevice 兼容层。
    //
    // krkrz（吉里吉里Z）把 OGLDrawDevice 作为 GL 绘制设备对外暴露，krkrgles 系
    // 游戏的 Initialize.tjs 会先看 `Window.OGLDrawDevice` 在不在，再决定要不要走
    // GPU 路径。缺了它游戏**不报错**，只是静静跳过 `GPULayer.tjs` /
    // `GPUAffineLayer.tjs`（真机实测：这两个脚本一条 exec 都没有），于是为 GL 管线
    // 写的 CG 就没地方画——表现就是"有声音、整屏黑"。
    //
    // 本类不需要自己实现 GL：它不是新的渲染实现，而是 GLESAdaptor 的**同源别名**，
    // 内部持有一个 GLESAdaptor 逐个转调，因此复用已经验证过的 capture 交付路径。
    // 是否挂到 Window 上由 ogldrawdevice_compat 选项决定（见 KrkrGlesPostRegist）。
    // 注意它只负责"让游戏的 GPU 判断成立"，并不等于 GPU 层真能跑——真机实测
    // GPU 层脚本初始化时仍会抛 `mixinclass.tjs(1) [(function) missing]`，
    // 因为 krkrz 的 Canvas / Texture / ShaderProgram 那一套本引擎没有实现。
    // -----------------------------------------------------------------------
#define KRKR_OGL_FORWARD(cb)                                                   \
    static tjs_error cb(tTJSVariant *r, tjs_int n, tTJSVariant **p,            \
                        OGLDrawDevice *s) {                                    \
        return GLESAdaptor::cb(r, n, p, s ? &s->adaptor_ : nullptr);           \
    }

    class OGLDrawDevice {
    public:
        OGLDrawDevice() = default;

        tjs_int getScreenWidth() const { return adaptor_.getScreenWidth(); }
        void setScreenWidth(tjs_int v) { adaptor_.setScreenWidth(v); }
        tjs_int getScreenHeight() const { return adaptor_.getScreenHeight(); }
        void setScreenHeight(tjs_int v) { adaptor_.setScreenHeight(v); }

        KRKR_OGL_FORWARD(getModuleCb)
        KRKR_OGL_FORWARD(setScreenSizeCb)
        KRKR_OGL_FORWARD(makeCurrentCb)
        KRKR_OGL_FORWARD(beginSceneCb)
        KRKR_OGL_FORWARD(endSceneCb)
        KRKR_OGL_FORWARD(entryUpdateObjectCb)
        KRKR_OGL_FORWARD(captureCb)
        KRKR_OGL_FORWARD(glesCaptureCb)
        KRKR_OGL_FORWARD(captureScreenCb)
        KRKR_OGL_FORWARD(glesCaptureScreenCb)
        KRKR_OGL_FORWARD(copyLayerCb)
        KRKR_OGL_FORWARD(glesCopyLayerCb)
        KRKR_OGL_FORWARD(drawLayerCb)
        KRKR_OGL_FORWARD(glesDrawLayerCb)
        KRKR_OGL_FORWARD(drawAffineCb)
        KRKR_OGL_FORWARD(drawAffineGLESCb)
        KRKR_OGL_FORWARD(renderCb)
        KRKR_OGL_FORWARD(setMatrixCb)
        KRKR_OGL_FORWARD(createModelCb)
        KRKR_OGL_FORWARD(createMatrixCb)
        KRKR_OGL_FORWARD(createDeviceCb)
        KRKR_OGL_FORWARD(glesEntryCb)
        KRKR_OGL_FORWARD(glesRemoveCb)
        KRKR_OGL_FORWARD(finalizeCb)

    private:
        GLESAdaptor adaptor_;
    };

#undef KRKR_OGL_FORWARD

} // namespace

// ---------------------------------------------------------------------------
// NCB Registration
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// krkrz 兼容层：按 ogldrawdevice_compat 选项决定把 OGLDrawDevice 暴露到什么程度。
//
//   off   —— 什么都不做（默认，保持原行为）
//   ogl   —— 只挂 Window.OGLDrawDevice
//   alias —— 挂 Window.OGLDrawDevice + Window.GLESAdaptor
//   kag   —— 在 alias 之上再接管 KAGWindow_createDrawDevice
//
// 各档的实测作用：
//
//   * `Window.OGLDrawDevice` 是**闸门**：游戏的 Initialize.tjs 先看它在不在，缺了就
//     静默跳过 GPULayer.tjs / GPUAffineLayer.tjs。挂上后两者都会加载
//     （会话 11:22 首次出现在 StorageExec 里）。所有非 off 档都会挂它。
//   * `Window.GLESAdaptor` **会改变部分游戏的行为**：千恋万花在 alias 档下被切进
//     motionplayer 的 `captureCanvas` 路径（`D3DAdaptor.captureCanvas` 211 次、
//     `drawOnto` 107 次、`drawPSBImages: captureCanvas active, skip draw` 104 次），
//     而 `Player::draw` 从此让路、UI 图全压在 `drawOnto` 这一条交付上——实测那条交付
//     不完整，UI 就出问题。`ogl` 档就是给这种"只要闸门、不要 canvas 捕获"的游戏用的。
//   * `kag` 解决"窗口绘制设备工厂"：`KAGWindow_createDrawDevice` 由游戏自己的
//     `system\mainwindow.tjs` 定义、在插件注册之后才 exec，所以覆盖必须**延迟**到
//     脚本加载完（用一次性连续事件钩子，装完立即摘钩）。
//     实测收益：**千恋万花**在 kag 档下能正常加载立绘与背景动态。
//
// ⚠️ 各档都不是万能的，**必须逐游戏试**：
//   - `kag` 对 G2（nainiuniu5krkr）有害：主机侧 `HostWindowLayer::SourceSample`
//     报 53 次 `FBO incomplete 0x8CD6`（GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT），
//     画面直接采不到（连回想页都黑）；draw 从 20/layers 143 变成 345/layers 465。
//   - `alias` 对千恋万花有害（见上）。
//   ⇒ 都不该做全局默认。
// ---------------------------------------------------------------------------
// 与 bridge/engine_api/include/engine_options.h 的 ENGINE_OPTION_OGLDRAWDEVICE_COMPAT
// 保持一致。引擎核心不依赖 bridge 的头，按既有惯例用字面量键名
// （同理见 FreeTypeFontRasterizer.cpp 里的 "font_fallback_mode"）。
static const tjs_char *KrkrOglCompatOption = TJS_W("ogldrawdevice_compat");
static const tjs_char *KrkrOglCompatOgl = TJS_W("ogl");
static const tjs_char *KrkrOglCompatAlias = TJS_W("alias");
static const tjs_char *KrkrOglCompatKag = TJS_W("kag");

// 把 ncbind 注册的全局类 `name` 挂到 Window 类对象上（即 `Window.<name>`）。
//
// 这里必须从 **C++** 侧写、不能改成 TJS 的 `Window.X = X`：TJS 对 Window 这种原生
// 对象的成员写入本来会以 ACCESSDENYED / MEMBERNOTFOUND 失败，AetherKiri 是给 tjs2 的
// `tTJSExtendableObject::PropSet` 加了一张"启动期可写名"白名单（含 OGLDrawDevice /
// GLESAdaptor / gpuDrawDevice）才放行的，见
// AetherKiri/cpp/core/tjs2/tjsObjectExtendable.cpp:9。KiriNext 没有那张白名单，
// 从 TJS 写会被 try/catch 静静吞掉、功能等于没做（那就是个"看着有、其实没用"的假开关）。
// 改用本仓库既有手法：ScriptMgnIntf.cpp 注册 Window.BasicDrawDevice 时就是这么写的。
static void KrkrOglAliasOntoWindow(const tjs_char *name) {
    // TVPGetScriptDispatch() 内部 AddRef 过，用完必须 Release。
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    tTJSVariant windowVal;
    if(TJS_SUCCEEDED(
           global->PropGet(0, TJS_W("Window"), nullptr, &windowVal, global))) {
        iTJSDispatch2 *windowClass = windowVal.AsObjectNoAddRef();
        tTJSVariant classVal;
        if(windowClass &&
           TJS_SUCCEEDED(
               global->PropGet(0, name, nullptr, &classVal, global)) &&
           classVal.Type() == tvtObject && classVal.AsObjectNoAddRef()) {
            windowClass->PropSet(
                TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER, name,
                nullptr, &classVal, windowClass);
        }
    }

    global->Release();
}

static const tjs_char *KrkrOglKagScript() {
    return TJS_W("function KAGWindow_createDrawDevice() {\n")
        TJS_W("    var dd = null;\n")
        TJS_W("    try { dd = new global.OGLDrawDevice(); } catch(e) { try {"
              " dd = new global.GLESAdaptor(); } catch(e2) { dd = null; } }\n")
        TJS_W("    try { if(dd !== null) dd.setScreenSize(this.width,"
              " this.height); } catch(e) { }\n")
        TJS_W("    try { this.gpuDrawDevice = dd; } catch(e) { }\n")
        TJS_W("    try { this.OGLDrawDevice = global.OGLDrawDevice; }"
              " catch(e) { }\n")
        TJS_W("    try { this.GLESAdaptor = global.GLESAdaptor; }"
              " catch(e) { }\n")
        TJS_W("    try { return new global.Window.BasicDrawDevice(); }"
              " catch(e) { }\n")
        TJS_W("    try { return new global.Window.PassThroughDrawDevice(); }"
              " catch(e) { }\n")
        TJS_W("    return null;\n")
        TJS_W("}\n")
        TJS_W("try { KAGWindow.KAGWindow_createDrawDevice ="
              " KAGWindow_createDrawDevice; } catch(e) { }\n")
        // KAGWindow 上没有 prototype，这句在真机会抛 `Member "prototype" does
        // not exist`（被 try 吞掉）。留着是为了兼容"把它当构造函数"的写法。
        TJS_W("try { KAGWindow.prototype.KAGWindow_createDrawDevice ="
              " KAGWindow_createDrawDevice; } catch(e) { }\n")
        TJS_W("try { KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; }"
              " catch(e) { }\n");
}

// 一次性安装器：脚本加载完之后的第一帧才轮到它，装完立即摘钩。
class KrkrOglKagInstallHook : public tTVPContinuousEventCallbackIntf {
public:
    void OnContinuousCallback(tjs_uint64 /*tick*/) override {
        TVPRemoveContinuousEventHook(this);
        try {
            TVPExecuteExpression(ttstr(KrkrOglKagScript()));
            spdlog::info("krkrgles: kag 档已接管 KAGWindow_createDrawDevice"
                         "（GL 设备进 gpuDrawDevice，真设备仍是 BasicDrawDevice）");
        } catch(...) {
            spdlog::warn("krkrgles: kag 档接管 KAGWindow_createDrawDevice 失败");
        }
    }
};

static KrkrOglKagInstallHook g_krkrOglKagInstallHook;

static void KrkrGlesPreRegist() {}

static void KrkrGlesPostRegist() {
    tTJSVariant modeVal;
    if(!TVPGetCommandLine(KrkrOglCompatOption, &modeVal))
        return;
    const ttstr mode(modeVal);
    const ttstr kOgl(KrkrOglCompatOgl);
    const ttstr kAlias(KrkrOglCompatAlias);
    const ttstr kKag(KrkrOglCompatKag);
    if(mode != kOgl && mode != kAlias && mode != kKag)
        return; // off 或未知取值：保持原行为

    // Window.OGLDrawDevice 是"闸门"：所有非 off 档都挂，游戏才会去加载
    // GPULayer.tjs / GPUAffineLayer.tjs（真机实测：会话 11:22 首次出现在
    // StorageExec 里）。
    KrkrOglAliasOntoWindow(TJS_W("OGLDrawDevice"));
    // Window.GLESAdaptor 只给 alias / kag。实测它会把一部分游戏（千恋万花）切进
    // motionplayer 的 captureCanvas 路径，而那条交付目前不完整 ⇒ `ogl` 档专门
    // 留给"只要闸门、不要 canvas 捕获"的游戏。
    if(mode != kOgl)
        KrkrOglAliasOntoWindow(TJS_W("GLESAdaptor"));
    spdlog::info("krkrgles: ogldrawdevice_compat={} 已启用（挂了 {}）",
                 mode == kOgl ? "ogl" : (mode == kKag ? "kag" : "alias"),
                 mode == kOgl ? "Window.OGLDrawDevice"
                              : "Window.OGLDrawDevice / Window.GLESAdaptor");

    // kag 档：额外接管窗口的绘制设备工厂。必须延迟安装，见本段开头说明。
    if(mode == kKag) {
        TVPAddContinuousEventHook(&g_krkrOglKagInstallHook);
        spdlog::info("krkrgles: kag 档就绪，KAGWindow_createDrawDevice 将在"
                     "脚本加载完成后的首个连续事件里接管");
    }
}
NCB_PRE_REGIST_CALLBACK(KrkrGlesPreRegist);
NCB_POST_REGIST_CALLBACK(KrkrGlesPostRegist);

NCB_REGISTER_CLASS(GLESModule) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESModule::entryUpdateObjectCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESModule::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESModule::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESModule::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESModule::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESModule::finalizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESModule::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESModule::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESModule::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESModule::glesCaptureScreenCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESModule::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESModule::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESModule::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESModule::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESModule::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESModule::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESModule::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESModule::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESModule::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESModule::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESModule::createDeviceCb, 0);
}

NCB_REGISTER_CLASS(GLESAdaptor) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(getModule, &GLESAdaptor::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESAdaptor::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESAdaptor::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESAdaptor::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESAdaptor::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject,
                            &GLESAdaptor::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESAdaptor::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESAdaptor::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen,
                            &GLESAdaptor::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESAdaptor::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESAdaptor::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESAdaptor::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESAdaptor::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESAdaptor::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESAdaptor::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESAdaptor::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESAdaptor::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESAdaptor::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &GLESAdaptor::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &GLESAdaptor::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESAdaptor::finalizeCb, 0);
}

// 名字面与 GLESAdaptor 保持一致：krkrz 的游戏会把 GL 设备当成 draw device 用，
// 调到的就是这一套接口。
NCB_REGISTER_CLASS(OGLDrawDevice) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_METHOD_RAW_CALLBACK(getModule, &OGLDrawDevice::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &OGLDrawDevice::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &OGLDrawDevice::makeCurrentCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &OGLDrawDevice::beginSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &OGLDrawDevice::endSceneCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject,
                            &OGLDrawDevice::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &OGLDrawDevice::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &OGLDrawDevice::glesCaptureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &OGLDrawDevice::captureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen,
                            &OGLDrawDevice::glesCaptureScreenCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &OGLDrawDevice::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &OGLDrawDevice::glesCopyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &OGLDrawDevice::glesDrawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &OGLDrawDevice::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &OGLDrawDevice::drawAffineGLESCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &OGLDrawDevice::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &OGLDrawDevice::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &OGLDrawDevice::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &OGLDrawDevice::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &OGLDrawDevice::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &OGLDrawDevice::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &OGLDrawDevice::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &OGLDrawDevice::finalizeCb, 0);
}

NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowPassThroughDrawDevice,
                            Window.PassThroughDrawDevice,
                            DrawDeviceGetModuleCb);
NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowBasicDrawDevice,
                            Window.BasicDrawDevice, DrawDeviceGetModuleCb);

extern "C" void TVPRegisterKrkrGLESPluginAnchor() {}
