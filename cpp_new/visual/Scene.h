#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "visual/DrawList.h"
#include "visual/Surface.h"

namespace krkr::visual {
  // 图层树的一个节点。
  //
  // 只描述"画什么、画在哪"，不含脚本或平台概念：桥接层把脚本参数写进来，
  // 后端只消费合成出来的 DrawList（docs/render-spec.md §14.1）。
  //
  // 坐标语义与参照实现的 Layer 一致：
  //   - left/top 相对父层原点（窗口根为 (0,0)）；
  //   - clip 只作用于本层自己的四边形，不下传给子层；
  //   - visible 为假时本层与子层都不画（参照实现的 nodeVisible）。
  struct SceneNode {
    std::string name;
    bool visible = false;
    int left = 0;
    int top = 0;
    int width = 32;
    int height = 32;
    int imageLeft = 0;
    int imageTop = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    std::uint32_t opacity = 255;
    // 原值 = tTVPLayerType；0（ltBinder）表示不画自己只画子层。
    std::uint32_t type = ltAlpha;
    bool hasClip = false;
    Rect clip;
    // 没有纹理时的纯色填充（Layer.colorRect / 默认层位图）。
    bool hasFill = false;
    std::uint32_t fill = 0;
    std::shared_ptr<const Texture> texture;
    std::vector<std::shared_ptr<SceneNode>> children;
    SceneNode *parent = nullptr;  // 非拥有；由 AttachNode/DetachNode 维护
  };

  // 把 child 挂到 parent 下；child 原有父层会被摘除。空指针被忽略。
  void AttachNode(const std::shared_ptr<SceneNode> &parent,
                  const std::shared_ptr<SceneNode> &child);
  // 从当前父层摘除（不销毁）。
  void DetachNode(const std::shared_ptr<SceneNode> &child);

  // 合成过程的计数：断言用它区分"没画"和"画了但被跳过"。
  struct SceneBuildStats {
    std::size_t nodesVisited = 0;
    std::size_t itemsEmitted = 0;
    std::size_t nodesHidden = 0;
    std::size_t nodesUnsupported = 0;  // 混合算子未实现：跳过自身，子层照画
    std::size_t itemsClippedOut = 0;
  };

  // 一个窗口的图层树。
  class Scene final {
    public:
      Scene(int width, int height);

      int Width() const { return width_; }
      int Height() const { return height_; }
      // 尺寸变化同时更新窗口根节点的矩形；返回 false 表示尺寸非法（未改动）。
      bool SetSize(int width, int height);

      // 窗口底色。参照实现的窗口层初始为不透明黑。
      std::uint32_t clearColor = 0xff000000u;

      const std::shared_ptr<SceneNode> &Root() const { return root_; }

      // 窗口 → 图层树 → DrawList。纹理由本场景持有，DrawList 仅在本次调用内有效
      // （DrawList 只存指针，调用方负责场景活得更久）。
      void BuildDrawList(DrawList &out, SceneBuildStats *stats = nullptr) const;

      // 节点总数（含窗口根），用于断言。
      std::size_t CountNodes() const;

    private:
      int width_ = 0;
      int height_ = 0;
      std::shared_ptr<SceneNode> root_;
  };
} // namespace krkr::visual
