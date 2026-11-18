#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "visual/DrawList.h"
#include "core/contracts/GLApi.h"

namespace krkr::visual {
  using krkr::render::GLApi;

  struct GLRenderStats {
    std::size_t itemsDrawn = 0;
    std::size_t itemsSkipped = 0;
    std::size_t texturesUploaded = 0;
  };

  // GLES3 风格后端：着色器 + 顶点属性 + 纹理，无固定管线。
  //
  // 只覆盖 8a 已实现的 5 种算子；命中不到的算子显式跳过并计数（不静默画错）。
  // 像素权威仍是 SoftwareBackend —— GL 的验收标准是"与软件输出逐像素 ±1"。
  class GLBackend final {
    public:
      GLBackend() = default;
      ~GLBackend();

      GLBackend(const GLBackend &) = delete;
      GLBackend &operator = (const GLBackend &) = delete;

      bool Initialize(const GLApi &api);
      bool IsReady() const { return ready_; }

      // 把 list 画到当前绑定的帧缓冲。viewport 由调用方设置（后端不假设窗口/离屏）。
      bool Render(const DrawList &list, int width, int height, GLRenderStats *stats = nullptr);

      void Shutdown();

      const std::string &GetLastErrorText() const { return lastError_; }

    private:
      bool EnsureProgram();
      std::uint32_t EnsureTexture(const Texture *texture);
      void ApplyBlend(LayerType type);

      krkr::render::GLApi api_;
      bool ready_ = false;
      std::string lastError_;

      krkr::render::GLuint program_ = 0;
      unsigned int vertexArray_ = 0;
      krkr::render::GLuint vertexBuffer_ = 0;
      krkr::render::GLint uniformTexture_ = -1;
      krkr::render::GLint uniformOpacity_ = -1;
      krkr::render::GLint uniformUseTexture_ = -1;
      krkr::render::GLint uniformColor_ = -1;

      std::unordered_map<const Texture *, krkr::render::GLuint> textures_;
      GLRenderStats stats_;
  };
} // namespace krkr::visual
