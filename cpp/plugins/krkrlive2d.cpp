#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define L2D_LOGI(...)                                                          \
    __android_log_print(ANDROID_LOG_INFO, "krkrlive2d", __VA_ARGS__)
#define L2D_LOGW(...)                                                          \
    __android_log_print(ANDROID_LOG_WARN, "krkrlive2d", __VA_ARGS__)
#else
#define L2D_LOGI(...) ((void)0)
#define L2D_LOGW(...) ((void)0)
#endif
#include "tjs.h"
#include "ncbind.hpp"
#include "StorageIntf.h"
#include "EventIntf.h"
#include "WindowIntf.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <minizip/unzip.h>
#include <minizip/ioapi.h>

// Cubism SDK
#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismModelUserData.hpp"
#include "Model/CubismUserModel.hpp"
#include "Motion/CubismMotion.hpp"
#include "Motion/CubismMotionManager.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Id/CubismIdManager.hpp"

#define NCB_MODULE_NAME TJS_W("krkrlive2d.dll")

using namespace Live2D::Cubism::Framework;

// Texture loaders defined in krkrgles.cpp
extern "C" GLuint LoadKtxTexture(const uint8_t *data, size_t dataSize);
extern "C" GLuint LoadPngTexture(const uint8_t *data, size_t dataSize);

// Shared render target so krkrgles.cpp copyLayer can read from
// the Live2D model's internal FBO.
struct Live2DRenderTarget {
    GLuint fbo = 0;
    GLsizei width = 0;
    GLsizei height = 0;
};
Live2DRenderTarget g_live2dRenderTarget;

extern void TVPSetPostDrawHook(void (*hook)());

// Registered Layer from krkrgles.cpp — used to blit Live2D content
extern iTJSDispatch2 *KrkrGLES_GetRegisteredLayer();

// CopyFBOToLayer from krkrgles.cpp — auto-switches GPU/CPU path.
// prevFbo: caller-saved FBO to restore after blit (-1 to query internally).
extern bool CopyFBOToLayer(GLuint fbo, GLsizei srcW, GLsizei srcH,
                           iTJSDispatch2 *layer, GLint prevFbo = -1);

// 是否正处在 krkrgles 的 capture() 回调里（见 krkrgles.cpp）。为真时，
// 脚本调 model.render() 的语义是"画进调用方当前绑定的目标"，而不只是更新内部 FBO。
extern "C" bool KrkrGLES_IsCaptureActive();

class CubismLive2DModel; // forward
static std::vector<CubismLive2DModel *> g_activeModels;
static void EnsureContinuousHook(); // forward

// ---------------------------------------------------------------------------
// GL 纹理缓存（按归档内路径键控）
//
// 为什么必须有：同一个 CG 的 `*.4096/texture_00.ktx` 是一份 ~21MB 的 BC7 KTX，
// 真机上单次 BC7 软解就是 150ms 量级（4096² 146ms + 2048² 41ms，见 krkrgles 的
// "BC7 decode" 日志）。而"切 CG / 轮播"这类操作会让同一路径在一局里被加载多次
// （引擎会为同一个 baseName 重新 new 一个 Live2DModel），原来的实现每次都重新
// 解码、重新上传并生成一张**新的** GL 纹理 —— 真机日志实测同一模型 3.2 秒内
// 完整加载两遍，纹理 id 从 356 变成 380，前一张直接泄漏（约 40MB 常驻）。
//
// 语义：引用计数。多个模型/多次加载共享同一张 GL 纹理，最后一个引用释放时才删除。
// 只在渲染线程访问（与本插件其余 GL 状态一致），因此只需要一把互斥锁保护跨线程的
// 计数，不需要额外的线程亲和处理。
// ---------------------------------------------------------------------------
namespace {
    struct CachedTexture {
        GLuint id = 0;
        int refs = 0;
    };

    std::mutex g_textureCacheMutex;
    std::unordered_map<std::string, CachedTexture> g_textureCache;

    // 命中返回 true 并 +1 引用；未命中返回 false（调用方负责上传并 RegisterTexture）。
    bool AcquireCachedTexture(const std::string &key, GLuint *outId) {
        if(key.empty() || !outId)
            return false;
        std::lock_guard<std::mutex> lock(g_textureCacheMutex);
        auto it = g_textureCache.find(key);
        if(it == g_textureCache.end() || it->second.id == 0)
            return false;
        it->second.refs++;
        *outId = it->second.id;
        return true;
    }

    // 登记一张新上传的纹理并返回该键**当前有效**的 GL id。
    //
    // 正常情况下这里必然是首次登记（命中缓存时调用方走的是 AcquireCachedTexture，
    // 不会走到这里），返回的就是传入的 id。只有并发/重复登记才会命中 else 分支；
    // 那时必须把本次多出来的那张删掉，并**返回既有 id**给调用方 —— 否则调用方会
    // 拿着一个刚被删除的 id 去渲染（悬垂纹理）。
    GLuint RegisterTexture(const std::string &key, GLuint id) {
        if(key.empty() || id == 0)
            return id;
        std::lock_guard<std::mutex> lock(g_textureCacheMutex);
        auto &entry = g_textureCache[key];
        if(entry.id == 0) {
            entry.id = id;
            entry.refs = 1;
            return id;
        }
        if(entry.id != id) {
            GLuint dup = id;
            glDeleteTextures(1, &dup);
        }
        // 本次调用同样持有一个引用。
        entry.refs++;
        return entry.id;
    }

    // 释放一个引用；引用归零时删除 GL 纹理并从表里摘除。
    void ReleaseTextureRef(const std::string &key) {
        if(key.empty())
            return;
        std::lock_guard<std::mutex> lock(g_textureCacheMutex);
        auto it = g_textureCache.find(key);
        if(it == g_textureCache.end())
            return;
        if(--it->second.refs <= 0) {
            if(it->second.id) {
                GLuint id = it->second.id;
                glDeleteTextures(1, &id);
            }
            g_textureCache.erase(it);
        }
    }

    // 清空纹理缓存；返回记录的条目数。
    //
    // `deleteGlObjects` 区分两种场景：
    //   - true：当前 GL context 有效（正常游戏内清理），把 GL 纹理一并删掉。
    //   - false：正在做插件卸载 / runtime-restart 收尾。此时调用方（engine_destroy）
    //     **没有**保证 EGL context 仍然 current，而在无 current context 时调
    //     glDeleteTextures 是未定义行为；何况那个 context 马上就要销毁，纹理会随
    //     它一起消失。这里只需要把记录丢掉，避免把失效的 id 交给新 context 复用。
    size_t ClearTextureCache(bool deleteGlObjects = true) {
        std::lock_guard<std::mutex> lock(g_textureCacheMutex);
        const size_t n = g_textureCache.size();
        if(deleteGlObjects) {
            for(auto &kv : g_textureCache) {
                if(kv.second.id) {
                    GLuint id = kv.second.id;
                    glDeleteTextures(1, &id);
                }
            }
        }
        g_textureCache.clear();
        return n;
    }
} // namespace

// configure 期由 scripts/gen_embedded_shaders.py 生成（见
// cpp/plugins/CMakeLists.txt）： 按文件名返回 SDK 的 GLES2 着色器源码。
extern "C" const unsigned char *KrkrLive2DEmbeddedShader(const char *path,
                                                         int *outSize);

// ---------------------------------------------------------------------------
// Cubism Allocator — uses standard malloc/free
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// KRKR_RENDER_PROBE：TJS 侧调用面探针（与 krkrgles.cpp 同款）
//
// 目的：确认游戏到底怎么驱动 Live2D（是脚本每帧调 model.render()，还是靠本插件
// 注册的连续动画钩子），以及呈现走了哪条路。按「方法名+参数个数+参数类型序列」
// 去重，只在该签名首次出现时打印一次（高频回调，必须去重）。
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

    class CubismAllocator : public ICubismAllocator {
    public:
        void *Allocate(const csmSizeType size) override {
            return std::malloc(size);
        }
        void Deallocate(void *mem) override { std::free(mem); }
        void *AllocateAligned(const csmSizeType size,
                              const csmUint32 alignment) override {
            size_t offset = alignment - 1 + sizeof(void *);
            void *raw = std::malloc(size + offset);
            if(!raw)
                return nullptr;
            void **aligned = reinterpret_cast<void **>(
                (reinterpret_cast<size_t>(raw) + offset) &
                ~(size_t(alignment) - 1));
            aligned[-1] = raw;
            return aligned;
        }
        void DeallocateAligned(void *mem) override {
            if(mem)
                std::free(static_cast<void **>(mem)[-1]);
        }
    };

    static CubismAllocator s_allocator;
    static bool s_cubismInitialized = false;

    // -----------------------------------------------------------------------
    // Cubism 的着色器加载回调
    //
    // CubismShader_OpenGLES2::GenerateShaders() 会以**裸文件名**调用它们
    // （"VertShaderSrc.vert" / "FragShaderSrc.frag" …）。渲染器不内背着色器，
    // 必须由宿主经 Option::LoadFileFunction 提供，否则 StartUp() 存下的
    // Option 里该字段是未初始化的野指针，一调用就 SIGSEGV。
    //
    // 我们只服务内嵌的着色器；查不到就返回 nullptr，让 SDK 走它自己的
    // "Failed to load vertex/fragment shader" 报错路径，而不是崩掉。
    // -----------------------------------------------------------------------
    csmByte *Live2DLoadFile(const std::string filePath, csmSizeInt *outSize) {
        if(!outSize)
            return nullptr;
        int size = 0;
        const unsigned char *data =
            KrkrLive2DEmbeddedShader(filePath.c_str(), &size);
        if(!data || size <= 0) {
            *outSize = 0;
            return nullptr;
        }
        // 必须由 ReleaseBytesFunction 释放，故统一走 Cubism 自己的分配器
        // （GetAllocator() 是 SDK 内部的，公开入口是
        // CubismFramework::Allocate）。
        csmByte *buf = reinterpret_cast<csmByte *>(
            CubismFramework::Allocate(static_cast<csmSizeType>(size)));
        if(!buf) {
            *outSize = 0;
            return nullptr;
        }
        std::memcpy(buf, data, static_cast<size_t>(size));
        *outSize = static_cast<csmSizeInt>(size);
        return buf;
    }

    void Live2DReleaseBytes(csmByte *byteData) {
        if(byteData)
            CubismFramework::Deallocate(byteData);
    }

    /**
     * 交给 CubismFramework::StartUp() 的 Option。
     *
     * **必须是静态存储期**。SDK 的 StartUp() 实现只有一句
     * `s_option = option;`：它**存指针、不拷贝**。之后
     * GetLoadFileFunction() / GetReleaseBytesFunction() / GetLoggingLevel()
     * 都靠 `s_option->...` 取值，其中第一个还没有空值保护。
     *
     * 这曾经是真机 SIGSEGV 的真因：Option 原先是本函数的**局部变量**，
     * 函数一返回栈就被复用，s_option 随即悬垂。后果很有迷惑性：
     *   * 读出来的不是 null，所以 `if (!fileLoader)` 拦不住；
     *   * 崩溃点恒定在取回调那一句（该函数第一件事就是取）；
     *   * 日志里**没有** "File loader is not set"，因为那行本身没被走到。
     * 放成静态后，指针在整个进程生命周期内有效。
     */
    CubismFramework::Option &GetCubismOption() {
        static CubismFramework::Option opt = [] {
            CubismFramework::Option o = {};
            o.LogFunction = [](const char *msg) {
                spdlog::debug("Cubism: {}", msg);
            };
            o.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
            o.LoadFileFunction = &Live2DLoadFile;
            o.ReleaseBytesFunction = &Live2DReleaseBytes;
            return o;
        }();
        return opt;
    }

    void EnsureCubismInitialized() {
        if(s_cubismInitialized)
            return;
        CubismFramework::StartUp(&s_allocator, &GetCubismOption());
        CubismFramework::Initialize();
        s_cubismInitialized = true;
        spdlog::info("krkrlive2d: Cubism SDK initialized");
    }

    // ---------------------------------------------------------------------------
    // ZIP helper — extract ALL entries from a ZIP archive in one pass.
    // Uses minizip custom IO to read directly from memory (no temp file).
    // ---------------------------------------------------------------------------
    using ZipArchive = std::unordered_map<std::string, std::vector<uint8_t>>;

    struct MemZipStream {
        const uint8_t *data;
        size_t size;
        size_t pos;
    };

    static voidpf ZCALLBACK mem_open_func(voidpf opaque, const char *, int) {
        return opaque;
    }
    static uLong ZCALLBACK mem_read_func(voidpf, voidpf stream, void *buf,
                                         uLong sz) {
        auto *s = static_cast<MemZipStream *>(stream);
        size_t avail = (s->pos < s->size) ? s->size - s->pos : 0;
        size_t n = (sz < avail) ? sz : avail;
        if(n > 0) {
            std::memcpy(buf, s->data + s->pos, n);
            s->pos += n;
        }
        return static_cast<uLong>(n);
    }
    static uLong ZCALLBACK mem_write_func(voidpf, voidpf, const void *, uLong) {
        return 0;
    }
    static long ZCALLBACK mem_tell_func(voidpf, voidpf stream) {
        return static_cast<long>(static_cast<MemZipStream *>(stream)->pos);
    }
    static long ZCALLBACK mem_seek_func(voidpf, voidpf stream, uLong offset,
                                        int origin) {
        auto *s = static_cast<MemZipStream *>(stream);
        size_t newpos = 0;
        switch(origin) {
            case ZLIB_FILEFUNC_SEEK_SET:
                newpos = offset;
                break;
            case ZLIB_FILEFUNC_SEEK_CUR:
                newpos = s->pos + offset;
                break;
            case ZLIB_FILEFUNC_SEEK_END:
                newpos = s->size + offset;
                break;
            default:
                return -1;
        }
        if(newpos > s->size)
            return -1;
        s->pos = newpos;
        return 0;
    }
    static int ZCALLBACK mem_close_func(voidpf, voidpf) { return 0; }
    static int ZCALLBACK mem_error_func(voidpf, voidpf) { return 0; }

    static bool ExtractZipToMemory(const uint8_t *zipData, size_t zipSize,
                                   ZipArchive &out) {
        out.clear();
        MemZipStream ms = { zipData, zipSize, 0 };

        zlib_filefunc_def funcs = {};
        funcs.zopen_file = mem_open_func;
        funcs.zread_file = mem_read_func;
        funcs.zwrite_file = mem_write_func;
        funcs.ztell_file = mem_tell_func;
        funcs.zseek_file = mem_seek_func;
        funcs.zclose_file = mem_close_func;
        funcs.zerror_file = mem_error_func;
        funcs.opaque = &ms;

        unzFile zf = unzOpen2(nullptr, &funcs);
        if(!zf)
            return false;

        int ret = unzGoToFirstFile(zf);
        while(ret == UNZ_OK) {
            char name[512];
            unz_file_info info;
            unzGetCurrentFileInfo(zf, &info, name, sizeof(name), nullptr, 0,
                                  nullptr, 0);
            if(info.uncompressed_size > 0 && unzOpenCurrentFile(zf) == UNZ_OK) {
                std::vector<uint8_t> buf(info.uncompressed_size);
                int bytesRead = unzReadCurrentFile(
                    zf, buf.data(), static_cast<unsigned>(buf.size()));
                if(bytesRead == static_cast<int>(buf.size()))
                    out[std::string(name)] = std::move(buf);
                unzCloseCurrentFile(zf);
            }
            ret = unzGoToNextFile(zf);
        }
        unzClose(zf);
        return !out.empty();
    }

    // ---------------------------------------------------------------------------
    // Load file from krkr2 storage system (XP3 / filesystem)
    // ---------------------------------------------------------------------------
    static bool LoadFromStorage(const ttstr &path, std::vector<uint8_t> &out) {
        try {
            tTJSBinaryStream *stream = TVPCreateStream(path, TJS_BS_READ);
            if(!stream)
                return false;
            tjs_uint64 size = stream->GetSize();
            out.resize(static_cast<size_t>(size));
            stream->ReadBuffer(out.data(), static_cast<tjs_uint>(size));
            delete stream;
            return true;
        } catch(...) {
            return false;
        }
    }

    // ---------------------------------------------------------------------------
    // Helper conversions
    // ---------------------------------------------------------------------------
    inline ttstr ToTTStr(const tTJSVariant &v) {
        return (v.Type() == tvtVoid) ? ttstr() : ttstr(v);
    }

    inline std::string ToKey(const tTJSVariant &v) {
        return ToTTStr(v).AsStdString();
    }

    inline tjs_real ToReal(const tTJSVariant &v, tjs_real fb = 0.0) {
        switch(v.Type()) {
            case tvtInteger:
            case tvtReal:
                return static_cast<tjs_real>(v);
            default:
                return fb;
        }
    }

    inline tjs_int ToInt(const tTJSVariant &v, tjs_int fb = 0) {
        switch(v.Type()) {
            case tvtInteger:
                return static_cast<tjs_int>(v);
            case tvtReal:
                return static_cast<tjs_int>(static_cast<tjs_real>(v));
            default:
                return fb;
        }
    }

    inline void SetResultObject(tTJSVariant *r, iTJSDispatch2 *o) {
        if(r && o)
            *r = tTJSVariant(o, o);
    }

    inline iTJSDispatch2 *CreateStringArray(const std::vector<ttstr> &items) {
        iTJSDispatch2 *arr = TJSCreateArrayObject();
        if(!arr)
            return nullptr;
        for(tjs_int i = 0; i < static_cast<tjs_int>(items.size()); ++i) {
            tTJSVariant v(items[static_cast<size_t>(i)]);
            arr->PropSetByNum(TJS_MEMBERENSURE, i, &v, arr);
        }
        return arr;
    }

    inline iTJSDispatch2 *CreateIdNameDict(const ttstr &id, const ttstr &name) {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return nullptr;
        tTJSVariant vId(id), vName(name);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("id"), nullptr, &vId, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &vName, dict);
        return dict;
    }

} // namespace

