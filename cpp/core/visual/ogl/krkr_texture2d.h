/**
 * @file krkr_texture2d.h
 * @brief Lightweight Texture2D replacement for CCTexture2D.
 *
 * This class provides just enough of the Texture2D API
 * that is actually used by RenderManager.cpp and RenderManager_ogl.cpp.
 * It is NOT a general-purpose texture class — only the methods actually
 * called in KiriKiri2 are implemented.
 *
 * Used in RenderManager.cpp:
 *   - new Texture2D / autorelease()
 *   - initWithData(data, dataLen, pixelFormat, width, height, size)
 *   - updateWithData(data, offsetX, offsetY, width, height)
 *   - getPixelsWide() / getPixelsHigh()
 *
 * Used in RenderManager_ogl.cpp (AdapterTexture2D):
 *   - _name, _contentSize, _maxS, _maxT, _pixelsWide, _pixelsHigh
 *   - _pixelFormat, _hasPremultipliedAlpha, _hasMipmaps
 *   - setGLProgram() — NOT needed (we remove this dependency)
 */
#pragma once

#include "ogl_common.h"

#include <spdlog/spdlog.h>
#include <cstddef> // size_t, ssize_t
#include <cstring> // memcpy

namespace krkr {

    // ---------------------------------------------------------------------------
    // PixelFormat — mirrors the original engine pixel format enum
    // ---------------------------------------------------------------------------
    enum class PixelFormat {
        RGBA8888,
        RGB888,
        RGBA4444,
        RGB565,
        A8,
        I8,
        AI88,
        BGRA8888,
    };

    // ---------------------------------------------------------------------------
    // Size — minimal Size struct for texture dimensions
    // ---------------------------------------------------------------------------
    struct Size {
        float width = 0;
        float height = 0;

        Size() = default;
        Size(float w, float h) : width(w), height(h) {}

        static const Size ZERO;
    };

    inline const Size Size::ZERO = { 0, 0 };

    // ---------------------------------------------------------------------------
    // Texture2D — lightweight Texture2D for the krkr rendering pipeline
    //
    // Implements reference counting with autorelease() support.
    // autorelease() is a no-op that just returns this — the caller is
    // expected to manage lifetime manually or via the existing KiriKiri2
    // reference counting in iTVPTexture2D.
    // ---------------------------------------------------------------------------
    class Texture2D {
    public:
        Texture2D() = default;
        virtual ~Texture2D() {
            if(_name && _ownsTexture) {
                glDeleteTextures(1, &_name);
            }
        }

        // --- Reference counting (simplified autorelease pool is not needed)
        // ---
        void autorelease() {
            // Originally, autorelease adds to a pool. Here we simply mark
            // that external code manages the lifetime. The caller
            // (iTVPTexture2D) handles Release() properly.
            _autoreleased = true;
        }

        // --- Initialization ---
        /**
         * Initialize with pixel data.
         *
         * @param data       Pixel data pointer
         * @param dataLen    Byte length of data (unused, kept for API compat)
         * @param format     Pixel format
         * @param pixelsWide Width in pixels
         * @param pixelsHigh Height in pixels
         * @param contentSize Content size (unused, kept for API compat)
         */
        bool initWithData(const void *data, ssize_t dataLen, PixelFormat format,
                          int pixelsWide, int pixelsHigh,
                          const Size &contentSize) {
            _pixelsWide = pixelsWide;
            _pixelsHigh = pixelsHigh;
            _pixelFormat = format;
            _contentSize = Size(static_cast<float>(pixelsWide),
                                static_cast<float>(pixelsHigh));

            GLenum glInternal = GL_RGBA;
            GLenum glFormat = GL_RGBA;
            GLenum glType = GL_UNSIGNED_BYTE;
            resolveGLFormat(format, glInternal, glFormat, glType);

            if(_name == 0) {
                glGenTextures(1, &_name);
                _ownsTexture = true;
            }
            glBindTexture(GL_TEXTURE_2D, _name);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glTexImage2D(GL_TEXTURE_2D, 0, glInternal, pixelsWide, pixelsHigh,
                         0, glFormat, glType, data);
            applyLuminanceSwizzle(format);

            // 静默失败时纹理是没有存储的：挂不上 FBO、画进去的像素全被丢弃。
            // 只补存储、不重传像素（data 的布局随格式变化，按 RGBA8 重传会读错）。
            if(!TVPTextureHasStorage(_name)) {
                static int s_rebuilds = 0;
                if(s_rebuilds < 8) {
                    ++s_rebuilds;
                    spdlog::error("krkrgl: Texture2D storage missing ({}x{} "
                                  "tex={} fmt=0x{:04X}) -> rebuilding as RGBA8",
                                  pixelsWide, pixelsHigh,
                                  static_cast<unsigned>(_name),
                                  static_cast<unsigned>(glInternal));
                }
                glBindTexture(GL_TEXTURE_2D, _name);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pixelsWide, pixelsHigh,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                while(glGetError() != GL_NO_ERROR) {
                }
            }
            return true;
        }

