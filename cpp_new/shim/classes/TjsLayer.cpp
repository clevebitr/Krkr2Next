#include "shim/classes/TjsLayer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/TjsDiagnostics.h"
#include "shim/classes/TjsWindowInternals.h"
#include "shim/engine/NativeClass.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"
#include "tjs/tjsNative.h"
#include "visual/Image.h"
#include "visual/ImageDecoder.h"

namespace krkr::script {

namespace {

// 从存储读整个文件。失败时抛带名字的异常，返回空串不用于继续。
std::string ReadStorageFile(const std::string &name, const std::string &context) {
  krkr::io::Storage *storage = GetContext().storage;
  if (storage == nullptr) {
    ThrowScriptException(context, "storage is not mounted");
  }
  const std::unique_ptr<krkr::io::IStream> stream = storage->Open(name);
  if (stream == nullptr) {
    ThrowScriptException(context + "(\"" + name + "\")", storage->GetLastErrorText());
  }
  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(stream->GetSize() > 0 ? stream->GetSize() : 0));
  char buffer[64 * 1024];
  std::int64_t read = 0;
  while ((read = stream->Read(buffer, static_cast<std::int64_t>(sizeof(buffer)))) > 0) {
    bytes.append(buffer, static_cast<std::size_t>(read));
  }
  if (read < 0) {
    ThrowScriptException(context + "(\"" + name + "\")", "read failed");
  }
  return bytes;
}

std::shared_ptr<krkr::visual::Texture> LoadTexture(const std::string &name,
                                                   const std::string &context) {
  const std::string bytes = ReadStorageFile(name, context);
  krkr::visual::Image image;
  std::string error;
  if (!krkr::visual::DecodeImage(bytes.data(), bytes.size(), image, error)) {
    ThrowScriptException(context + "(\"" + name + "\")", error);
  }
  auto texture = std::make_shared<krkr::visual::Texture>();
  texture->width = image.width;
  texture->height = image.height;
  texture->pixels = std::move(image.pixels);
  return texture;
}

// 对象已被回收（还在脚本作用域里但原生实例失效）时显式失败，而不是访问空节点。
detail::LayerNativeInstance &ActiveLayer(detail::LayerNativeInstance *layer,
                                        const char *context) {
  if (layer == nullptr || layer->node == nullptr) {
    ThrowScriptException(context, "the layer has been invalidated");
  }
  return *layer;
}

// 把节点在其父层的子序列里移动到指定位置（bringToFront / moveBefore 等共用）。
void MoveChildTo(const std::shared_ptr<krkr::visual::SceneNode> &node, std::size_t index) {
  if (node == nullptr || node->parent == nullptr) {
    return;
  }
  auto &siblings = node->parent->children;
  const auto found = std::find(siblings.begin(), siblings.end(), node);
  if (found == siblings.end()) {
    return;
  }
  siblings.erase(found);
  if (index > siblings.size()) {
    index = siblings.size();
  }
  siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), node);
}

std::size_t IndexOf(const std::shared_ptr<krkr::visual::SceneNode> &node) {
  if (node == nullptr || node->parent == nullptr) {
    return 0;
  }
  const auto &siblings = node->parent->children;
  const auto found = std::find(siblings.begin(), siblings.end(), node);
  if (found == siblings.end()) {
    return siblings.size();
  }
  return static_cast<std::size_t>(std::distance(siblings.begin(), found));
}

void SetClip(detail::LayerNativeInstance &layer, int left, int top, int width, int height,
             const char *context) {
  if (width < 0 || height < 0) {
    ThrowScriptException(context, "clip size must not be negative");
  }
  layer.node->clip.left = left;
  layer.node->clip.top = top;
  layer.node->clip.right = left + width;
  layer.node->clip.bottom = top + height;
  layer.node->hasClip = width > 0 && height > 0;
}

void SetNodeSize(detail::LayerNativeInstance &layer, int width, int height,
                 const char *context) {
  if (width < 0 || height < 0) {
    ThrowScriptException(context, "layer size must not be negative");
  }
  layer.node->width = width;
  layer.node->height = height;
}

class LayerClass final : public TJS::tTJSNativeClass {
  public:
    static tjs_uint32 ClassID;

    LayerClass() : tTJSNativeClass(TJS_W("Layer")) {
      TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/ Layer)
      TJS_DECL_EMPTY_FINALIZE_METHOD

      TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var. name*/ _this,
                                        /*var. type*/ detail::LayerNativeInstance,
                                        /*TJS class name*/ Layer) {
        return TJS_S_OK;
      }
      TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Layer)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPos) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        ActiveLayer(_this, "Layer.setPos").node->left = static_cast<tjs_int>(*param[0]);
        _this->node->top = static_cast<tjs_int>(*param[1]);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPos)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSize) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        SetNodeSize(ActiveLayer(_this, "Layer.setSize"), static_cast<tjs_int>(*param[0]),
                    static_cast<tjs_int>(*param[1]), "Layer.setSize");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSize)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSizeToImageSize) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.setSizeToImageSize");
        SetNodeSize(layer, layer.node->imageWidth, layer.node->imageHeight,
                    "Layer.setSizeToImageSize");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSizeToImageSize)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setImagePos) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        ActiveLayer(_this, "Layer.setImagePos").node->imageLeft =
            static_cast<tjs_int>(*param[0]);
        _this->node->imageTop = static_cast<tjs_int>(*param[1]);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setImagePos)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setImageSize) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 2) {
          return TJS_E_BADPARAMCOUNT;
        }
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.setImageSize");
        const tjs_int width = static_cast<tjs_int>(*param[0]);
        const tjs_int height = static_cast<tjs_int>(*param[1]);
        if (width < 0 || height < 0) {
          ThrowScriptException("Layer.setImageSize", "image size must not be negative");
        }
        layer.node->imageWidth = width;
        layer.node->imageHeight = height;
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setImageSize)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setClip) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 4) {
          return TJS_E_BADPARAMCOUNT;
        }
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.setClip");
        SetClip(layer, static_cast<tjs_int>(*param[0]), static_cast<tjs_int>(*param[1]),
                static_cast<tjs_int>(*param[2]), static_cast<tjs_int>(*param[3]),
                "Layer.setClip");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setClip)

      // 读入图层位图。与参照实现一致：只换位图，不改变图层尺寸，
      // 需要尺寸跟随图片时脚本要自己调 setSizeToImageSize。
      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ loadImages) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.loadImages");
        const std::string name = detail::Utf8Text(*param[0]);
        layer.node->texture = LoadTexture(name, "Layer.loadImages");
        layer.node->hasFill = false;
        layer.node->imageLeft = 0;
        layer.node->imageTop = 0;
        layer.node->imageWidth = layer.node->texture->width;
        layer.node->imageHeight = layer.node->texture->height;
        krkr::log::Info("Layer.loadImages(\"", name, "\") = ", layer.node->imageWidth, "x",
                        layer.node->imageHeight);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ loadImages)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ loadProvinceImage) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        ActiveLayer(_this, "Layer.loadProvinceImage");
        // 命中区域（Province）属于第 9 步的输入模型，这里显式失败而不是装作成功。
        ThrowScriptException("Layer.loadProvinceImage",
                             "province images are not implemented yet (input model, step 9)");
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ loadProvinceImage)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ bringToFront) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.bringToFront");
        MoveChildTo(layer.node, layer.node->parent != nullptr
                                    ? layer.node->parent->children.size()
                                    : 0);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ bringToFront)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ bringToBack) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.bringToBack");
        MoveChildTo(layer.node, 0);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ bringToBack)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ moveBefore) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.moveBefore");
        detail::LayerNativeInstance *other = detail::ResolveLayer(*param[0], "Layer.moveBefore");
        if (other->node == nullptr || other->node->parent != layer.node->parent) {
          ThrowScriptException("Layer.moveBefore", "the given layer has a different parent");
        }
        MoveChildTo(layer.node, IndexOf(other->node));
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ moveBefore)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ moveBehind) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        if (numparams < 1) {
          return TJS_E_BADPARAMCOUNT;
        }
        detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.moveBehind");
        detail::LayerNativeInstance *other = detail::ResolveLayer(*param[0], "Layer.moveBehind");
        if (other->node == nullptr || other->node->parent != layer.node->parent) {
          ThrowScriptException("Layer.moveBehind", "the given layer has a different parent");
        }
        MoveChildTo(layer.node, IndexOf(other->node) + 1);
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ moveBehind)

      TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ update) {
        TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
        // 合成由宿主 tick 统一驱动；脚本的 update 只表达"这一帧有变化"。
        krkr::log::Debug("Layer.update: composition is driven by the host tick");
        return TJS_S_OK;
      }
      TJS_END_NATIVE_METHOD_DECL(/*func. name*/ update)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ visible)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = (ActiveLayer(_this, "Layer.visible").node->visible) ? 1 : 0;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.visible").node->visible = static_cast<bool>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ visible)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ opacity)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = static_cast<tjs_int>(ActiveLayer(_this, "Layer.opacity").node->opacity);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          tjs_int value = static_cast<tjs_int>(*param);
          value = std::clamp<tjs_int>(value, 0, 255);
          ActiveLayer(_this, "Layer.opacity").node->opacity = static_cast<std::uint32_t>(value);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ opacity)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ type)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = static_cast<tjs_int>(ActiveLayer(_this, "Layer.type").node->type);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          const tjs_int value = static_cast<tjs_int>(*param);
          if (value < 0 || value > 28) {
            ThrowScriptException("Layer.type", "layer type is out of range (0..28)");
          }
          ActiveLayer(_this, "Layer.type").node->type = static_cast<std::uint32_t>(value);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ type)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ left)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.left").node->left;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.left").node->left = static_cast<tjs_int>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ left)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ top)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.top").node->top;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.top").node->top = static_cast<tjs_int>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ top)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ width)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.width").node->width;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.width");
          SetNodeSize(layer, static_cast<tjs_int>(*param), layer.node->height, "Layer.width");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ width)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ height)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.height").node->height;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.height");
          SetNodeSize(layer, layer.node->width, static_cast<tjs_int>(*param), "Layer.height");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ height)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ imageLeft)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.imageLeft").node->imageLeft;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.imageLeft").node->imageLeft = static_cast<tjs_int>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ imageLeft)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ imageTop)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.imageTop").node->imageTop;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.imageTop").node->imageTop = static_cast<tjs_int>(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ imageTop)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ imageWidth)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.imageWidth").node->imageWidth;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          const tjs_int value = static_cast<tjs_int>(*param);
          if (value < 0) {
            ThrowScriptException("Layer.imageWidth", "image size must not be negative");
          }
          ActiveLayer(_this, "Layer.imageWidth").node->imageWidth = value;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ imageWidth)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ imageHeight)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.imageHeight").node->imageHeight;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          const tjs_int value = static_cast<tjs_int>(*param);
          if (value < 0) {
            ThrowScriptException("Layer.imageHeight", "image size must not be negative");
          }
          ActiveLayer(_this, "Layer.imageHeight").node->imageHeight = value;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ imageHeight)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ name)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = WideFromUtf8(ActiveLayer(_this, "Layer.name").node->name).c_str();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.name").node->name = detail::Utf8Text(*param);
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ name)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ clipLeft)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.clipLeft").node->clip.left;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.clipLeft");
          SetClip(layer, static_cast<tjs_int>(*param), layer.node->clip.top,
                  layer.node->clip.Width(), layer.node->clip.Height(), "Layer.clipLeft");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ clipLeft)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ clipTop)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.clipTop").node->clip.top;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.clipTop");
          SetClip(layer, layer.node->clip.left, static_cast<tjs_int>(*param),
                  layer.node->clip.Width(), layer.node->clip.Height(), "Layer.clipTop");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ clipTop)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ clipWidth)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.clipWidth").node->clip.Width();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.clipWidth");
          SetClip(layer, layer.node->clip.left, layer.node->clip.top,
                  static_cast<tjs_int>(*param), layer.node->clip.Height(),
                  "Layer.clipWidth");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ clipWidth)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ clipHeight)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          *result = ActiveLayer(_this, "Layer.clipHeight").node->clip.Height();
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_BEGIN_NATIVE_PROP_SETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          detail::LayerNativeInstance &layer = ActiveLayer(_this, "Layer.clipHeight");
          SetClip(layer, layer.node->clip.left, layer.node->clip.top,
                  layer.node->clip.Width(), static_cast<tjs_int>(*param),
                  "Layer.clipHeight");
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ clipHeight)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ window)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.window");
          if (_this->windowObject.Type() == tvtVoid) {
            ThrowScriptException("Layer.window", "the owning window has been invalidated");
          }
          *result = _this->windowObject;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ window)

      TJS_BEGIN_NATIVE_PROP_DECL(/*prop. name*/ parent)
      {
        TJS_BEGIN_NATIVE_PROP_GETTER {
          TJS_GET_NATIVE_INSTANCE(_this, detail::LayerNativeInstance);
          ActiveLayer(_this, "Layer.parent");
          // 主图层的父是窗口根，不是 Layer 对象，因此返回 void（参照实现返回 null）。
          *result = _this->parentObject;
          return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER

        TJS_DENY_NATIVE_PROP_SETTER
      }
      TJS_END_NATIVE_PROP_DECL(/*prop. name*/ parent)

      TJS_END_NATIVE_MEMBERS
    }

  protected:
    TJS::iTJSNativeInstance *CreateNativeInstance() override {
      return new detail::LayerNativeInstance();
    }
};

} // namespace