// ---------------------------------------------------------------------------
// CubismLive2DModel — wraps CubismUserModel for the engine
// ---------------------------------------------------------------------------
class CubismLive2DModel : public CubismUserModel {
public:
    struct MosaicRect {
        GLint x = 0;
        GLint y = 0;
        GLsizei w = 0;
        GLsizei h = 0;
    };

    CubismLive2DModel() : CubismUserModel() { g_activeModels.push_back(this); }
    ~CubismLive2DModel() override {
        g_activeModels.erase(
            std::remove(g_activeModels.begin(), g_activeModels.end(), this),
            g_activeModels.end());
        DestroyInternalFBO();
        ReleaseTextures();
    }

    bool LoadFromL2D(const std::vector<uint8_t> &zipData,
                     const std::string &baseName) {
        EnsureCubismInitialized();
        baseName_ = baseName;

        // 允许同一实例重复加载：先释放上一轮的纹理引用，否则引用计数会只增不减，
        // 缓存里的纹理永远删不掉（内存泄漏），且 textureIds_ 会越滚越长。
        ReleaseTextures();

        ZipArchive archive;
        if(!ExtractZipToMemory(zipData.data(), zipData.size(), archive)) {
            spdlog::error("krkrlive2d: failed to extract ZIP for {}", baseName);
            return false;
        }

        // Find and parse model3.json
        std::string modelJsonName = baseName + ".model3.json";
        auto jsonIt = archive.find(modelJsonName);
        if(jsonIt == archive.end()) {
            spdlog::error("krkrlive2d: model3.json not found in {}",
                          modelJsonName);
            return false;
        }

        setting_ = CSM_NEW CubismModelSettingJson(
            jsonIt->second.data(),
            static_cast<csmSizeType>(jsonIt->second.size()));
        if(!setting_)
            return false;

        // Load .moc3
        std::string mocName;
        if(setting_->GetModelFileName()) {
            csmString s = setting_->GetModelFileName();
            mocName = std::string(s.GetRawString());
        }
        if(mocName.empty())
            mocName = baseName + ".moc3";

        auto mocIt = archive.find(mocName);
        if(mocIt == archive.end()) {
            spdlog::error("krkrlive2d: moc3 not found: {}", mocName);
            return false;
        }

        LoadModel(mocIt->second.data(),
                  static_cast<csmSizeType>(mocIt->second.size()));
        if(!_moc || !_model) {
            spdlog::error("krkrlive2d: failed to create model from moc");
            return false;
        }

        DetectMosaicDrawables(archive);

        // Load textures
        csmInt32 texCount = setting_->GetTextureCount();
        for(csmInt32 i = 0; i < texCount; ++i) {
            std::string texPath;
            if(setting_->GetTextureFileName(i)) {
                csmString s = setting_->GetTextureFileName(i);
                texPath = std::string(s.GetRawString());
            }
            if(texPath.empty())
                continue;

            std::string ktxPath = texPath;
            auto dotPos = ktxPath.rfind('.');
            if(dotPos != std::string::npos)
                ktxPath = ktxPath.substr(0, dotPos) + ".ktx";

            L2D_LOGI("tex #%d: texPath='%s' ktxPath='%s'", i, texPath.c_str(),
                     ktxPath.c_str());
            // L2D_LOGI 在 Android 上只进 logcat，而真机取证只能靠 engine.log，
            // 所以纹理分支的关键结论必须同时走 spdlog。
            spdlog::info("krkrlive2d: tex #{}: texPath='{}' ktxPath='{}'", i,
                         texPath, ktxPath);

            GLuint texId = 0;
            // 缓存键：实际命中的归档内路径。KTX 与 PNG 分开记，避免
            // "KTX 缺失退化成 PNG" 的场景与纯 KTX 路径互相覆盖。
            std::string cacheKey;

            auto ktxIt = archive.find(ktxPath);
            if(ktxIt != archive.end()) {
                cacheKey = ktxPath;
                if(AcquireCachedTexture(cacheKey, &texId)) {
                    // 命中：跳过 20MB 的 BC7 软解与重复上传。
                    spdlog::info("krkrlive2d: tex #{}: 纹理缓存命中 '{}' -> "
                                 "texId={}（跳过重复解码/上传）",
                                 i, cacheKey, static_cast<unsigned>(texId));
                } else {
                    L2D_LOGI("tex #%d: found KTX in archive (%zu bytes)", i,
                             ktxIt->second.size());
                    texId = LoadKtxTexture(ktxIt->second.data(),
                                           ktxIt->second.size());
                    if(texId) {
                        L2D_LOGI("tex #%d: KTX loaded OK (texId=%u)", i, texId);
                        // 用登记后的"有效 id"：并发/重复登记时它会换成既有那张。
                        texId = RegisterTexture(cacheKey, texId);
                    } else {
                        L2D_LOGW("tex #%d: KTX load FAILED", i);
                    }
                    spdlog::info("krkrlive2d: tex #{}: KTX found ({} bytes) -> "
                                 "texId={}（已入缓存）",
                                 i, ktxIt->second.size(),
                                 static_cast<unsigned>(texId));
                }
            } else {
                L2D_LOGI("tex #%d: KTX not found in archive", i);
                spdlog::warn("krkrlive2d: tex #{}: KTX 不在包里（key='{}'，"
                             "包里共 {} 项）",
                             i, ktxPath, archive.size());
            }

            if(!texId) {
                auto texIt = archive.find(texPath);
                if(texIt != archive.end()) {
                    cacheKey = texPath;
                    if(AcquireCachedTexture(cacheKey, &texId)) {
                        spdlog::info("krkrlive2d: tex #{}: 纹理缓存命中 '{}' "
                                     "-> texId={}（跳过重复解码/上传）",
                                     i, cacheKey,
                                     static_cast<unsigned>(texId));
                    } else {
                        L2D_LOGI("tex #%d: found PNG in archive (%zu bytes)", i,
                                 texIt->second.size());
                        texId = LoadPngTexture(texIt->second.data(),
                                               texIt->second.size());
                        if(texId) {
                            L2D_LOGI("tex #%d: PNG loaded OK (texId=%u)", i,
                                     texId);
                            texId = RegisterTexture(cacheKey, texId);
                        } else {
                            L2D_LOGW("tex #%d: PNG decode FAILED", i);
                        }
                    }
                } else {
                    L2D_LOGW("tex #%d: PNG not found in archive either", i);
                    spdlog::warn("krkrlive2d: tex #{}: PNG 也不在包里"
                                 "（key='{}'）",
                                 i, texPath);
                }
            }

            if(!texId) {
                glGenTextures(1, &texId);
                glBindTexture(GL_TEXTURE_2D, texId);
                uint32_t white = 0xFFFFFFFF;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, &white);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, 0);
                L2D_LOGW("tex #%d: using 1x1 white placeholder", i);
                spdlog::warn("krkrlive2d: tex #{}: 用 1x1 白色占位纹理"
                             "（纹理没加载到，模型会变成纯色）",
                             i);
                // 占位纹理不入缓存：它属于"这次加载失败"的产物，缓存下来会让
                // 后续加载永远拿到白色，掩盖真正的加载错误。缓存键置空即可。
                cacheKey.clear();
            }