        /**
         * Update a sub-region of the texture.
         */
        bool updateWithData(const void *data, int offsetX, int offsetY,
                            int width, int height) {
            if(_name == 0)
                return false;

            // glTexSubImage2D 只需要客户格式；纹理的格式与 swizzle 已在
            // initWithData 时定下，这里改不了也不该改。
            GLenum glInternalUnused = GL_RGBA;
            GLenum glFormat = GL_RGBA;
            GLenum glType = GL_UNSIGNED_BYTE;
            resolveGLFormat(_pixelFormat, glInternalUnused, glFormat, glType);

            glBindTexture(GL_TEXTURE_2D, _name);
            glTexSubImage2D(GL_TEXTURE_2D, 0, offsetX, offsetY, width, height,
                            glFormat, glType, data);
            return true;
        }

        // --- Accessors ---
        GLuint getName() const { return _name; }
        int getPixelsWide() const { return _pixelsWide; }
        int getPixelsHigh() const { return _pixelsHigh; }
        PixelFormat getPixelFormat() const { return _pixelFormat; }
        const Size &getContentSize() const { return _contentSize; }

        // --- Fields exposed for AdapterTexture2D in RenderManager_ogl.cpp ---
        // These mirror the original Texture2D protected members that
        // AdapterTexture2D directly accesses.
        GLuint _name = 0;
        Size _contentSize;
        float _maxS = 1.0f;
        float _maxT = 1.0f;
        int _pixelsWide = 0;
        int _pixelsHigh = 0;
        PixelFormat _pixelFormat = PixelFormat::RGBA8888;
        bool _hasPremultipliedAlpha = false;
        bool _hasMipmaps = false;

    protected:
        bool _ownsTexture = false;
        bool _autoreleased = false;

        /**
         * 内部格式与客户格式分开返回。
         *
         * GLES 3.0 移除了 GL_LUMINANCE / GL_LUMINANCE_ALPHA /
         * GL_ALPHA，单/双通道 格式必须写成 R8/RG8 内部格式 + GL_RED/GL_RG
         * 客户格式。采样语义由 applyLuminanceSwizzle
         * 在纹理创建后补回，见那边的说明。
         */
        static void resolveGLFormat(PixelFormat format, GLenum &glInternal,
                                    GLenum &glFormat, GLenum &glType) {
            switch(format) {
                case PixelFormat::RGBA8888:
                    glInternal = glFormat = GL_RGBA;
                    glType = GL_UNSIGNED_BYTE;
                    break;
                case PixelFormat::RGB888:
                    glInternal = glFormat = GL_RGB;
                    glType = GL_UNSIGNED_BYTE;
                    break;
                case PixelFormat::RGBA4444:
                    glInternal = glFormat = GL_RGBA;
                    glType = GL_UNSIGNED_SHORT_4_4_4_4;
                    break;
                case PixelFormat::RGB565:
                    glInternal = glFormat = GL_RGB;
                    glType = GL_UNSIGNED_SHORT_5_6_5;
                    break;
                case PixelFormat::A8:
                case PixelFormat::I8:
                    glInternal = GL_R8;
                    glFormat = GL_RED;
                    glType = GL_UNSIGNED_BYTE;
                    break;
                case PixelFormat::AI88:
                    glInternal = GL_RG8;
                    glFormat = GL_RG;
                    glType = GL_UNSIGNED_BYTE;
                    break;
                case PixelFormat::BGRA8888:
#ifdef GL_BGRA
                    glInternal = glFormat = GL_BGRA;
                    glType = GL_UNSIGNED_BYTE;
                    break;
#else
                    glInternal = glFormat = GL_RGBA;
                    glType = GL_UNSIGNED_BYTE;
                    break;
#endif
            }
        }

        /**
         * 还原 GL_LUMINANCE / GL_LUMINANCE_ALPHA / GL_ALPHA 的采样语义。
         *
         * swizzle 是纹理对象状态，调用前目标纹理必须已绑定。不能省：只换成 R8
         * 而不设 swizzle，采样结果是
         * (L,0,0,1)，原本当作灰度使用的地方会整片变红。 调用方需在 glTexImage2D
         * 之后立即调用一次；glTexSubImage2D 不改纹理格式， 无需重复设置。
         */
        static void applyLuminanceSwizzle(PixelFormat format) {
            switch(format) {
                case PixelFormat::I8: // 旧 GL_LUMINANCE
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R,
                                    GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G,
                                    GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B,
                                    GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A,
                                    GL_ONE);
                    break;
                case PixelFormat::A8: // 旧 GL_ALPHA
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R,
                                    GL_ZERO);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G,
                                    GL_ZERO);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B,
                                    GL_ZERO);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A,
                                    GL_RED);
                    break;
                case PixelFormat::AI88: // 旧 GL_LUMINANCE_ALPHA
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R,
                                    GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G,
                                    GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B,
                                    GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A,
                                    GL_GREEN);
                    break;
                default:
                    break;
            }
        }
    };

} // namespace krkr