namespace detail {

tjs_uint32 LayerClassID() { return LayerClass::ClassID; }

LayerNativeInstance *ResolveLayer(const tTJSVariant &value, const std::string &context) {
  iTJSDispatch2 *object = value.AsObjectClosureNoAddRef().Object;
  if (object == nullptr) {
    ThrowScriptException(context, "the argument is not an object");
  }
  LayerNativeInstance *layer = nullptr;
  if (TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, LayerClassID(),
                                               reinterpret_cast<iTJSNativeInstance **>(&layer))) ||
      layer == nullptr) {
    ThrowScriptException(context, "the argument is not a Layer");
  }
  return layer;
}

tjs_error TJS_INTF_METHOD LayerNativeInstance::Construct(tjs_int numparams, tTJSVariant **param,
                                                         iTJSDispatch2 *tjs_obj) {
  if (numparams < 1) {
    return TJS_E_BADPARAMCOUNT;
  }

  WindowNativeInstance *owner = ResolveWindow(*param[0], "Layer");

  // 参照实现的层默认值：ltAlpha、不可见、32x32、全透明位图。
  // 脚本要显式 visible = true 才会出现在合成结果里。
  node = std::make_shared<krkr::visual::SceneNode>();
  node->type = krkr::visual::ltAlpha;
  node->visible = false;
  node->width = 32;
  node->height = 32;
  node->hasFill = true;
  node->fill = 0x00000000u;

  ownerObject = tjs_obj;
  window = owner->handle;
  iTJSDispatch2 *windowRaw = param[0]->AsObjectClosureNoAddRef().Object;
  windowObject = tTJSVariant(windowRaw, windowRaw);

  std::shared_ptr<krkr::visual::SceneNode> parentNode =
      owner->scene != nullptr ? owner->scene->Root() : nullptr;
  if (parentNode == nullptr) {
    ThrowScriptException("Layer", "the owning window has no scene");
  }

  if (numparams >= 2) {
    iTJSDispatch2 *parentRaw = param[1]->AsObjectClosureNoAddRef().Object;
    if (parentRaw != nullptr) {
      LayerNativeInstance *parent = ResolveLayer(*param[1], "Layer");
      if (parent->node == nullptr) {
        ThrowScriptException("Layer", "the parent layer has been invalidated");
      }
      parentNode = parent->node;
      parentObject = tTJSVariant(parentRaw, parentRaw);
    }
  }

  krkr::visual::AttachNode(parentNode, node);

  // 没有父层的层就是该窗口的主图层（参照实现的 AttachPrimary）。
  const bool isPrimary = parentNode == owner->scene->Root();
  if (isPrimary) {
    if (owner->primaryLayer != nullptr) {
      krkr::log::Warn("Layer: replacing the primary layer of this window");
    }
    owner->primaryLayer = tjs_obj;
  }

  krkr::log::Info("script: Layer created (", isPrimary ? "primary" : "child of layer",
                  ", scene has ", owner->scene->CountNodes(), " node(s))");
  return TJS_S_OK;
}

void TJS_INTF_METHOD LayerNativeInstance::Invalidate() {
  if (auto handle = window.lock(); handle != nullptr && handle->instance != nullptr &&
                                   handle->instance->primaryLayer == ownerObject) {
    handle->instance->primaryLayer = nullptr;
  }
  if (node != nullptr) {
    krkr::visual::DetachNode(node);
    node.reset();
  }
  windowObject.Clear();
  parentObject.Clear();
  ownerObject = nullptr;
}

} // namespace detail

tjs_uint32 LayerClass::ClassID = static_cast<tjs_uint32>(-1);

void RegisterLayerClass(const ScriptContext &context) {
  if (!ExposeNativeClass(context.engine, TJS_W("Layer"), new LayerClass())) {
    return;
  }
  krkr::log::Info("script: Layer registered");
}

} // namespace krkr::script