            textureIds_.push_back(texId);
            textureKeys_.push_back(cacheKey);
        }

        {
            // "模型纹理槽 -> GL 纹理 id" 的对应关系，只在加载期打一次。
            std::string ids;
            for(auto id : textureIds_)
                ids += std::to_string(static_cast<unsigned>(id)) + " ";
            spdlog::info("krkrlive2d: 纹理槽 {} 个，GL id = [{}]",
                         textureIds_.size(), ids);
        }

        // SDK 的签名是 CreateRenderer(width, height, maskBufferCount = 1)，
        // 没有无参重载。尺寸按模型画布像素算——绘制用的 internalFbo_ 就依
        // GetCanvasWidthPixel/HeightPixel 建（见 ContinuousUpdate），两者一致。
        csmUint32 renderTargetW =
            static_cast<csmUint32>(GetModel()->GetCanvasWidthPixel());
        csmUint32 renderTargetH =
            static_cast<csmUint32>(GetModel()->GetCanvasHeightPixel());
        if(renderTargetW == 0)
            renderTargetW = 1920;
        if(renderTargetH == 0)
            renderTargetH = 1080;
        CreateRenderer(renderTargetW, renderTargetH);
        auto *renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
        if(!renderer) {
            spdlog::error("krkrlive2d: failed to create renderer");
            return false;
        }
        for(int i = 0; i < static_cast<int>(textureIds_.size()); ++i)
            renderer->BindTexture(static_cast<csmUint32>(i),
                                  textureIds_[static_cast<size_t>(i)]);
        renderer->SetMvpMatrix(&projMatrix_);
        renderer->IsPremultipliedAlpha(false);

        csmInt32 eyeBlinkCount = setting_->GetEyeBlinkParameterCount();
        for(csmInt32 i = 0; i < eyeBlinkCount; ++i) {
            CubismIdHandle pid = setting_->GetEyeBlinkParameterId(i);
            if(pid)
                _eyeBlinkIds.PushBack(pid);
        }
        if(_eyeBlinkIds.GetSize() > 0) {
            _eyeBlink = CubismEyeBlink::Create(setting_);
        }
        csmInt32 lipSyncCount = setting_->GetLipSyncParameterCount();
        for(csmInt32 i = 0; i < lipSyncCount; ++i) {
            CubismIdHandle pid = setting_->GetLipSyncParameterId(i);
            if(pid)
                _lipSyncIds.PushBack(pid);
        }

        // Load motions
        // 同时登记"组名 → 组内动作名"表：脚本侧 getMotionGroupName /
        // getMotionCount / getMotionName / startMotion 全靠它。原先这张表**从未被
        // 填充**（motionGroupNames_ 硬编码成 {"main"}、motionNames_ 只有读没有写），
        // 于是 getMotionCount 恒为 0、getMotionName 恒为空串 —— 游戏拿不到可播的
        // 动作名，切 CG/轮播动画时自然什么都不会发生。
        //
        // ⚠️ 组名必须以"对外暴露名"登记：Cubism 允许**未命名组**——
        // `"motions": { "": [...] }`，实测本作 ev_cg001_02s.l2d 正是这种（1 组 7 个
        // 动作，组名字面就是空串）。而游戏的 system/AffineSourceLive2D.tjs 是按固定
        // 组名 `main` 调 startMotion/getMotionCount 的。若直接把空串暴露出去，
        // `getMotionCount("main")` 恒为 0、`startMotion("main", n)` 永远找不到动作 ——
        // 表现就是「每次点击都在重放当前动画」。所以空组名按 `main` 对外，
        // 内部仍用真实组名索引 motions_（见 groupRealName_）。
        motionGroupNames_.clear();
        motionNames_.clear();
        groupRealName_.clear();
        csmInt32 groupCount = setting_->GetMotionGroupCount();
        for(csmInt32 g = 0; g < groupCount; ++g) {
            const csmChar *group = setting_->GetMotionGroupName(g);
            if(!group)
                continue;
            const std::string realGroup(group);
            const std::string externalGroup =
                realGroup.empty() ? std::string("main") : realGroup;
            bool groupRegistered = false;
            csmInt32 motionCount = setting_->GetMotionCount(group);
            for(csmInt32 m = 0; m < motionCount; ++m) {
                const csmChar *motionFile =
                    setting_->GetMotionFileName(group, m);
                if(!motionFile)
                    continue;
                std::string motionPath(motionFile);
                auto motionIt = archive.find(motionPath);
                if(motionIt != archive.end()) {
                    CubismMotion *motion =
                        static_cast<CubismMotion *>(CubismMotion::Create(
                            motionIt->second.data(),
                            static_cast<csmSizeType>(motionIt->second.size())));
                    if(motion) {
                        csmFloat32 fadeIn =
                            setting_->GetMotionFadeInTimeValue(group, m);
                        csmFloat32 fadeOut =
                            setting_->GetMotionFadeOutTimeValue(group, m);
                        if(fadeIn >= 0.f)
                            motion->SetFadeInTime(fadeIn);
                        if(fadeOut >= 0.f)
                            motion->SetFadeOutTime(fadeOut);
                        motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
                        std::string key =
                            realGroup + "_" + std::to_string(m);
                        motions_[key] = motion;
                        if(!groupRegistered) {
                            groupRegistered = true;
                            motionGroupNames_.push_back(ttstr(externalGroup));
                            groupRealName_[externalGroup] = realGroup;
                        }
                        // 脚本按**组内序号**定位动作（见 StartMotionByIndex），
                        // 所以这张表必须按 m 的顺序 push，不能按归档命中顺序。
                        auto &names = motionNames_[externalGroup];
                        if(names.size() <= static_cast<size_t>(m))
                            names.resize(static_cast<size_t>(m) + 1);
                        // 动作名取文件名主干（去掉目录、以及 .motion3.json 这类
                        // 全部后缀），例如 "motions/ev_mv001_02_00.motion3.json"
                        // → "ev_mv001_02_00"。用 rfind('.') 只会切掉最后一个点，
                        // 会得到带 ".motion3" 的怪名字。
                        std::string display = motionPath;
                        auto slashPos = display.find_last_of("/\\");
                        if(slashPos != std::string::npos)
                            display = display.substr(slashPos + 1);
                        auto dotPos = display.find('.');
                        if(dotPos != std::string::npos)
                            display = display.substr(0, dotPos);
                        names[static_cast<size_t>(m)] =
                            ttstr(display.empty() ? key : display);
                    }
                }
            }
        }
        if(motionGroupNames_.empty()) {
            motionGroupNames_.push_back(TJS_W("main"));
            groupRealName_["main"] = "";
        }

        // 把"脚本能看到什么"完整记一次：轮播类问题只有对照这张表才能判断
        // 是「动作没登记」还是「组名/序号对不上」。
        {
            std::string table;
            for(const auto &grp : motionGroupNames_) {
                const std::string g = grp.AsStdString();
                table += g.empty() ? "(空)" : g;
                table += "[";
                auto it = motionNames_.find(g);
                if(it != motionNames_.end()) {
                    for(size_t i = 0; i < it->second.size(); ++i) {
                        if(i)
                            table += ",";
                        table += it->second[i].AsStdString();
                    }
                }
                table += "] ";
            }
            spdlog::info("krkrlive2d: 暴露给脚本的动作表: {}", table);
        }

        if(_motionManager && !motions_.empty()) {
            // 自动起播必须是**确定的**第一个动作（首组 #0），不能取
            // motions_.begin() —— 那是 unordered_map 的桶序，等于随机挑一个动作
            // （真机日志里就出现过"首播 '_5'"）。脚本没显式 startMotion 时，
            // 起播动作决定了玩家看到什么，随机是不可接受的。
            auto it = motions_.find(groupRealName_.empty()
                                        ? std::string()
                                        : groupRealName_.begin()->second + "_0");
            if(it == motions_.end())
                it = motions_.begin();
            _motionManager->StartMotionPriority(it->second, false, 1);
            autoFirstMotionKey_ = it->first;
            spdlog::debug("krkrlive2d: auto-started motion '{}'", it->first);
        }
        spdlog::info("krkrlive2d: motion 表就绪：{} 组 / {} 个动作（首播 '{}'）",
                     motionGroupNames_.size(), motions_.size(),
                     autoFirstMotionKey_);

        loaded_ = true;
        EnsureBlitProgram();
        EnsureContinuousHook();
        spdlog::info("krkrlive2d: model loaded: {} ({} parts, {} drawables, {} "
                     "textures, {} motions) "
                     "canvas={:.2f}x{:.2f} canvasPx={}x{} ppu={:.0f}",
                     baseName, GetModel()->GetPartCount(),
                     GetModel()->GetDrawableCount(), texCount,
                     static_cast<int>(motions_.size()),
                     GetModel()->GetCanvasWidth(),
                     GetModel()->GetCanvasHeight(),
                     static_cast<int>(GetModel()->GetCanvasWidthPixel()),
                     static_cast<int>(GetModel()->GetCanvasHeightPixel()),
                     GetModel()->GetPixelsPerUnit());
        return true;
    }

    void ContinuousUpdate(GLint savedFBO, const GLint savedVP[4]) {
        if(!loaded_ || !GetModel())
            return;

        auto now = std::chrono::steady_clock::now();
        float dt = 0.016f;
        if(lastUpdateTime_.time_since_epoch().count() > 0) {
            dt = std::chrono::duration<float>(now - lastUpdateTime_).count();
            if(dt < 0.002f)
                return;
            if(dt > 0.1f)
                dt = 0.1f;
        }
        lastUpdateTime_ = now;

        GetModel()->LoadParameters();
        if(_motionManager && _motionManager->IsFinished() &&
           !motions_.empty() && !motionStopped_) {
            // 续播"当前选中的动作"，而非无脑回到第一个。脚本切过动作后
            // （StartMotionByIndex 会刷新 selectedMotionKey_），这里才不会把它
            // 拽回 motions_.begin()，轮播才能真正连续。
            auto it = selectedMotionKey_.empty()
                ? motions_.find(autoFirstMotionKey_)
                : motions_.find(selectedMotionKey_);
            if(it == motions_.end())
                it = motions_.begin();
            _motionManager->StartMotionPriority(it->second, false, 1);
        }
        if(_motionManager)
            _motionManager->UpdateMotion(GetModel(), dt);
        GetModel()->SaveParameters();
        if(_eyeBlink)
            _eyeBlink->UpdateParameters(GetModel(), dt);
        GetModel()->Update();
        UpdateMosaicPartOpacity();

        GLsizei canvasW =
            static_cast<GLsizei>(GetModel()->GetCanvasWidthPixel());
        GLsizei canvasH =
            static_cast<GLsizei>(GetModel()->GetCanvasHeightPixel());
        if(canvasW <= 0)
            canvasW = 1920;
        if(canvasH <= 0)
            canvasH = 1080;
        EnsureInternalFBO(canvasW, canvasH);

        glBindFramebuffer(GL_FRAMEBUFFER, internalFbo_);
        glViewport(0, 0, fboW_, fboH_);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
#if defined(KRKR_RENDER_PROBE)
        ProbeFboStage("after-clear");
#endif

        auto *renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
        if(renderer) {
            UpdateProjection();
            renderer->SetMvpMatrix(&projMatrix_);
            renderer->DrawModel();
#if defined(KRKR_RENDER_PROBE)
            // 必须打在 DrawModel() **之后**：绘制前 SDK 还没绑 program 和纹理，
            // 那时读到的 program=0/unit0Tex=0 是正常状态，判不了"绑了哪张纹理"。
            ProbeDrawState();
#endif
        }
#if defined(KRKR_RENDER_PROBE)
        ProbeFboStage("after-draw");
#endif
        ApplyMosaicPostEffect();
#if defined(KRKR_RENDER_PROBE)
        ProbeFboStage("after-posteffect");
#endif

#if defined(KRKR_RENDER_PROBE)
        // 采样内部 FBO。旧指标 nonZero/maxA **证明不了"立绘渲染出来了"**：
        // 一整块 (255,255,255,0) 同样得 nonZero=9/9，而 maxA=255 只要 9 个点里
        // 有 1 个不透明就成立。所以改按 **alpha>0 的点数** 统计，并额外报中心像素
        // 与"白而透明"计数（预乘口径下 RGB>0 而 A=0 是非法像素）。
        // 4x4 网格 + 中心共 17 次读取，每个模型仍只做两次（README 硬约束 4）。
        {
            static int s_samples = 0;
            if(s_samples < 2) {
                ++s_samples;
                int aNonZero = 0, rgbNonZero = 0, whiteZero = 0, maxA = 0;
                unsigned char ctr[4] = { 0, 0, 0, 0 };
                glReadPixels(fboW_ / 2, fboH_ / 2, 1, 1, GL_RGBA,
                             GL_UNSIGNED_BYTE, ctr);
                for(int gy = 0; gy < 4; ++gy) {
                    for(int gx = 0; gx < 4; ++gx) {
                        unsigned char px[4] = { 0, 0, 0, 0 };
                        glReadPixels(fboW_ * (2 * gx + 1) / 8,
                                     fboH_ * (2 * gy + 1) / 8, 1, 1, GL_RGBA,
                                     GL_UNSIGNED_BYTE, px);
                        if(px[3] > 0)
                            ++aNonZero;
                        if(px[0] || px[1] || px[2])
                            ++rgbNonZero;
                        if(px[3] == 0 && px[0] == 255 && px[1] == 255 &&
                           px[2] == 255)
                            ++whiteZero;
                        if(px[3] > maxA)
                            maxA = px[3];
                    }
                }
                spdlog::info("[probe] krkrlive2d: internal FBO {}x{} "
                             "a>0={}/16 maxA={} rgbNonZero={}/16 "
                             "white+transparent={}/16 center=({},{},{},{})",
                             static_cast<int>(fboW_), static_cast<int>(fboH_),
                             aNonZero, maxA, rgbNonZero, whiteZero, ctr[0],
                             ctr[1], ctr[2], ctr[3]);
            }
        }
#endif

        g_live2dRenderTarget = { internalFbo_, fboW_, fboH_ };

        glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
        glViewport(savedVP[0], savedVP[1], savedVP[2], savedVP[3]);
    }

#if defined(KRKR_RENDER_PROBE)
    // 采 internalFbo_ 中心像素，分三个时机调用，用来区分：
    //   after-clear 就已经是不透明黑 ⇒ 清屏色/绑定被外面污染了；
    //   after-draw 才变黑          ⇒ 是模型自己画黑的。
    void ProbeFboStage(const char *stage) {
        static int s_stageSamples = 0;
        if(s_stageSamples >= 6) // 3 个时机 × 每个模型 2 次
            return;
        ++s_stageSamples;
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        unsigned char px[4] = { 0, 0, 0, 0 };
        glReadPixels(fboW_ / 2, fboH_ / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        spdlog::info("[probe] krkrlive2d: {} fbo={} internal={} "
                     "center=({},{},{},{})",
                     stage, static_cast<int>(fbo),
                     static_cast<int>(internalFbo_), px[0], px[1], px[2], px[3]);
    }

    // 绘制前一刻的 GL 状态：染色器程序、0 号纹理单元绑的是谁、那张纹理名是否有效、
    // blend 与 glGetError。**只用 ES2 入口点** —— 本文件只有 gl2.h/gl2ext.h，
    // ES3 的 glGetTexLevelParameteriv 连声明都没有（`GL_TEXTURE_WIDTH` 在 NDK 头里
    // 也不存在，CI 实测过）。所以要判"纹理有没有 level-0 图像"，靠的是 krkrgles
    // 那边纹理加载期的日志（`texId=` / `用 1x1 白色占位纹理`）与这里的 `unit0Tex`
    // 对账：两者不一致就是绑错了纹理，占位纹理则会在那边明确打出来。
    void ProbeDrawState() {
        static int s_stateSamples = 0;
        if(s_stateSamples >= 2)
            return;
        ++s_stateSamples;
        GLint prog = 0, activeUnit = 0, tex0 = 0, bsrcA = 0, bdstA = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        // 显式切到 0 号单元再读绑定：绘制结束后活动单元可能是 1/2（遮罩/混合），
        // 直接读 GL_TEXTURE_BINDING_2D 拿到的不一定是模型纹理那一张。
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        glActiveTexture(static_cast<GLenum>(activeUnit));
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &bdstA);
        spdlog::info("[probe] krkrlive2d: draw state program={} activeUnit=0x{:04X} "
                     "unit0Tex={} isTex0={} err=0x{:04X}",
                     static_cast<int>(prog), static_cast<unsigned>(activeUnit),
                     static_cast<int>(tex0),
                     tex0 ? static_cast<int>(glIsTexture(tex0)) : 0,
                     static_cast<unsigned>(glGetError()));
        spdlog::info("[probe] krkrlive2d: draw blend enabled={} srcA=0x{:04X} "
                     "dstA=0x{:04X}",
                     glIsEnabled(GL_BLEND) ? 1 : 0, static_cast<unsigned>(bsrcA),
                     static_cast<unsigned>(bdstA));
    }
