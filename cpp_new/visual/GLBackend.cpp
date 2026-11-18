#include "visual/GLBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace krkr::visual {

using krkr::render::GLuint;
using krkr::render::GLenum;
using krkr::render::GLint;
using krkr::render::GLsizei;
using krkr::render::GLsizeiptr;
using krkr::render::GLfloat;
using krkr::render::GLchar;

namespace {

const char *const kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

// 纯色与纹理走同一个着色器：uUseTexture 决定取色方式。
// 混合由 glBlendFunc 完成，保持与软件后端同一套语义（直通 alpha）。
const char *const kFragmentShader = R"(#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform int uUseTexture;
uniform float uOpacity;
uniform vec4 uColor;
out vec4 oColor;
void main() {
    vec4 color = uUseTexture != 0 ? texture(uTexture, vTexCoord) : uColor;
    color.a *= uOpacity;
    oColor = color;
}
)";

GLuint CompileShader(const GLApi &api, GLenum type, const char *source, std::string &error) {
  const GLuint shader = api.CreateShader(type);
  if (shader == 0) {
    error = "CreateShader failed";
    return 0;
  }
  api.ShaderSource(shader, 1, &source, nullptr);
  api.CompileShader(shader);
  GLint status = 0;
  api.GetShaderiv(shader, KRKR_GL_COMPILE_STATUS, &status);
  if (status == 0) {
    char log[1024] = {};
    GLsizei length = 0;
    api.GetShaderInfoLog(shader, sizeof(log) - 1, &length, log);
    error = std::string("shader compile failed: ") + log;
    api.DeleteShader(shader);
    return 0;
  }
  return shader;
}

bool IsAxisAligned(const DrawItem &item) {
  const float epsilon = 0.001f;
  return std::fabs(item.vertices[0][1] - item.vertices[1][1]) < epsilon &&
         std::fabs(item.vertices[2][1] - item.vertices[3][1]) < epsilon &&
         std::fabs(item.vertices[0][0] - item.vertices[2][0]) < epsilon &&
         std::fabs(item.vertices[1][0] - item.vertices[3][0]) < epsilon;
}

} // namespace

GLBackend::~GLBackend() { Shutdown(); }

bool GLBackend::Initialize(const GLApi &api) {
  api_ = api;
  if (!api_.IsComplete()) {
    lastError_ = "GL entry points are incomplete";
    ready_ = false;
    return false;
  }
  api_.PixelStorei(KRKR_GL_UNPACK_ALIGNMENT, 1);
  api_.PixelStorei(KRKR_GL_PACK_ALIGNMENT, 1);
  // core profile 要求绑定 VAO 才能绘制，否则 glDrawArrays 是无效操作。
  api_.GenVertexArrays(1, &vertexArray_);
  if (vertexArray_ == 0) {
    lastError_ = "GenVertexArrays failed";
    ready_ = false;
    return false;
  }
  api_.BindVertexArray(vertexArray_);

  api_.GenBuffers(1, &vertexBuffer_);
  if (vertexBuffer_ == 0) {
    lastError_ = "GenBuffers failed";
    ready_ = false;
    return false;
  }
  if (!EnsureProgram()) {
    ready_ = false;
    return false;
  }
  ready_ = true;
  return true;
}

bool GLBackend::EnsureProgram() {
  if (program_ != 0) {
    return true;
  }

  const GLuint vertex = CompileShader(api_, KRKR_GL_VERTEX_SHADER, kVertexShader, lastError_);
  if (vertex == 0) {
    return false;
  }
  const GLuint fragment =
      CompileShader(api_, KRKR_GL_FRAGMENT_SHADER, kFragmentShader, lastError_);
  if (fragment == 0) {
    api_.DeleteShader(vertex);
    return false;
  }

  program_ = api_.CreateProgram();
  api_.AttachShader(program_, vertex);
  api_.AttachShader(program_, fragment);
  api_.LinkProgram(program_);
  api_.DeleteShader(vertex);
  api_.DeleteShader(fragment);

  GLint status = 0;
  api_.GetProgramiv(program_, KRKR_GL_LINK_STATUS, &status);
  if (status == 0) {
    char log[1024] = {};
    GLsizei length = 0;
    api_.GetProgramInfoLog(program_, sizeof(log) - 1, &length, log);
    lastError_ = std::string("program link failed: ") + log;
    return false;
  }

  uniformTexture_ = api_.GetUniformLocation(program_, "uTexture");
  uniformOpacity_ = api_.GetUniformLocation(program_, "uOpacity");
  uniformUseTexture_ = api_.GetUniformLocation(program_, "uUseTexture");
  uniformColor_ = api_.GetUniformLocation(program_, "uColor");
  return true;
}