#endif

    void BlitOverlay(GLint curFBO, const GLint vp[4]) {
        if(!loaded_ || !internalFbo_ || !fboTex_)
            return;
        BlitFBOToTarget(curFBO, vp[0], vp[1], vp[2], vp[3]);
    }

    void UpdateAndDraw(GLint savedFBO, const GLint savedVP[4]) {
        if(!loaded_ || !GetModel())
            return;
        ContinuousUpdate(savedFBO, savedVP);
    }

    void UpdateProjection() {
        CubismMatrix44 proj;
        proj.LoadIdentity();

        if(GetModel() && _modelMatrix) {
            float cw = GetModel()->GetCanvasWidth();
            float ch = GetModel()->GetCanvasHeight();
            if(cw > 0.f && ch > 0.f) {
                // _modelMatrix SetHeight(2.0) maps model to NDC height [-1,1],
                // width to [-cw/ch, cw/ch]. FBO aspect matches model aspect,
                // so scale X by ch/cw to fit width into [-1,1].
                proj.Scale(ch / cw, 1.0f);
            }
            proj.MultiplyByMatrix(_modelMatrix);
        }

        projMatrix_ = proj;
    }

    void SetRenderSize(int w, int h) {
        renderWidth_ = w;
        renderHeight_ = h;
    }

    // 把脚本给的组名解析成模型里的真实组名：
    //   1) 命中别名表（典型：未命名组按 "main" 暴露，真实组名是 ""）；
    //   2) 单组模型：任何没命中的组名都当成那唯一一组 —— 容忍脚本用
    //      main/idle/default/"" 等不同叫法，这些在单组模型里都是同一组；
    //   3) 都不成立就原样返回，让调用方按找不到处理（并打 warn）。
    std::string ResolveGroupName(const std::string &requested) const {
        auto it = groupRealName_.find(requested);
        if(it != groupRealName_.end())
            return it->second;
        if(groupRealName_.size() == 1)
            return groupRealName_.begin()->second;
        return requested;
    }

    // 返回是否真的切成功了（调用方据此决定要不要更新 selectedMotionKey_）。
    bool StartMotionByIndex(const std::string &group, int index) {
        const std::string real = ResolveGroupName(group);
        std::string key = real + "_" + std::to_string(index);
        auto it = motions_.find(key);
        if(it == motions_.end() || !_motionManager)
            return false;
        // priority 2 高于加载时的自动起播(1)，切动作才能立刻生效。
        _motionManager->StartMotionPriority(it->second, false, 2);
        selectedMotionKey_ = key;
        motionStopped_ = false;
        return true;
    }

    // 停止当前动作。清掉选中键，否则播完续播会把刚停掉的动作又拉回来；
    // 同时置 motionStopped_，让 ContinuousUpdate 的"播完续播"不再把它重新拉起
    // —— 脚本显式 stopMotion 之后，插件不应该跟它抢控制权。
    void StopMotion() {
        if(_motionManager)
            _motionManager->StopAllMotions();
        selectedMotionKey_.clear();
        motionStopped_ = true;
    }

    // 按"动作显示名"切动作。脚本的用法不止一种：
    //   1) startMotion(动作全名)      —— getMotionName 的返回值
    //   2) startMotion("00")          —— 只给文件名末尾的序号
    //   3) startMotion(组名, 序号)
    // 因此匹配时同时接受：全名、全名去掉组前缀后的尾段（最后一个 '_' 之后）、
    // 以及大小写不敏感的比较。全都对不上才算失败。
    static std::string MotionNameTail(const std::string &full) {
        const auto us = full.rfind('_');
        return us == std::string::npos ? std::string() : full.substr(us + 1);
    }

    static bool NameEqNoCase(const std::string &a, const std::string &b) {
        if(a.size() != b.size())
            return false;
        for(size_t i = 0; i < a.size(); ++i) {
            if(std::tolower(static_cast<unsigned char>(a[i])) !=
               std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    bool StartMotionByName(const std::string &name) {
        if(name.empty())
            return false;
        for(const auto &kv : motionNames_) {
            const auto &names = kv.second;
            for(size_t i = 0; i < names.size(); ++i) {
                const std::string full = names[i].AsStdString();
                if(full.empty())
                    continue;
                const std::string tail = MotionNameTail(full);
                if(NameEqNoCase(full, name) ||
                   (!tail.empty() && NameEqNoCase(tail, name))) {
                    return StartMotionByIndex(kv.first,
                                              static_cast<int>(i));
                }
            }
        }
        return false;
    }

    // 查"组内动作名表"。先按对外组名精确查；查不到且模型只有一组时退化为
    // 那一组（容忍脚本用 main/idle/"" 等不同叫法）。
    const std::vector<ttstr> *FindMotionNames(const std::string &group) const {
        auto it = motionNames_.find(group);
        if(it != motionNames_.end())
            return &it->second;
        if(motionNames_.size() == 1)
            return &motionNames_.begin()->second;
        return nullptr;
    }

    // 实际已加载的动作总数（不是组数）。
    size_t TotalMotionCount() const { return motions_.size(); }

    // 供脚本 getCurrentMotions 用：当前真正在播/选中的动作键。
    const std::string &SelectedMotionKey() const { return selectedMotionKey_; }
    const std::vector<ttstr> &MotionGroups() const { return motionGroupNames_; }
    bool IsLoaded() const { return loaded_; }

    // ── 可见性 ────────────────────────────────────────────────────────────
    // 脚本调 model.hide() 后必须真的停止"每帧更新 + 每帧整帧 blit"。
    // 原来的实现里 show/hide 是空壳，于是**每个**加载过的 CG 都会一直参与每帧
    // 渲染：切 N 个 CG 后每帧就有 N 次模型更新 + N 次全屏拷贝，这正是 KAG 模式下
    // 越玩越卡的直接原因（g_activeModels 只增不减）。
    void SetVisible(bool v) {
        if(visible_ == v)
            return;
        visible_ = v;
        // 重新显示时清掉陈旧的时间基准：否则隐藏期间累积的 dt 会在恢复的第一帧
        // 被夹到上限，动作会"跳"一下。
        if(visible_)
            lastUpdateTime_ = std::chrono::steady_clock::time_point();
    }
    bool IsVisible() const { return visible_; }

    void SetMosaicSize(float x, float y) {
        mosaicSizeX_ = x;
        mosaicSizeY_ = y;
    }

    bool HasMosaicDrawables() const { return !mosaicDrawableIndices_.empty(); }

private:
    static bool EqualsAsciiIgnoreCase(const char *lhs, const char *rhs) {
        if(!lhs || !rhs)
            return false;
        while(*lhs && *rhs) {
            if(std::tolower(static_cast<unsigned char>(*lhs)) !=
               std::tolower(static_cast<unsigned char>(*rhs)))
                return false;
            ++lhs;
            ++rhs;
        }
        return *lhs == '\0' && *rhs == '\0';
    }

    void DetectMosaicDrawables(const ZipArchive &archive) {
        mosaicDrawableIndices_.clear();
        mosaicParentPartIndices_.clear();
        mosaicParentOpacityDefaults_.clear();
        mosaicRects_.clear();
        mosaicCpuScratch_.clear();

        if(GetModel()) {
            GetModel()->ClearDrawableForceHiddenFlags();
        }
        if(!GetModel() || !setting_)
            return;

        std::string userDataPath;
        if(setting_->GetUserDataFile()) {
            csmString s = setting_->GetUserDataFile();
            userDataPath = s.GetRawString();
        }
        if(userDataPath.empty())
            userDataPath = baseName_ + ".userdata3.json";

        auto userDataIt = archive.find(userDataPath);
        if(userDataIt == archive.end()) {
            const size_t slash = userDataPath.find_last_of("/\\");
            if(slash != std::string::npos) {
                userDataIt = archive.find(userDataPath.substr(slash + 1));
            }
        }
        if(userDataIt == archive.end())
            return;

        CubismModelUserData *userData = CubismModelUserData::Create(
            userDataIt->second.data(),
            static_cast<csmSizeInt>(userDataIt->second.size()));
        if(!userData)
            return;

        std::unordered_set<csmInt32> drawableSet;
        std::unordered_set<csmInt32> partSet;
        const auto &nodes = userData->GetArtMeshUserDatas();
        for(csmUint32 i = 0; i < nodes.GetSize(); ++i) {
            const auto *node = nodes[i];
            if(!node)
                continue;
            if(!EqualsAsciiIgnoreCase(node->Value.GetRawString(), "mosaic"))
                continue;

            const csmInt32 drawableIndex =
                GetModel()->GetDrawableIndex(node->TargetId);
            if(drawableIndex < 0)
                continue;

            if(drawableSet.insert(drawableIndex).second) {
                mosaicDrawableIndices_.push_back(drawableIndex);
            }

            const csmInt32 partIndex = GetModel()->GetDrawableParentPartIndex(
                static_cast<csmUint32>(drawableIndex));
            if(partIndex >= 0 && partSet.insert(partIndex).second) {
                mosaicParentPartIndices_.push_back(partIndex);
                mosaicParentOpacityDefaults_[partIndex] =
                    GetModel()->GetPartOpacity(partIndex);
            }
        }

        CubismModelUserData::Delete(userData);

        if(!mosaicDrawableIndices_.empty()) {
            spdlog::info("krkrlive2d: detected {} mosaic drawables ({} parent "
                         "parts) in {}",
                         static_cast<int>(mosaicDrawableIndices_.size()),
                         static_cast<int>(mosaicParentPartIndices_.size()),
                         baseName_);
        }
    }

    bool IsMosaicEnabled() const {
        if(mosaicDrawableIndices_.empty())
            return false;
        const float x = (mosaicSizeX_ > 0.0f) ? mosaicSizeX_ : mosaicSizeY_;
        const float y = (mosaicSizeY_ > 0.0f) ? mosaicSizeY_ : mosaicSizeX_;
        return x >= 2.0f || y >= 2.0f;
    }

    int GetMosaicBlockX() const {
        float v = (mosaicSizeX_ > 0.0f) ? mosaicSizeX_ : mosaicSizeY_;
        if(v < 1.0f)
            v = 1.0f;
        int iv = static_cast<int>(std::lround(v));
        if(iv < 1)
            iv = 1;
        if(iv > 256)
            iv = 256;
        return iv;
    }

    int GetMosaicBlockY() const {
        float v = (mosaicSizeY_ > 0.0f) ? mosaicSizeY_ : mosaicSizeX_;
        if(v < 1.0f)
            v = 1.0f;
        int iv = static_cast<int>(std::lround(v));
        if(iv < 1)
            iv = 1;
        if(iv > 256)
            iv = 256;
        return iv;
    }

    void UpdateMosaicPartOpacity() {
        if(!GetModel())
            return;
        // Mosaic effect is disabled globally; always hide mosaic-tagged overlay
        // meshes.
        const bool hideSourceMesh = !mosaicEffectEnabled_ || IsMosaicEnabled();
        for(csmInt32 drawableIndex : mosaicDrawableIndices_) {
            if(drawableIndex < 0)
                continue;
            GetModel()->SetDrawableForceHidden(drawableIndex, hideSourceMesh);
        }

        if(mosaicParentPartIndices_.empty())
            return;
        for(csmInt32 partIndex : mosaicParentPartIndices_) {
            if(partIndex < 0)
                continue;
            auto it = mosaicParentOpacityDefaults_.find(partIndex);
            GetModel()->SetPartOpacity(
                partIndex,
                (it != mosaicParentOpacityDefaults_.end()) ? it->second : 1.0f);
        }
    }

    static bool RectsOverlapOrTouch(const MosaicRect &a, const MosaicRect &b,
                                    GLint pad) {
        const GLint ax1 = a.x + a.w + pad;
        const GLint ay1 = a.y + a.h + pad;
        const GLint bx1 = b.x + b.w + pad;
        const GLint by1 = b.y + b.h + pad;
        return !(ax1 < b.x || bx1 < a.x || ay1 < b.y || by1 < a.y);
    }

    static MosaicRect MergeRects(const MosaicRect &a, const MosaicRect &b) {
        const GLint x0 = std::min(a.x, b.x);
        const GLint y0 = std::min(a.y, b.y);
        const GLint x1 = std::max(a.x + a.w, b.x + b.w);
        const GLint y1 = std::max(a.y + a.h, b.y + b.h);
        MosaicRect r;
        r.x = x0;
        r.y = y0;
        r.w = x1 - x0;
        r.h = y1 - y0;
        return r;
    }

    void CollectMosaicRects() {
        mosaicRects_.clear();
        auto *model = GetModel();
        if(!model || mosaicDrawableIndices_.empty() || fboW_ <= 0 || fboH_ <= 0)
            return;

        constexpr GLint pad = 2;
        for(const csmInt32 drawableIndex : mosaicDrawableIndices_) {
            if(drawableIndex < 0 || drawableIndex >= model->GetDrawableCount())
                continue;

            const csmInt32 vertexCount =
                model->GetDrawableVertexCount(drawableIndex);
            const Live2D::Cubism::Core::csmVector2 *positions =
                model->GetDrawableVertexPositions(drawableIndex);
            if(vertexCount <= 0 || !positions)
                continue;

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();

            for(csmInt32 i = 0; i < vertexCount; ++i) {
                const float ndcX = projMatrix_.TransformX(positions[i].X);
                const float ndcY = projMatrix_.TransformY(positions[i].Y);
                const float px =
                    (ndcX * 0.5f + 0.5f) * static_cast<float>(fboW_);
                const float py =
                    (ndcY * 0.5f + 0.5f) * static_cast<float>(fboH_);
                minX = std::min(minX, px);
                minY = std::min(minY, py);
                maxX = std::max(maxX, px);
                maxY = std::max(maxY, py);
            }

            if(maxX <= minX || maxY <= minY)
                continue;

            const GLint x0 =
                std::max<GLint>(0, static_cast<GLint>(std::floor(minX)) - pad);
            const GLint y0 =
                std::max<GLint>(0, static_cast<GLint>(std::floor(minY)) - pad);
            const GLint x1 = std::min<GLint>(
                fboW_, static_cast<GLint>(std::ceil(maxX)) + pad);
            const GLint y1 = std::min<GLint>(
                fboH_, static_cast<GLint>(std::ceil(maxY)) + pad);
            if(x1 <= x0 || y1 <= y0)
                continue;

            MosaicRect rect;
            rect.x = x0;
            rect.y = y0;
            rect.w = x1 - x0;
            rect.h = y1 - y0;
            mosaicRects_.push_back(rect);
        }

        // Merge touching/overlapping rects to reduce readback/update count.
        bool merged = true;
        while(merged) {
            merged = false;
            for(size_t i = 0; i < mosaicRects_.size() && !merged; ++i) {
                for(size_t j = i + 1; j < mosaicRects_.size(); ++j) {
                    if(!RectsOverlapOrTouch(mosaicRects_[i], mosaicRects_[j],
                                            2))
                        continue;
                    mosaicRects_[i] =
                        MergeRects(mosaicRects_[i], mosaicRects_[j]);
                    mosaicRects_.erase(mosaicRects_.begin() + j);
                    merged = true;
                    break;
                }
            }
        }
    }

    void EnsureMosaicProgram() {
        if(mosaicProgram_ || !mosaicGpuEnabled_)
            return;
        const char *vs = "#version 100\n"
                         "attribute vec2 a_pos;\n"
                         "varying vec2 v_uv;\n"
                         "void main() {\n"
                         "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
                         "  v_uv = a_pos * 0.5 + 0.5;\n"
                         "}\n";
        const char *fs =
            "#version 100\n"
            "precision mediump float;\n"
            "varying vec2 v_uv;\n"
            "uniform sampler2D u_tex;\n"
            "uniform vec2 u_texSize;\n"
            "uniform vec2 u_blockSize;\n"
            "uniform vec2 u_uvOffset;\n"
            "uniform vec2 u_uvScale;\n"
            "void main() {\n"
            "  vec2 uv = u_uvOffset + v_uv * u_uvScale;\n"
            "  vec2 block = max(u_blockSize, vec2(1.0));\n"
            "  vec2 pix = floor((uv * u_texSize) / block) * block + 0.5 * "
            "block;\n"
            "  vec2 suv = clamp(pix / u_texSize, vec2(0.0), vec2(1.0));\n"
            "  gl_FragColor = texture2D(u_tex, suv);\n"
            "}\n";
        GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
        if(!vsh) {
            mosaicGpuEnabled_ = false;
            spdlog::warn(
                "krkrlive2d: disable GPU mosaic (create vertex shader failed)");
            return;
        }
        glShaderSource(vsh, 1, &vs, nullptr);
        glCompileShader(vsh);
        GLint vCompiled = GL_FALSE;
        glGetShaderiv(vsh, GL_COMPILE_STATUS, &vCompiled);
        GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
        if(!fsh) {
            glDeleteShader(vsh);
            mosaicGpuEnabled_ = false;
            spdlog::warn("krkrlive2d: disable GPU mosaic (create fragment "
                         "shader failed)");
            return;
        }
        glShaderSource(fsh, 1, &fs, nullptr);
        glCompileShader(fsh);
        GLint fCompiled = GL_FALSE;
        glGetShaderiv(fsh, GL_COMPILE_STATUS, &fCompiled);
        if(vCompiled != GL_TRUE || fCompiled != GL_TRUE) {
            glDeleteShader(vsh);
            glDeleteShader(fsh);
            mosaicGpuEnabled_ = false;
            spdlog::warn(
                "krkrlive2d: disable GPU mosaic (shader compile failed)");
            return;
        }
        mosaicProgram_ = glCreateProgram();
        if(!mosaicProgram_) {
            glDeleteShader(vsh);
            glDeleteShader(fsh);
            mosaicGpuEnabled_ = false;
            spdlog::warn(
                "krkrlive2d: disable GPU mosaic (create program failed)");
            return;
        }
        glAttachShader(mosaicProgram_, vsh);
        glAttachShader(mosaicProgram_, fsh);
        glBindAttribLocation(mosaicProgram_, 0, "a_pos");
        glLinkProgram(mosaicProgram_);
        glDeleteShader(vsh);
        glDeleteShader(fsh);
        GLint linked = GL_FALSE;
        glGetProgramiv(mosaicProgram_, GL_LINK_STATUS, &linked);
        if(linked != GL_TRUE) {
            glDeleteProgram(mosaicProgram_);
            mosaicProgram_ = 0;
            mosaicGpuEnabled_ = false;
            spdlog::warn(
                "krkrlive2d: disable GPU mosaic (program link failed)");
            return;
        }
        mosaicLocPos_ = glGetAttribLocation(mosaicProgram_, "a_pos");
        mosaicLocTex_ = glGetUniformLocation(mosaicProgram_, "u_tex");
        mosaicLocTexSize_ = glGetUniformLocation(mosaicProgram_, "u_texSize");
        mosaicLocBlock_ = glGetUniformLocation(mosaicProgram_, "u_blockSize");
        mosaicLocUvOffset_ = glGetUniformLocation(mosaicProgram_, "u_uvOffset");
        mosaicLocUvScale_ = glGetUniformLocation(mosaicProgram_, "u_uvScale");
        if(mosaicLocPos_ < 0 || mosaicLocTex_ < 0 || mosaicLocTexSize_ < 0 ||
           mosaicLocBlock_ < 0 || mosaicLocUvOffset_ < 0 ||
           mosaicLocUvScale_ < 0) {
            glDeleteProgram(mosaicProgram_);
            mosaicProgram_ = 0;
            mosaicGpuEnabled_ = false;
            spdlog::warn("krkrlive2d: disable GPU mosaic (uniform/attrib "
                         "lookup failed)");
        }
    }

    void EnsureMosaicSourceTexture(GLsizei w, GLsizei h) {
        if(w <= 0 || h <= 0 || !mosaicGpuEnabled_)
            return;
        if(!mosaicSrcTex_)
            glGenTextures(1, &mosaicSrcTex_);
        if(!mosaicSrcTex_) {
            mosaicGpuEnabled_ = false;
            spdlog::warn("krkrlive2d: disable GPU mosaic (create source "
                         "texture failed)");
            return;
        }
        glBindTexture(GL_TEXTURE_2D, mosaicSrcTex_);
        if(mosaicTexW_ != w || mosaicTexH_ != h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            mosaicTexW_ = w;
            mosaicTexH_ = h;
        }
        if(glGetError() != GL_NO_ERROR) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &mosaicSrcTex_);
            mosaicSrcTex_ = 0;
            mosaicTexW_ = mosaicTexH_ = 0;
            mosaicGpuEnabled_ = false;
            spdlog::warn("krkrlive2d: disable GPU mosaic (source texture "
                         "upload failed)");
            return;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool ApplyMosaicPostEffectGPU() {
        if(!mosaicGpuEnabled_)
            return false;
        EnsureMosaicProgram();
        EnsureMosaicSourceTexture(fboW_, fboH_);
        if(!mosaicProgram_ || !mosaicSrcTex_)
            return false;

        while(glGetError() != GL_NO_ERROR) {
        }
        glBindFramebuffer(GL_FRAMEBUFFER, internalFbo_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mosaicSrcTex_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, fboW_, fboH_);
        if(glGetError() != GL_NO_ERROR) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
            if(mosaicProgram_) {
                glDeleteProgram(mosaicProgram_);
                mosaicProgram_ = 0;
            }
            if(mosaicSrcTex_) {
                glDeleteTextures(1, &mosaicSrcTex_);
                mosaicSrcTex_ = 0;
            }
            mosaicTexW_ = mosaicTexH_ = 0;
            mosaicGpuEnabled_ = false;
            spdlog::warn("krkrlive2d: GPU mosaic failed at copy stage, "
                         "switching to CPU");
            return false;
        }

        glUseProgram(mosaicProgram_);
        glUniform1i(mosaicLocTex_, 0);
        glUniform2f(mosaicLocTexSize_, static_cast<float>(fboW_),
                    static_cast<float>(fboH_));
        glUniform2f(mosaicLocBlock_, static_cast<float>(GetMosaicBlockX()),
                    static_cast<float>(GetMosaicBlockY()));

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        static const float quad[] = {
            -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f
        };
        glEnableVertexAttribArray(mosaicLocPos_);
        glVertexAttribPointer(mosaicLocPos_, 2, GL_FLOAT, GL_FALSE, 0, quad);

        for(const auto &rect : mosaicRects_) {
            if(rect.w <= 0 || rect.h <= 0)
                continue;
            glViewport(rect.x, rect.y, rect.w, rect.h);
            const float uvOffsetX =
                static_cast<float>(rect.x) / static_cast<float>(fboW_);
            const float uvOffsetY =
                static_cast<float>(rect.y) / static_cast<float>(fboH_);
            const float uvScaleX =
                static_cast<float>(rect.w) / static_cast<float>(fboW_);
            const float uvScaleY =
                static_cast<float>(rect.h) / static_cast<float>(fboH_);
            glUniform2f(mosaicLocUvOffset_, uvOffsetX, uvOffsetY);
            glUniform2f(mosaicLocUvScale_, uvScaleX, uvScaleY);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        const bool ok = (glGetError() == GL_NO_ERROR);
        glDisableVertexAttribArray(mosaicLocPos_);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        if(!ok) {
            if(mosaicProgram_) {
                glDeleteProgram(mosaicProgram_);
                mosaicProgram_ = 0;
            }
            if(mosaicSrcTex_) {
                glDeleteTextures(1, &mosaicSrcTex_);
                mosaicSrcTex_ = 0;
            }
            mosaicTexW_ = mosaicTexH_ = 0;
            mosaicGpuEnabled_ = false;
            spdlog::warn("krkrlive2d: GPU mosaic failed at draw stage, "
                         "switching to CPU");
        }
        return ok;
    }

    void ApplyMosaicPostEffectCPU() {
        if(!internalFbo_ || !fboTex_)
            return;

        const int blockX = GetMosaicBlockX();
        const int blockY = GetMosaicBlockY();
        if(blockX <= 1 && blockY <= 1)
            return;

        GLint prevPack = 4;
        GLint prevUnpack = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &prevPack);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, fboTex_);

        for(const auto &rect : mosaicRects_) {
            if(rect.w <= 0 || rect.h <= 0)
                continue;

            const size_t pixCount =
                static_cast<size_t>(rect.w) * static_cast<size_t>(rect.h);
            const size_t byteCount = pixCount * 4u;
            if(mosaicCpuScratch_.size() < byteCount)
                mosaicCpuScratch_.resize(byteCount);

            uint8_t *buf = mosaicCpuScratch_.data();
            glReadPixels(rect.x, rect.y, rect.w, rect.h, GL_RGBA,
                         GL_UNSIGNED_BYTE, buf);
            if(glGetError() != GL_NO_ERROR)
                continue;

            for(int by = 0; by < rect.h; by += blockY) {
                const int sampleY = by;
                const int yMax =
                    std::min(by + blockY, static_cast<int>(rect.h));
                for(int bx = 0; bx < rect.w; bx += blockX) {
                    const int sampleX = bx;
                    const int xMax =
                        std::min(bx + blockX, static_cast<int>(rect.w));
                    const size_t sidx = (static_cast<size_t>(sampleY) *
                                             static_cast<size_t>(rect.w) +
                                         static_cast<size_t>(sampleX)) *
                        4u;
                    const uint8_t r = buf[sidx + 0];
                    const uint8_t g = buf[sidx + 1];
                    const uint8_t b = buf[sidx + 2];
                    const uint8_t a = buf[sidx + 3];

                    for(int y = by; y < yMax; ++y) {
                        const size_t row = static_cast<size_t>(y) *
                            static_cast<size_t>(rect.w);
                        for(int x = bx; x < xMax; ++x) {
                            const size_t didx =
                                (row + static_cast<size_t>(x)) * 4u;
                            buf[didx + 0] = r;
                            buf[didx + 1] = g;
                            buf[didx + 2] = b;
                            buf[didx + 3] = a;
                        }
                    }
                }
            }

            glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.w, rect.h,
                            GL_RGBA, GL_UNSIGNED_BYTE, buf);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, prevPack);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);
    }

    void ApplyMosaicPostEffect() {
        if(!mosaicEffectEnabled_)
            return;
        if(!IsMosaicEnabled() || !internalFbo_ || !fboTex_)
            return;
        CollectMosaicRects();
        if(mosaicRects_.empty())
            return;

        if(!ApplyMosaicPostEffectGPU()) {
            ApplyMosaicPostEffectCPU();
        }
    }

    void ReleaseTextures() {
        // 共享纹理按缓存键递减引用，只有最后一个引用释放时才真正删 GL 纹理；
        // 占位纹理（key 为空）没有共享者，直接删。
        for(size_t i = 0; i < textureIds_.size(); ++i) {
            const GLuint texId = textureIds_[i];
            if(!texId)
                continue;
            if(i < textureKeys_.size() && !textureKeys_[i].empty()) {
                ReleaseTextureRef(textureKeys_[i]);
            } else {
                GLuint id = texId;
                glDeleteTextures(1, &id);
            }
        }
        textureIds_.clear();
        textureKeys_.clear();
    }

    void EnsureInternalFBO(GLsizei w, GLsizei h) {
        if(w > 4096)
            w = 4096;
        if(h > 4096)
            h = 4096;
        if(internalFbo_ && fboW_ == w && fboH_ == h)
            return;
        DestroyInternalFBO();
        glGenFramebuffers(1, &internalFbo_);
        glGenTextures(1, &fboTex_);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, internalFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, fboTex_, 0);
        fboW_ = w;
        fboH_ = h;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        spdlog::debug("krkrlive2d: created internal FBO {}x{} fbo={} tex={}", w,
                      h, internalFbo_, fboTex_);
    }

    void DestroyInternalFBO() {
        if(mosaicProgram_) {
            glDeleteProgram(mosaicProgram_);
            mosaicProgram_ = 0;
        }
        if(mosaicSrcTex_) {
            glDeleteTextures(1, &mosaicSrcTex_);
            mosaicSrcTex_ = 0;
        }
        mosaicTexW_ = mosaicTexH_ = 0;
        if(blitProgram_) {
            glDeleteProgram(blitProgram_);
            blitProgram_ = 0;
        }
        if(fboTex_) {
            glDeleteTextures(1, &fboTex_);
            fboTex_ = 0;
        }
        if(internalFbo_) {
            glDeleteFramebuffers(1, &internalFbo_);
            internalFbo_ = 0;
        }
        fboW_ = fboH_ = 0;
    }

    void EnsureBlitProgram() {
        if(blitProgram_)
            return;
        const char *vs = "#version 100\n"
                         "attribute vec2 a_pos;\n"
                         "uniform vec2 u_scale;\n"
                         "uniform float u_flipY;\n"
                         "varying vec2 v_uv;\n"
                         "void main() {\n"
                         "  gl_Position = vec4(a_pos * u_scale, 0.0, 1.0);\n"
                         "  v_uv = vec2(a_pos.x * 0.5 + 0.5, u_flipY * a_pos.y "
                         "* 0.5 + 0.5);\n"
                         "}\n";
        const char *fs =
            "#version 100\n"
            "precision mediump float;\n"
            "varying vec2 v_uv;\n"
            "uniform sampler2D u_tex;\n"
            "void main() { gl_FragColor = texture2D(u_tex, v_uv); }\n";
        GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vsh, 1, &vs, nullptr);
        glCompileShader(vsh);
        GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fsh, 1, &fs, nullptr);
        glCompileShader(fsh);
        blitProgram_ = glCreateProgram();
        glAttachShader(blitProgram_, vsh);
        glAttachShader(blitProgram_, fsh);
        glBindAttribLocation(blitProgram_, 0, "a_pos");
        glLinkProgram(blitProgram_);
        glDeleteShader(vsh);
        glDeleteShader(fsh);
        locPos_ = glGetAttribLocation(blitProgram_, "a_pos");
        locTex_ = glGetUniformLocation(blitProgram_, "u_tex");
        locScale_ = glGetUniformLocation(blitProgram_, "u_scale");
        locFlipY_ = glGetUniformLocation(blitProgram_, "u_flipY");
    }

    void BlitFBOToTarget(GLint targetFBO, GLint vpX, GLint vpY, GLsizei vpW,
                         GLsizei vpH) {
        EnsureBlitProgram();
        if(!blitProgram_ || !fboTex_)
            return;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(vpX, vpY, vpW, vpH);

        glUseProgram(blitProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glUniform1i(locTex_, 0);

        float modelAspect =
            (fboH_ > 0) ? static_cast<float>(fboW_) / fboH_ : 1.78f;
        float vpAspect = (vpH > 0) ? static_cast<float>(vpW) / vpH : 1.78f;
        float sx = 1.0f, sy = 1.0f;
        if(modelAspect > vpAspect)
            sy = vpAspect / modelAspect;
        else if(modelAspect < vpAspect)
            sx = modelAspect / vpAspect;
        glUniform2f(locScale_, sx, sy);
#if defined(__ANDROID__)
        glUniform1f(locFlipY_,
                    1.0f); // Android: flip Y so display is right-side up
#else
        glUniform1f(locFlipY_, -1.0f); // Mac/desktop: keep current orientation
#endif
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
        glEnableVertexAttribArray(locPos_);
        glVertexAttribPointer(locPos_, 2, GL_FLOAT, GL_FALSE, 0, quad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(locPos_);

        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

    bool loaded_ = false;
    // 默认可见：不调 show()/hide() 的老游戏行为不变。
    bool visible_ = true;
    int renderWidth_ = 1920;
    int renderHeight_ = 1080;
    std::string baseName_;
    CubismModelSettingJson *setting_ = nullptr;
    std::vector<GLuint> textureIds_;
    // 与 textureIds_ 一一对应的缓存键（空串 = 占位纹理，不参与缓存/引用计数）。
    // 释放时按它递减引用，最后一个引用才真正 glDeleteTextures。
    std::vector<std::string> textureKeys_;
    CubismMatrix44 projMatrix_;
    std::unordered_map<std::string, ACubismMotion *> motions_;
    // 动作元数据表：与 motions_ 同属"模型自身的动作信息"，必须定义在本类里
    // （脚本侧 getMotionGroupName / getMotionCount / getMotionName 经
    // MotionGroups()/FindMotionNames() 读出去）。加载期由 LoadFromL2D 填充。
    std::vector<ttstr> motionGroupNames_;
    std::unordered_map<std::string, std::vector<ttstr>> motionNames_;
    // 对外暴露的组名 → 模型里的真实组名。两者不同只发生在**未命名组**上：
    // model3.json 的 `"motions": { "": [...] }` 会让真实组名为空串，而对脚本
    // 暴露成 "main"（游戏脚本按 main 调用）。见 LoadFromL2D 里的说明。
    std::unordered_map<std::string, std::string> groupRealName_;
    // 加载时自动起播的动作键（motions_ 的键："组_序号"）。动作播完后按它续播，
    // 而不是重新取 motions_.begin() —— 否则脚本切过的动作会被"跳回"第一个。
    std::string autoFirstMotionKey_;
    // 当前选中的动作键，由 StartMotionByIndex 成功后写入，供播完续播使用。
    std::string selectedMotionKey_;
    // 脚本显式 stopMotion 后置位，禁止 ContinuousUpdate 自动续播（否则插件会
    // 立刻把刚停掉的动作又拉起来）。任何一次成功的 startMotion 都会清掉它。
    bool motionStopped_ = false;
    csmVector<const CubismId *> _eyeBlinkIds;
    csmVector<const CubismId *> _lipSyncIds;
    std::vector<csmInt32> mosaicDrawableIndices_;
    std::vector<csmInt32> mosaicParentPartIndices_;
    std::unordered_map<csmInt32, csmFloat32> mosaicParentOpacityDefaults_;
    std::vector<MosaicRect> mosaicRects_;
    float mosaicSizeX_ = 24.0f;
    float mosaicSizeY_ = 24.0f;

    GLuint internalFbo_ = 0;
    GLuint fboTex_ = 0;
    GLsizei fboW_ = 0;
    GLsizei fboH_ = 0;
    GLuint blitProgram_ = 0;
    GLint locPos_ = 0, locTex_ = 0, locScale_ = 0, locFlipY_ = -1;
    GLuint mosaicProgram_ = 0;
    GLint mosaicLocPos_ = 0, mosaicLocTex_ = -1;
    GLint mosaicLocTexSize_ = -1, mosaicLocBlock_ = -1;
    GLint mosaicLocUvOffset_ = -1, mosaicLocUvScale_ = -1;
    GLuint mosaicSrcTex_ = 0;
    GLsizei mosaicTexW_ = 0, mosaicTexH_ = 0;
    bool mosaicGpuEnabled_ = true;
    bool mosaicEffectEnabled_ = false;
    std::vector<uint8_t> mosaicCpuScratch_;
    std::chrono::steady_clock::time_point lastUpdateTime_;
};

// ---------------------------------------------------------------------------
// Continuous animation driver — updates all active Live2D models every frame.
// When a registered Layer is available, blits via GPU/CPU CopyFBOToLayer.
// Otherwise falls back to PostDrawHook overlay blit.
// ---------------------------------------------------------------------------
class Live2DContinuousCallback : public tTVPContinuousEventCallbackIntf {
public:
    void OnContinuousCallback(tjs_uint64 /*tick*/) override {
        bool anyActive = false;
        iTJSDispatch2 *layer = KrkrGLES_GetRegisteredLayer();
#if defined(KRKR_RENDER_PROBE)
        {
            static int s_lastHadLayer = -1;
            const int had = layer ? 1 : 0;
            if(had != s_lastHadLayer) {
                s_lastHadLayer = had;
                spdlog::info("[probe] krkrlive2d: registered layer {} -> {}"
                             " presentation path",
                             had ? "present" : "absent",
                             had ? "CopyFBOToLayer" : "PostDrawHook overlay");
            }
        }
#endif

        GLint savedFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
        GLint savedVP[4];
        glGetIntegerv(GL_VIEWPORT, savedVP);

        for(auto *m : g_activeModels) {
            // 不可见的模型既不更新也不拷贝。之前缺这道判断，切过 N 个 CG 之后
            // 每帧就是 N 次模型更新 + N 次全屏 CopyFBOToLayer（KAG 档下的主要
            // 卡顿来源）。
            if(!m || !m->IsLoaded() || !m->IsVisible())
                continue;
            m->ContinuousUpdate(savedFBO, savedVP);
            anyActive = true;
            if(layer && g_live2dRenderTarget.fbo) {
                CopyFBOToLayer(g_live2dRenderTarget.fbo,
                               g_live2dRenderTarget.width,
                               g_live2dRenderTarget.height, layer, savedFBO);
            }
        }
        if(anyActive && !layer && TVPGetWindowCount() > 0) {
            tTJSNI_Window *win = TVPGetWindowListAt(0);
            if(win)
                TVPPostWindowUpdate(win);
        }
    }
};

static Live2DContinuousCallback g_live2dContinuousCb;
static bool g_continuousHookRegistered = false;

static void Live2DPostDrawHook() {
#if defined(KRKR_RENDER_PROBE)
    {
        static bool s_logged = false;
        if(!s_logged) {
            s_logged = true;
            spdlog::info("[probe] krkrlive2d: PostDrawHook entered "
                         "(registeredLayer={})",
                         KrkrGLES_GetRegisteredLayer() ? "present" : "absent");
        }
    }
#endif
    if(KrkrGLES_GetRegisteredLayer())
        return;

    GLint curFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &curFBO);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    for(auto *m : g_activeModels) {
        if(m && m->IsLoaded() && m->IsVisible()) {
            m->BlitOverlay(curFBO, vp);
        }
    }
}

static void EnsureContinuousHook() {
    if(g_continuousHookRegistered)
        return;
    TVPAddContinuousEventHook(&g_live2dContinuousCb);
    TVPSetPostDrawHook(Live2DPostDrawHook);
    g_continuousHookRegistered = true;
    spdlog::info("krkrlive2d: continuous animation hook registered");
}

// ---------------------------------------------------------------------------
// Live2DMatrix — simple 2x3 affine matrix (TJS interface)
// ---------------------------------------------------------------------------
class Live2DMatrix {
public:
    Live2DMatrix() = default;

    static tjs_error setMatrixCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                 Live2DMatrix *s) {
        if(!s || !p)
            return TJS_S_OK;
        for(int i = 0; i < 6 && i < n; ++i)
            s->m_[i] = ToReal(*p[i], s->m_[i]);
        return TJS_S_OK;
    }

private:
    std::array<tjs_real, 6> m_{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
};

// ---------------------------------------------------------------------------
// Live2DDevice — rendering device wrapper
// ---------------------------------------------------------------------------
class Live2DDevice {
public:
    Live2DDevice() = default;
    void beginScene() {
        KRKR_PROBE_TJS("Live2DDevice", "beginScene", 0, nullptr);
        inScene_ = true;
    }
    void endScene() {
        KRKR_PROBE_TJS("Live2DDevice", "endScene", 0, nullptr);
        inScene_ = false;
    }
    void onBeginScene() {
        KRKR_PROBE_TJS("Live2DDevice", "onBeginScene", 0, nullptr);
        beginScene();
    }
    void onEndScene() {
        KRKR_PROBE_TJS("Live2DDevice", "onEndScene", 0, nullptr);
        endScene();
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int numparams,
                              tTJSVariant **param, Live2DDevice *) {
        KRKR_PROBE_TJS("Live2DDevice", "render", numparams, param);
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject) {
            iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
            if(obj) {
                tjs_uint hint = 0;
                obj->FuncCall(0, TJS_W("sync"), &hint, nullptr, 0, nullptr,
                              obj);
            }
        }
        if(r)
            *r = true;
        return TJS_S_OK;
    }

private:
    bool inScene_ = false;
};

// ---------------------------------------------------------------------------
// Live2DModel — TJS class wrapping CubismLive2DModel
// ---------------------------------------------------------------------------
class Live2DModel {
public:
    Live2DModel() { ensureDeviceObject(); }

    ~Live2DModel() {
        if(cubismModel_) {
            CSM_DELETE(cubismModel_);
            cubismModel_ = nullptr;
        }
        if(deviceObj_) {
            deviceObj_->Release();
            deviceObj_ = nullptr;
        }
    }

    // Non-copyable for simplicity with Cubism resources
    Live2DModel(const Live2DModel &) = delete;
    Live2DModel &operator=(const Live2DModel &) = delete;

    // --- Core callbacks ---

    static tjs_error loadCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "load", n, p);
        if(!s)
            return TJS_S_OK;
        if(n <= 0 || !p) {
            if(r)
                *r = false;
            return TJS_S_OK;
        }

        ttstr storagePath = ToTTStr(*p[0]);
        s->storage_ = storagePath;

        // Load the .l2d file from the engine's storage
        std::vector<uint8_t> zipData;
        if(!LoadFromStorage(storagePath, zipData)) {
            spdlog::error("krkrlive2d: failed to load from storage: {}",
                          storagePath.AsStdString());
            if(r)
                *r = false;
            return TJS_S_OK;
        }

        // Extract base name from filename (e.g., "ev_cg003_02s" from path)
        std::string fullPath = storagePath.AsStdString();
        std::string baseName;
        auto slash = fullPath.rfind('/');
        auto bslash = fullPath.rfind('\\');
        size_t start = 0;
        if(slash != std::string::npos)
            start = slash + 1;
        if(bslash != std::string::npos && bslash + 1 > start)
            start = bslash + 1;
        baseName = fullPath.substr(start);
        auto dot = baseName.rfind('.');
        if(dot != std::string::npos)
            baseName = baseName.substr(0, dot);

        EnsureCubismInitialized();

        if(s->cubismModel_) {
            CSM_DELETE(s->cubismModel_);
            s->cubismModel_ = nullptr;
        }
        s->cubismModel_ = CSM_NEW CubismLive2DModel();
        if(!s->cubismModel_->LoadFromL2D(zipData, baseName)) {
            spdlog::error("krkrlive2d: model load failed: {}", baseName);
            CSM_DELETE(s->cubismModel_);
            s->cubismModel_ = nullptr;
            if(r)
                *r = false;
            return TJS_S_OK;
        }
        s->cubismModel_->SetMosaicSize(static_cast<float>(s->mosaicX_),
                                       static_cast<float>(s->mosaicY_));
        // 把 TJS 侧已有的可见性状态同步给新模型：脚本可能先 hide() 再 load()。
        s->cubismModel_->SetVisible(s->visible_);

        s->loaded_ = true;
        s->progress_ = 1.0;
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "render", n, p);
        if(s && s->cubismModel_ && s->cubismModel_->IsLoaded()) {
            GLint savedFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
            GLint savedVP[4];
            glGetIntegerv(GL_VIEWPORT, savedVP);
            s->cubismModel_->UpdateAndDraw(savedFBO, savedVP);
            // capture 回调里脚本调 render() 时，调用方已经把捕获目标绑好了；
            // 官方插件的语义是"画在当前 FBO"，G2 的全动画正是靠这一步把立绘
            // 合成进 capture 的目标图层。UpdateAndDraw 已把 FBO 还原成 savedFBO，
            // 所以这里直接把它当目标 blit。非 capture 场景保持原行为（叠加钩子负责呈现）。
            if(KrkrGLES_IsCaptureActive())
                s->cubismModel_->BlitOverlay(savedFBO, savedVP);
        }
        if(s)
            s->progress_ = 1.0;
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    // show/hide 必须真正作用到 CubismModel：连续动画钩子按可见性跳过更新与
    // 整帧 blit。否则切 CG 后所有历史模型都在每帧参与渲染。
    static tjs_error showCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "show", n, p);
        if(s) {
            s->visible_ = true;
            if(s->cubismModel_)
                s->cubismModel_->SetVisible(true);
            spdlog::info("krkrlive2d: model show: {}", s->storage_.AsStdString());
        }
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error hideCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "hide", n, p);
        if(s) {
            s->visible_ = false;
            if(s->cubismModel_)
                s->cubismModel_->SetVisible(false);
            spdlog::info("krkrlive2d: model hide: {}", s->storage_.AsStdString());
        }
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error getDeviceCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 Live2DModel *s) {
        if(!r)
            return TJS_S_OK;
        if(!s || !s->deviceObj_) {
            r->Clear();
            return TJS_S_OK;
        }
        *r = tTJSVariant(s->deviceObj_, s->deviceObj_);
        return TJS_S_OK;
    }

    static tjs_error setDeviceCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                 Live2DModel *s) {
        if(!s || !p || n <= 0 || !p[0])
            return TJS_S_OK;
        iTJSDispatch2 *obj =
            (p[0]->Type() == tvtObject) ? p[0]->AsObjectNoAddRef() : nullptr;
        if(obj == s->deviceObj_)
            return TJS_S_OK;
        if(obj)
            obj->AddRef();
        if(s->deviceObj_)
            s->deviceObj_->Release();
        s->deviceObj_ = obj;
        if(!s->deviceObj_)
            s->ensureDeviceObject();
        return TJS_S_OK;
    }

    // --- Parameter / Part / Motion stubs that work with Cubism model ---

    static tjs_error setScaleCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                Live2DModel *s) {
        if(s && p && n > 0)
            s->scale_ = ToReal(*p[0], s->scale_);
        return TJS_S_OK;
    }

    static tjs_error getScaleCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                Live2DModel *s) {
        if(r && s)
            *r = s->scale_;
        return TJS_S_OK;
    }

    static tjs_error setMatrixCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                 Live2DModel *s) {
        if(!s || !p)
            return TJS_S_OK;
        for(int i = 0; i < 6 && i < n; ++i)
            s->matrix_[i] = ToReal(*p[i], s->matrix_[i]);
        return TJS_S_OK;
    }

    static tjs_error setVoiceValueCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                     Live2DModel *s) {
        if(s && p && n > 0)
            s->voiceValue_ = ToReal(*p[0], s->voiceValue_);
        return TJS_S_OK;
    }

    static tjs_error setVoiceWeightCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                      Live2DModel *s) {
        if(s && p && n > 0)
            s->voiceWeight_ = ToReal(*p[0], s->voiceWeight_);
        return TJS_S_OK;
    }

    static tjs_error setVoiceModeCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                    Live2DModel *s) {
        if(s && p && n > 0)
            s->voiceMode_ = ToInt(*p[0], s->voiceMode_);
        return TJS_S_OK;
    }

    static tjs_error setBlinkingIntervalCb(tTJSVariant *, tjs_int n,
                                           tTJSVariant **p, Live2DModel *s) {
        if(s && p && n > 0)
            s->blinkingInterval_ = ToReal(*p[0], s->blinkingInterval_);
        return TJS_S_OK;
    }

    static tjs_error setBlinkingSettingsCb(tTJSVariant *, tjs_int n,
                                           tTJSVariant **p, Live2DModel *s) {
        if(!s || !p)
            return TJS_S_OK;
        if(n > 0)
            s->blinkingInterval_ = ToReal(*p[0], s->blinkingInterval_);
        if(n > 1)
            s->blinkingMode_ = ToInt(*p[1], s->blinkingMode_);
        return TJS_S_OK;
    }

    static tjs_error setBlinkingModeCb(tTJSVariant *, tjs_int n,
                                       tTJSVariant **p, Live2DModel *s) {
        if(s && p && n > 0)
            s->blinkingMode_ = ToInt(*p[0], s->blinkingMode_);
        return TJS_S_OK;
    }

    static tjs_error setMosaicParamCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                      Live2DModel *s) {
        if(!s || !p)
            return TJS_S_OK;
        if(n > 0)
            s->mosaicX_ = ToReal(*p[0], s->mosaicX_);
        if(n > 1)
            s->mosaicY_ = ToReal(*p[1], s->mosaicY_);
        if(s->cubismModel_) {
            s->cubismModel_->SetMosaicSize(static_cast<float>(s->mosaicX_),
                                           static_cast<float>(s->mosaicY_));
        }
        return TJS_S_OK;
    }

    static tjs_error getExpressionCountCb(tTJSVariant *r, tjs_int,
                                          tTJSVariant **, Live2DModel *s) {
        if(r)
            *r = static_cast<tjs_int>(s ? s->expressionNames_.size() : 0);
        return TJS_S_OK;
    }

    static tjs_error getExpressionNameCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, Live2DModel *s) {
        if(!r || !s)
            return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        if(idx >= 0 && idx < static_cast<tjs_int>(s->expressionNames_.size()))
            *r = s->expressionNames_[static_cast<size_t>(idx)];
        else
            *r = ttstr();
        return TJS_S_OK;
    }

    static tjs_error setExpressionCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "setExpression", n, p);
        if(s && n > 0 && p)
            s->expression_ = ToTTStr(*p[0]);
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error getExpressionCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     Live2DModel *s) {
        if(r && s)
            *r = s->expression_;
        return TJS_S_OK;
    }

    static tjs_error fixExpressionCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     Live2DModel *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error getMotionGroupCountCb(tTJSVariant *r, tjs_int,
                                           tTJSVariant **, Live2DModel *s) {
        if(r)
            *r = static_cast<tjs_int>(
                (s && s->cubismModel_)
                    ? s->cubismModel_->MotionGroups().size()
                    : 0);
        return TJS_S_OK;
    }

    static tjs_error getMotionGroupNameCb(tTJSVariant *r, tjs_int n,
                                          tTJSVariant **p, Live2DModel *s) {
        if(!r)
            return TJS_S_OK;
        if(!s || !s->cubismModel_) {
            *r = ttstr();
            return TJS_S_OK;
        }
        const auto &groups = s->cubismModel_->MotionGroups();
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        *r = (idx >= 0 && idx < static_cast<tjs_int>(groups.size()))
            ? groups[static_cast<size_t>(idx)]
            : ttstr();
        return TJS_S_OK;
    }

    static tjs_error getMotionCountCb(tTJSVariant *r, tjs_int n,
                                      tTJSVariant **p, Live2DModel *s) {
        if(!r)
            return TJS_S_OK;
        if(!s || !s->cubismModel_) {
            *r = 0;
            return TJS_S_OK;
        }
        ttstr group = (n > 0 && p) ? ToTTStr(*p[0]) : TJS_W("main");
        const std::vector<ttstr> *names =
            s->cubismModel_->FindMotionNames(group.AsStdString());
        *r = (names == nullptr) ? 0 : static_cast<tjs_int>(names->size());
        return TJS_S_OK;
    }

    static tjs_error getMotionNameCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                     Live2DModel *s) {
        if(!r)
            return TJS_S_OK;
        if(!s || !s->cubismModel_) {
            *r = ttstr();
            return TJS_S_OK;
        }
        ttstr group = (n > 0 && p) ? ToTTStr(*p[0]) : TJS_W("main");
        tjs_int idx = (n > 1 && p) ? ToInt(*p[1], 0) : 0;
        const std::vector<ttstr> *names =
            s->cubismModel_->FindMotionNames(group.AsStdString());
        if(names != nullptr && idx >= 0 &&
           idx < static_cast<tjs_int>(names->size()))
            *r = (*names)[static_cast<size_t>(idx)];
        else
            *r = ttstr();
        return TJS_S_OK;
    }

    // 脚本侧 startMotion 的签名沿用官方插件：startMotion(组名[, 组内序号])。
    // 关键修复：原实现只把动作名记进 currentMotions_ 就返回 true，**从不真正启动
    // 动作** —— 于是切 CG / 轮播动画时画面纹丝不动（模型一直在播加载时自动起播的
    // 那个动作），这是轮播失效的直接根因。这里改为真正落到 CubismMotionManager。
    static tjs_error startMotionCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "startMotion", n, p);
        if(!s)
            return TJS_S_OK;
        if(!s->cubismModel_) {
            if(r)
                *r = false;
            return TJS_S_OK;
        }

        ttstr group = (n > 0 && p) ? ToTTStr(*p[0]) : TJS_W("main");
        if(group.IsEmpty())
            group = TJS_W("main");
        const std::string raw = group.AsStdString();

        // 把脚本**实际传来的参数**记下来（含个数与类型）。轮播类问题只有看到
        // 真实入参才能判断是"组名对不上"、"序号恒为 0"、还是"只传了名字"。
        // startMotion 由玩家点击驱动，频率极低，不需要限频。
        {
            std::string dump;
            for(tjs_int i = 0; i < n && p; ++i) {
                if(i)
                    dump += ", ";
                if(!p[i]) {
                    dump += "null";
                    continue;
                }
                if(p[i]->Type() == tvtString || p[i]->Type() == tvtOctet) {
                    dump += "'" + ToTTStr(*p[i]).AsStdString() + "'";
                } else if(p[i]->Type() == tvtInteger ||
                          p[i]->Type() == tvtReal) {
                    dump += std::to_string(static_cast<long long>(
                        ToInt(*p[i], 0)));
                } else {
                    dump += "<type=" +
                        std::to_string(static_cast<int>(p[i]->Type())) + ">";
                }
            }
            spdlog::info("krkrlive2d: startMotion 实参 {} 个: {}", n, dump);
        }

        // 参数写法有三种，按"从最具体到最宽松"的顺序试，避免误拆：
        // 参数形态（按"从最具体到最宽松"试，避免误拆）：
        //   1) startMotion(组名, 序号)          —— 两个参数，最明确
        //   2) startMotion(组名, 动作名)        —— 两个参数，第二个是字符串
        //   3) startMotion("组名.动作名")       —— **本作实测用法**：
        //      scn 的字符串表里就是 "main.ev_mv001_02_03h"
        //   4) startMotion(动作名)              —— 单个参数，名字里可能含下划线
        //   5) startMotion("组_序号")
        //   6) startMotion(组名)                —— 取该组第 0 个
        std::string groupName = raw;
        tjs_int index = 0;
        bool started = false;

        const bool secondIsString =
            n > 1 && p && p[1] &&
            (p[1]->Type() == tvtString || p[1]->Type() == tvtOctet);

        if(secondIsString) {
            // (组名, 动作名)
            const std::string motionName = ToTTStr(*p[1]).AsStdString();
            started = s->cubismModel_->StartMotionByName(motionName);
            if(!started)
                started = s->cubismModel_->StartMotionByName(raw);
        } else {
            if(n > 1 && p && p[1])
                index = ToInt(*p[1], 0);
            // 先按 (组名, 序号)。未命名组已按 main 对外，这里能直接命中。
            started = s->cubismModel_->StartMotionByIndex(raw, index);
            if(!started)
                started = s->cubismModel_->StartMotionByName(raw);
            if(!started) {
                // "组名.动作名"：本作的实际写法。按**第一个** '.' 拆（组名不会含点）。
                const auto dot = raw.find('.');
                if(dot != std::string::npos && dot + 1 < raw.size()) {
                    const std::string g = raw.substr(0, dot);
                    const std::string nm = raw.substr(dot + 1);
                    started = s->cubismModel_->StartMotionByName(nm);
                    if(!started)
                        started = s->cubismModel_->StartMotionByIndex(g, index);
                    if(started)
                        groupName = g;
                }
            }
            if(!started) {
                // "组_序号"：只认**最后一个**下划线且其右侧全为数字。
                const auto us = raw.rfind('_');
                if(us != std::string::npos && us + 1 < raw.size()) {
                    const std::string tail = raw.substr(us + 1);
                    if(tail.find_first_not_of("0123456789") ==
                       std::string::npos) {
                        const tjs_int parsed = std::atoi(tail.c_str());
                        const std::string head = raw.substr(0, us);
                        if(s->cubismModel_->StartMotionByIndex(head, parsed)) {
                            groupName = head;
                            index = parsed;
                            started = true;
                        }
                    }
                }
            }
        }

        s->playing_ = true;
        s->currentMotions_.clear();
        s->currentMotions_.push_back(group);
        if(started) {
            spdlog::info("krkrlive2d: startMotion '{}' #{} -> 已启动（键 {}）",
                         groupName, static_cast<int>(index),
                         s->cubismModel_->SelectedMotionKey());
        } else {
            // 找不到动作时不要假装成功：脚本靠返回值决定后续流程，
            // 恒返回 true 会让"切不过去"变成静默失败。
            spdlog::warn("krkrlive2d: startMotion '{}' #{} 未找到该动作"
                         "（模型 {} 组 / {} 个动作）",
                         groupName, static_cast<int>(index),
                         s->cubismModel_->MotionGroups().size(),
                         s->cubismModel_->TotalMotionCount());
        }
        if(r)
            *r = started;
        return TJS_S_OK;
    }

    static tjs_error stopMotionCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  Live2DModel *s) {
        if(s) {
            s->playing_ = false;
            s->currentMotions_.clear();
            // 同时清掉"选中动作"，否则停掉之后播完续播还会把上一个动作拉回来。
            if(s->cubismModel_)
                s->cubismModel_->StopMotion();
            spdlog::info("krkrlive2d: stopMotion: {}", s->storage_.AsStdString());
        }
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error getCurrentMotionsCb(tTJSVariant *r, tjs_int,
                                         tTJSVariant **, Live2DModel *s) {
        if(!r || !s)
            return TJS_S_OK;
        iTJSDispatch2 *arr = CreateStringArray(s->currentMotions_);
        SetResultObject(r, arr);
        if(arr)
            arr->Release();
        return TJS_S_OK;
    }

    static tjs_error getParameterCountCb(tTJSVariant *r, tjs_int,
                                         tTJSVariant **, Live2DModel *s) {
        if(r) {
            if(s && s->cubismModel_ && s->cubismModel_->GetModel())
                *r = s->cubismModel_->GetModel()->GetParameterCount();
            else
                *r = static_cast<tjs_int>(s ? s->parameterNames_.size() : 0);
        }
        return TJS_S_OK;
    }

    static tjs_error getParameterInfoCb(tTJSVariant *r, tjs_int n,
                                        tTJSVariant **p, Live2DModel *s) {
        if(!r || !s)
            return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        if(s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *model = s->cubismModel_->GetModel();
            if(idx >= 0 && idx < model->GetParameterCount()) {
                const char *id =
                    model->GetParameterId(idx)->GetString().GetRawString();
                ttstr tid(id);
                iTJSDispatch2 *dict = CreateIdNameDict(tid, tid);
                SetResultObject(r, dict);
                if(dict)
                    dict->Release();
                return TJS_S_OK;
            }
        }
        r->Clear();
        return TJS_S_OK;
    }

    static tjs_error getParameterValueCb(tTJSVariant *r, tjs_int n,
                                         tTJSVariant **p, Live2DModel *s) {
        if(!r || !s || n <= 0 || !p)
            return TJS_S_OK;
        if(s->cubismModel_ && s->cubismModel_->GetModel()) {
            std::string key = ToKey(*p[0]);
            auto *id = CubismFramework::GetIdManager()->GetId(key.c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetParameterIndex(id);
            if(idx >= 0)
                *r = static_cast<tjs_real>(
                    s->cubismModel_->GetModel()->GetParameterValue(idx));
            else
                *r = 0.0;
        } else {
            auto it = s->parameterValues_.find(ToKey(*p[0]));
            *r = (it == s->parameterValues_.end()) ? 0.0 : it->second;
        }
        return TJS_S_OK;
    }

    static tjs_error setParameterValueCb(tTJSVariant *, tjs_int n,
                                         tTJSVariant **p, Live2DModel *s) {
        if(!s || !p || n <= 1)
            return TJS_S_OK;
        if(s->cubismModel_ && s->cubismModel_->GetModel()) {
            std::string key = ToKey(*p[0]);
            auto *id = CubismFramework::GetIdManager()->GetId(key.c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetParameterIndex(id);
            if(idx >= 0)
                s->cubismModel_->GetModel()->SetParameterValue(
                    idx, static_cast<csmFloat32>(ToReal(*p[1], 0.0)));
        }
        s->parameterValues_[ToKey(*p[0])] = ToReal(*p[1], 0.0);
        return TJS_S_OK;
    }

    static tjs_error setParameterTypeCb(tTJSVariant *, tjs_int n,
                                        tTJSVariant **p, Live2DModel *s) {
        if(s && p && n > 1)
            s->parameterTypes_[ToKey(*p[0])] = ToInt(*p[1], 0);
        return TJS_S_OK;
    }

    static tjs_error setDiffParameterValueCb(tTJSVariant *, tjs_int n,
                                             tTJSVariant **p, Live2DModel *s) {
        if(s && p && n > 1)
            s->diffParameterValues_[ToKey(*p[0])] = ToReal(*p[1], 0.0);
        return TJS_S_OK;
    }

    static tjs_error getDiffParameterValueCb(tTJSVariant *r, tjs_int n,
                                             tTJSVariant **p, Live2DModel *s) {
        if(!r || !s || !p || n <= 0)
            return TJS_S_OK;
        auto it = s->diffParameterValues_.find(ToKey(*p[0]));
        *r = (it == s->diffParameterValues_.end()) ? 0.0 : it->second;
        return TJS_S_OK;
    }

    static tjs_error addEyeBlinkIdCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     Live2DModel *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error addLipSyncIdCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    Live2DModel *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error canSyncCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                               Live2DModel *s) {
        if(r)
            *r = (s != nullptr);
        return TJS_S_OK;
    }

    static tjs_error syncCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                            Live2DModel *s) {
        if(s)
            s->progress_ = 1.0;
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error progressCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "progress", n, p);
        if(s && p && n > 0)
            s->progress_ = ToReal(*p[0], s->progress_);
        if(r && s)
            *r = s->progress_;
        return TJS_S_OK;
    }

    static tjs_error cloneCb(tTJSVariant *r, tjs_int n, tTJSVariant **p, Live2DModel *s) {
            KRKR_PROBE_TJS("Live2DModel", "clone", n, p);
        // Clone not supported for Cubism models (shared GPU resources)
        if(r)
            r->Clear();
        return TJS_S_OK;
    }

    static tjs_error isPlayingCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 Live2DModel *s) {
        if(r && s)
            *r = s->playing_;
        return TJS_S_OK;
    }

    static tjs_error getPartCountCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    Live2DModel *s) {
        if(r) {
            if(s && s->cubismModel_ && s->cubismModel_->GetModel())
                *r = s->cubismModel_->GetModel()->GetPartCount();
            else
                *r = static_cast<tjs_int>(s ? s->partNames_.size() : 0);
        }
        return TJS_S_OK;
    }

    static tjs_error getPartInfoCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   Live2DModel *s) {
        if(!r || !s)
            return TJS_S_OK;
        tjs_int idx = (n > 0 && p) ? ToInt(*p[0], 0) : 0;
        if(s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *model = s->cubismModel_->GetModel();
            if(idx >= 0 && idx < model->GetPartCount()) {
                const char *id =
                    model->GetPartId(idx)->GetString().GetRawString();
                ttstr tid(id);
                iTJSDispatch2 *dict = CreateIdNameDict(tid, tid);
                SetResultObject(r, dict);
                if(dict)
                    dict->Release();
                return TJS_S_OK;
            }
        }
        r->Clear();
        return TJS_S_OK;
    }

    static tjs_error getPartValueCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                    Live2DModel *s) {
        if(!r || !s || !p || n <= 0)
            return TJS_S_OK;
        if(s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *id =
                CubismFramework::GetIdManager()->GetId(ToKey(*p[0]).c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetPartIndex(id);
            if(idx >= 0) {
                *r = static_cast<tjs_real>(
                    s->cubismModel_->GetModel()->GetPartOpacity(idx));
                return TJS_S_OK;
            }
        }
        auto it = s->partValues_.find(ToKey(*p[0]));
        *r = (it == s->partValues_.end()) ? 1.0 : it->second;
        return TJS_S_OK;
    }

    static tjs_error setPartValueCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                    Live2DModel *s) {
        if(!s || !p || n <= 1)
            return TJS_S_OK;
        if(s->cubismModel_ && s->cubismModel_->GetModel()) {
            auto *id =
                CubismFramework::GetIdManager()->GetId(ToKey(*p[0]).c_str());
            csmInt32 idx = s->cubismModel_->GetModel()->GetPartIndex(id);
            if(idx >= 0)
                s->cubismModel_->GetModel()->SetPartOpacity(
                    idx, static_cast<csmFloat32>(ToReal(*p[1], 1.0)));
        }
        s->partValues_[ToKey(*p[0])] = ToReal(*p[1], 1.0);
        return TJS_S_OK;
    }

    static tjs_error setPartFadeTimeCb(tTJSVariant *, tjs_int n,
                                       tTJSVariant **p, Live2DModel *s) {
        if(s && p && n > 0)
            s->partFadeTime_ = ToReal(*p[0], s->partFadeTime_);
        return TJS_S_OK;
    }

    static tjs_error getEventCountCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     Live2DModel *) {
        if(r)
            *r = 0;
        return TJS_S_OK;
    }

    static tjs_error getEventNameCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    Live2DModel *) {
        if(r)
            *r = ttstr();
        return TJS_S_OK;
    }

    static tjs_error addVriableMotionCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        Live2DModel *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error delVariableMotionCb(tTJSVariant *r, tjs_int,
                                         tTJSVariant **, Live2DModel *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error getVariableMotionCountCb(tTJSVariant *r, tjs_int,
                                              tTJSVariant **, Live2DModel *) {
        if(r)
            *r = 0;
        return TJS_S_OK;
    }

    static tjs_error getVariableMotionNameCb(tTJSVariant *r, tjs_int,
                                             tTJSVariant **, Live2DModel *) {
        if(r)
            *r = ttstr();
        return TJS_S_OK;
    }

    static tjs_error getVariableMotionInfoCb(tTJSVariant *r, tjs_int,
                                             tTJSVariant **, Live2DModel *) {
        if(r)
            r->Clear();
        return TJS_S_OK;
    }

    static tjs_error getVariableCb(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                   Live2DModel *s) {
        if(!r || !s || !p || n <= 0)
            return TJS_S_OK;
        auto it = s->variables_.find(ToKey(*p[0]));
        if(it == s->variables_.end())
            r->Clear();
        else
            *r = it->second;
        return TJS_S_OK;
    }

    static tjs_error setVariableCb(tTJSVariant *, tjs_int n, tTJSVariant **p,
                                   Live2DModel *s) {
        if(s && p && n > 1)
            s->variables_[ToKey(*p[0])] = *p[1];
        return TJS_S_OK;
    }

    static tjs_error isMosaicModelCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     Live2DModel *s) {
        if(r)
            *r =
                (s && s->cubismModel_ && s->cubismModel_->HasMosaicDrawables());
        return TJS_S_OK;
    }

    static tjs_error reloadCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                              Live2DModel *) {
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error resetPartsCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  Live2DModel *s) {
        if(s)
            s->partValues_.clear();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error resetVariablesCb(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      Live2DModel *s) {
        if(s)
            s->variables_.clear();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

    static tjs_error resetExpressionVariablesCb(tTJSVariant *r, tjs_int,
                                                tTJSVariant **,
                                                Live2DModel *s) {
        if(s)
            s->expression_ = ttstr();
        if(r)
            *r = true;
        return TJS_S_OK;
    }

private:
    void ensureDeviceObject() {
        if(deviceObj_)
            return;
        auto *dev =
            ncbInstanceAdaptor<Live2DDevice>::CreateAdaptor(new Live2DDevice());
        if(dev)
            deviceObj_ = dev;
    }

    bool loaded_ = false;
    bool playing_ = false;
    // 可见性（TJS 侧状态）。脚本可能先 hide() 再 load()，所以加载完成时要把
    // 这个状态同步给新建的 CubismLive2DModel（见 loadCb）。
    bool visible_ = true;
    iTJSDispatch2 *deviceObj_ = nullptr;
    CubismLive2DModel *cubismModel_ = nullptr;

    ttstr storage_;
    ttstr expression_;
    tjs_real scale_ = 1.0;
    std::array<tjs_real, 6> matrix_{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
    tjs_real voiceValue_ = 0.0, voiceWeight_ = 1.0;
    tjs_int voiceMode_ = 0;
    tjs_real blinkingInterval_ = 0.0;
    tjs_int blinkingMode_ = 0;
    tjs_real mosaicX_ = 24.0, mosaicY_ = 24.0;
    tjs_real partFadeTime_ = 0.0;
    tjs_real progress_ = 0.0;
    tjs_int renderWidth_ = 1920, renderHeight_ = 1080;

    std::vector<ttstr> expressionNames_{ TJS_W("default") };
    // 注意：动作元数据表（motionGroupNames_ / motionNames_ / autoFirstMotionKey_ /
    // selectedMotionKey_）**不在这里** —— 它们属于 CubismLive2DModel（与 motions_
    // 同处一地），脚本侧经 s->cubismModel_->MotionGroups()/FindMotionNames() 读取。
    std::vector<ttstr> parameterNames_;
    std::vector<ttstr> partNames_{ TJS_W("PartMain") };
    std::vector<ttstr> currentMotions_;
    std::unordered_map<std::string, tjs_real> parameterValues_;
    std::unordered_map<std::string, tjs_real> diffParameterValues_;
    std::unordered_map<std::string, tjs_real> partValues_;
    std::unordered_map<std::string, tjs_int> parameterTypes_;
    std::unordered_map<std::string, tTJSVariant> variables_;
};

// ---------------------------------------------------------------------------
// NCB Registration
// ---------------------------------------------------------------------------
NCB_REGISTER_CLASS(Live2DMatrix) {
    Constructor();
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DMatrix::setMatrixCb, 0);
}

NCB_REGISTER_CLASS(Live2DDevice) {
    Constructor();
    NCB_METHOD(beginScene);
    NCB_METHOD(endScene);
    NCB_METHOD(onBeginScene);
    NCB_METHOD(onEndScene);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DDevice::renderCb, 0);
}