std::uint32_t GLBackend::EnsureTexture(const Texture *texture) {
  if (texture == nullptr || texture->width <= 0 || texture->height <= 0) {
    return 0;
  }
  const auto found = textures_.find(texture);
  if (found != textures_.end()) {
    return found->second;
  }

  // 引擎侧像素是 0xAARRGGBB（内存里是 B,G,R,A）。GLES3 只有 GL_RGBA，
  // 因此上传前转成 R,G,B,A，避免依赖桌面专属的 GL_BGRA。
  std::vector<std::uint8_t> rgba(texture->pixels.size() * 4);
  for (std::size_t i = 0; i < texture->pixels.size(); ++i) {
    const std::uint32_t pixel = texture->pixels[i];
    rgba[i * 4 + 0] = static_cast<std::uint8_t>((pixel >> 16) & 0xffu);
    rgba[i * 4 + 1] = static_cast<std::uint8_t>((pixel >> 8) & 0xffu);
    rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixel & 0xffu);
    rgba[i * 4 + 3] = static_cast<std::uint8_t>((pixel >> 24) & 0xffu);
  }

  GLuint id = 0;
  api_.GenTextures(1, &id);
  if (id == 0) {
    return 0;
  }
  api_.BindTexture(KRKR_GL_TEXTURE_2D, id);
  api_.TexParameteri(KRKR_GL_TEXTURE_2D, KRKR_GL_TEXTURE_MIN_FILTER, KRKR_GL_NEAREST);
  api_.TexParameteri(KRKR_GL_TEXTURE_2D, KRKR_GL_TEXTURE_MAG_FILTER, KRKR_GL_NEAREST);
  api_.TexParameteri(KRKR_GL_TEXTURE_2D, KRKR_GL_TEXTURE_WRAP_S, KRKR_GL_CLAMP_TO_EDGE);
  api_.TexParameteri(KRKR_GL_TEXTURE_2D, KRKR_GL_TEXTURE_WRAP_T, KRKR_GL_CLAMP_TO_EDGE);
  api_.TexImage2D(KRKR_GL_TEXTURE_2D, 0, KRKR_GL_RGBA, texture->width, texture->height, 0,
                  KRKR_GL_RGBA, KRKR_GL_UNSIGNED_BYTE, rgba.data());

  textures_.emplace(texture, id);
  ++stats_.texturesUploaded;
  return id;
}

void GLBackend::ApplyBlend(LayerType type) {
  switch (type) {
    case ltOpaque:
      api_.BlendFuncSeparate(KRKR_GL_ONE, KRKR_GL_ZERO, KRKR_GL_ONE, KRKR_GL_ZERO);
      break;
    case ltAlpha:
      api_.BlendFuncSeparate(KRKR_GL_SRC_ALPHA, KRKR_GL_ONE_MINUS_SRC_ALPHA, KRKR_GL_ONE,
                             KRKR_GL_ONE_MINUS_SRC_ALPHA);
      break;
    case ltAdditive:
      api_.BlendFuncSeparate(KRKR_GL_SRC_ALPHA, KRKR_GL_ONE, KRKR_GL_ONE, KRKR_GL_ONE);
      break;
    case ltSubtractive:
      api_.BlendEquation(KRKR_GL_FUNC_REVERSE_SUBTRACT);
      api_.BlendFuncSeparate(KRKR_GL_SRC_ALPHA, KRKR_GL_ONE, KRKR_GL_ONE, KRKR_GL_ONE);
      break;
    case ltMultiplicative:
      // GL 的固定管线只能近似"dst * src"；完整语义靠软件后端兜底。
      api_.BlendFuncSeparate(KRKR_GL_DST_COLOR, KRKR_GL_ZERO, KRKR_GL_ONE, KRKR_GL_ONE);
      break;
  }
}