NCB_REGISTER_CLASS(Live2DModel) {
    Constructor();
    NCB_PROPERTY_RAW_CALLBACK(device, Live2DModel::getDeviceCb,
                              Live2DModel::setDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DModel::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(show, &Live2DModel::showCb, 0);
    NCB_METHOD_RAW_CALLBACK(hide, &Live2DModel::hideCb, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Live2DModel::progressCb, 0);
    NCB_METHOD_RAW_CALLBACK(load, &Live2DModel::loadCb, 0);
    NCB_METHOD_RAW_CALLBACK(clone, &Live2DModel::cloneCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, &Live2DModel::setScaleCb, 0);
    NCB_METHOD_RAW_CALLBACK(getScale, &Live2DModel::getScaleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DModel::setMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceValue, &Live2DModel::setVoiceValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceWeight, &Live2DModel::setVoiceWeightCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceMode, &Live2DModel::setVoiceModeCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingInterval,
                            &Live2DModel::setBlinkingIntervalCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingSettings,
                            &Live2DModel::setBlinkingSettingsCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingMode, &Live2DModel::setBlinkingModeCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(setMosaicParam, &Live2DModel::setMosaicParamCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionCount,
                            &Live2DModel::getExpressionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionName,
                            &Live2DModel::getExpressionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(setExpression, &Live2DModel::setExpressionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpression, &Live2DModel::getExpressionCb, 0);
    NCB_METHOD_RAW_CALLBACK(fixExpression, &Live2DModel::fixExpressionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupCount,
                            &Live2DModel::getMotionGroupCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupName,
                            &Live2DModel::getMotionGroupNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionCount, &Live2DModel::getMotionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionName, &Live2DModel::getMotionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(startMotion, &Live2DModel::startMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(stopMotion, &Live2DModel::stopMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getCurrentMotions,
                            &Live2DModel::getCurrentMotionsCb, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Live2DModel::isPlayingCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterCount,
                            &Live2DModel::getParameterCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterInfo, &Live2DModel::getParameterInfoCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(getParameterValue,
                            &Live2DModel::getParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterValue,
                            &Live2DModel::setParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterType, &Live2DModel::setParameterTypeCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(setDiffParameterValue,
                            &Live2DModel::setDiffParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffParameterValue,
                            &Live2DModel::getDiffParameterValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(addEyeBlinkId, &Live2DModel::addEyeBlinkIdCb, 0);
    NCB_METHOD_RAW_CALLBACK(addLipSyncId, &Live2DModel::addLipSyncIdCb, 0);
    NCB_METHOD_RAW_CALLBACK(canSync, &Live2DModel::canSyncCb, 0);
    NCB_METHOD_RAW_CALLBACK(sync, &Live2DModel::syncCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartCount, &Live2DModel::getPartCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartInfo, &Live2DModel::getPartInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPart, &Live2DModel::setPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartValue, &Live2DModel::getPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartValue, &Live2DModel::setPartValueCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartFadeTime, &Live2DModel::setPartFadeTimeCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(getEventCount, &Live2DModel::getEventCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventName, &Live2DModel::getEventNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(addVriableMotion, &Live2DModel::addVriableMotionCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(delVariableMotion,
                            &Live2DModel::delVariableMotionCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionCount,
                            &Live2DModel::getVariableMotionCountCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionName,
                            &Live2DModel::getVariableMotionNameCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionInfo,
                            &Live2DModel::getVariableMotionInfoCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, &Live2DModel::getVariableCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, &Live2DModel::setVariableCb, 0);
    NCB_METHOD_RAW_CALLBACK(isMosaicModel, &Live2DModel::isMosaicModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(reload, &Live2DModel::reloadCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetParts, &Live2DModel::resetPartsCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetVariables, &Live2DModel::resetVariablesCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetExpressionVariables,
                            &Live2DModel::resetExpressionVariablesCb, 0);
}

// 插件卸载 / runtime-restart 时清空全局纹理缓存。
//
// 为什么必须做：缓存里的 GL 纹理 id 属于**创建它们的那个 GL context**。
// engine_destroy → 重新 engine_open_game 会重建 EGL context（AGENTS.md 硬约束 7：
// context 重建后不得复用旧纹理），而本缓存是进程级静态的 —— 不在这里清掉，
// 新 context 下第一次命中缓存就会把一个已失效的 id 绑上去，表现为立绘变黑/错纹理，
// 且因为没有 GL 报错会非常难查。
//
// 传 false：此处不保证 EGL context 仍 current（engine_destroy 在注销前没有
// eglMakeCurrent），而那个 context 紧接着就会被销毁，纹理会随之消失。只需要
// 丢掉记录，绝不能让失效 id 活到下一个 context。
static void KrkrLive2DPreUnregist() {
    const size_t n = ClearTextureCache(/*deleteGlObjects=*/false);
    if(n)
        spdlog::info("krkrlive2d: 卸载时清空纹理缓存（丢弃 {} 条记录，"
                     "GL 纹理由旧 context 一并销毁）",
                     n);
}
NCB_PRE_UNREGIST_CALLBACK(KrkrLive2DPreUnregist);

extern "C" void TVPRegisterKrkrLive2DPluginAnchor() {}