bool GLBackend::Render(const DrawList &list, int width, int height, GLRenderStats *statsOut) {
  if (!ready_) {
    lastError_ = "backend is not initialized";
    return false;
  }
  if (width <= 0 || height <= 0) {
    lastError_ = "invalid viewport";
    return false;
  }

  stats_.itemsDrawn = 0;
  stats_.itemsSkipped = 0;

  if (list.clear) {
    const std::uint32_t color = list.clearColor;
    api_.ClearColor(static_cast<GLfloat>((color >> 16) & 0xffu) / 255.0f,
                    static_cast<GLfloat>((color >> 8) & 0xffu) / 255.0f,
                    static_cast<GLfloat>(color & 0xffu) / 255.0f,
                    static_cast<GLfloat>((color >> 24) & 0xffu) / 255.0f);
    api_.Clear(KRKR_GL_COLOR_BUFFER_BIT);
  }

  api_.UseProgram(program_);
  api_.Enable(KRKR_GL_BLEND);
  api_.ActiveTexture(KRKR_GL_TEXTURE0);
  api_.Uniform1i(uniformTexture_, 0);
  api_.BindVertexArray(vertexArray_);
  api_.BindBuffer(KRKR_GL_ARRAY_BUFFER, vertexBuffer_);

  const auto toClipX = [width](float x) { return x / static_cast<float>(width) * 2.0f - 1.0f; };
  const auto toClipY = [height](float y) { return 1.0f - y / static_cast<float>(height) * 2.0f; };

  for (const DrawItem &item : list.items) {
    if (!IsAxisAligned(item)) {
      ++stats_.itemsSkipped;
      continue;
    }

    const Texture *texture = list.Resolve(item.textureId);
    const std::uint32_t glTexture = EnsureTexture(texture);
    if (texture != nullptr && glTexture == 0) {
      ++stats_.itemsSkipped;
      continue;
    }

    // 三角形条带：TL, TR, BL, BR。
    const float quad[4][4] = {
      {toClipX(item.vertices[0][0]), toClipY(item.vertices[0][1]), item.uvs[0][0], item.uvs[0][1]},
      {toClipX(item.vertices[1][0]), toClipY(item.vertices[1][1]), item.uvs[1][0], item.uvs[1][1]},
      {toClipX(item.vertices[2][0]), toClipY(item.vertices[2][1]), item.uvs[2][0], item.uvs[2][1]},
      {toClipX(item.vertices[3][0]), toClipY(item.vertices[3][1]), item.uvs[3][0], item.uvs[3][1]},
    };

    api_.BufferData(KRKR_GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(quad)), quad,
                    KRKR_GL_STREAM_DRAW);
    api_.EnableVertexAttribArray(0);
    api_.EnableVertexAttribArray(1);
    api_.VertexAttribPointer(0, 2, KRKR_GL_FLOAT, 0, sizeof(float) * 4, nullptr);
    api_.VertexAttribPointer(1, 2, KRKR_GL_FLOAT, 0, sizeof(float) * 4,
                             reinterpret_cast<const void *>(sizeof(float) * 2));

    if (glTexture != 0) {
      api_.BindTexture(KRKR_GL_TEXTURE_2D, glTexture);
      api_.Uniform1i(uniformUseTexture_, 1);
      api_.Uniform1f(uniformOpacity_, static_cast<GLfloat>(item.opacity) / 255.0f);
    } else {
      const std::uint32_t color = item.color;
      api_.Uniform1i(uniformUseTexture_, 0);
      api_.Uniform1f(uniformOpacity_, static_cast<GLfloat>(item.opacity) / 255.0f);
      api_.Uniform4f(uniformColor_, static_cast<GLfloat>((color >> 16) & 0xffu) / 255.0f,
                     static_cast<GLfloat>((color >> 8) & 0xffu) / 255.0f,
                     static_cast<GLfloat>(color & 0xffu) / 255.0f,
                     static_cast<GLfloat>((color >> 24) & 0xffu) / 255.0f);
    }

    const bool clipValid = item.clip.Width() > 0 && item.clip.Height() > 0;
    if (clipValid) {
      api_.Enable(KRKR_GL_SCISSOR_TEST);
      api_.Scissor(item.clip.left, height - item.clip.bottom, item.clip.Width(),
                   item.clip.Height());
    }

    api_.BlendEquation(KRKR_GL_FUNC_ADD);
    ApplyBlend(item.blend);
    api_.DrawArrays(KRKR_GL_TRIANGLE_STRIP, 0, 4);
    if (clipValid) {
      api_.Disable(KRKR_GL_SCISSOR_TEST);
    }
    ++stats_.itemsDrawn;
  }

  if (statsOut != nullptr) {
    *statsOut = stats_;
  }
  return api_.GetError() == KRKR_GL_NO_ERROR_VALUE;
}

void GLBackend::Shutdown() {
  if (!ready_ && program_ == 0 && textures_.empty() && vertexBuffer_ == 0) {
    return;
  }
  for (const auto &entry : textures_) {
    api_.DeleteTextures(1, &entry.second);
  }
  textures_.clear();
  if (vertexBuffer_ != 0 && api_.BindBuffer != nullptr) {
    api_.BindBuffer(KRKR_GL_ARRAY_BUFFER, 0);
  }
  if (vertexArray_ != 0 && api_.BindVertexArray != nullptr) {
    api_.BindVertexArray(0);
  }
  program_ = 0;
  vertexArray_ = 0;
  vertexBuffer_ = 0;
  ready_ = false;
}

} // namespace krkr::visual
