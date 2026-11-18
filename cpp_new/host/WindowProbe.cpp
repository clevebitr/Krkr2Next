#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "engine/ScriptRuntime.h"
#include "host/ProbeHost.h"
#include "shim/classes/TjsWindow.h"
#include "shim/engine/TjsEngine.h"
#include "visual/DrawList.h"
#include "visual/Scene.h"
#include "visual/SoftwareBackend.h"
#include "visual/Surface.h"

// 场景合成测试台（第 7g 步：Window / Layer 脚本桥接）。
//
//   window_probe <scene-script.tjs> <root-path-file> [out.ppm]
//
// 做的事：跑一段建立窗口与图层的脚本 → 取主场景 → 合成 DrawList →
// 软件后端出像素 → 对手算的期望值逐点断言 → 写 PPM。
// 期望值写死在这里（不是拿另一份渲染结果对照），这样"脚本参数没接上"
// 会表现为像素不符，而不是两边一起错。

using krkr::hostprobe::IsAssertionPass;
using krkr::hostprobe::ProbeHost;
using krkr::hostprobe::ReadRootPath;
using krkr::hostprobe::ReadWholeFile;

namespace {

constexpr int kSceneWidth = 32;
constexpr int kSceneHeight = 24;
constexpr std::uint32_t kBlack = 0xFF000000u;

int failures = 0;

void Check(bool condition, const char *what) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) {
    ++failures;
  }
}

void CheckPixel(const krkr::visual::Surface &surface, int x, int y,
                std::uint32_t expected, const char *what) {
  const std::uint32_t actual = surface.Get(x, y);
  const bool ok = actual == expected;
  std::printf("%s  %s (pixel %d,%d = 0x%08X, expected 0x%08X)\n", ok ? "PASS" : "FAIL",
              what, x, y, actual, expected);
  if (!ok) {
    ++failures;
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: window_probe <scene-script.tjs> <root-path-file> [out.ppm]\n");
    return 2;
  }

  std::string script;
  if (!ReadWholeFile(argv[1], script)) {
    std::fprintf(stderr, "cannot read script: %s\n", argv[1]);
    return 2;
  }

  std::string root;
  if (!ReadRootPath(argv[2], root) || root.empty()) {
    std::fprintf(stderr, "cannot read root path file: %s\n", argv[2]);
    return 2;
  }

  krkr::io::Storage storage(root);
  if (!storage.Mounted()) {
    std::fprintf(stderr, "cannot mount %s: %s\n", root.c_str(), storage.GetLastErrorText());
    return 2;
  }

  std::printf("script   = %s\n", argv[1]);
  std::printf("root     = %s\n", root.c_str());

  ProbeHost host(argc, argv);
  host.SetExecutablePath(argv[0]);
  host.SetDataDirectory(root);

  krkr::engine::ScriptRuntime runtime;
  std::string error;
  if (!runtime.Startup(&storage, host.Services(), &error)) {
    std::fprintf(stderr, "script runtime startup failed: %s\n", error.c_str());
    return 1;
  }

  // 与 tjs_probe 同一约定：脚本以抛出 "<name>-ok" 表示全部断言通过。
  const bool runOk = runtime.GetEngine()->RunScript(script, argv[1], &error);
  const bool scriptPass = runOk || IsAssertionPass(error);
  std::printf("exec     = %s\n", scriptPass ? "ok (assertion marker)" : error.c_str());
  Check(scriptPass, "scene script reached its \"window-ok\" marker");

  const std::shared_ptr<krkr::visual::Scene> scene = krkr::script::GetPrimaryScene();
  Check(scene != nullptr, "the script created a window (primary scene is available)");
  Check(krkr::script::GetWindowCount() == 1, "exactly one window was created");
  if (scene == nullptr) {
    std::printf("\nFAILED (%d failure(s))\n", failures);
    return 1;
  }

  Check(scene->Width() == kSceneWidth && scene->Height() == kSceneHeight,
        "window size reaches the scene");
  Check(host.GetTitle() == "window probe", "window.caption reaches the host window");
  Check(scene->CountNodes() == 3, "scene holds the window root and two layers");

  krkr::visual::DrawList list;
  krkr::visual::SceneBuildStats stats;
  scene->BuildDrawList(list, &stats);

  std::printf("      nodes=%zu emitted=%zu hidden=%zu unsupported=%zu clipped=%zu\n",
              stats.nodesVisited, stats.itemsEmitted, stats.nodesHidden,
              stats.nodesUnsupported, stats.itemsClippedOut);
  Check(stats.nodesVisited == 3, "every visible node was walked");
  Check(stats.itemsEmitted == 2, "one draw item per visible layer");
  Check(stats.nodesHidden == 0 && stats.nodesUnsupported == 0,
        "nothing was skipped for visibility or blend support");
  Check(list.items.size() == 2, "draw list carries two items");
  Check(list.textures.size() == 3, "each item registered its own texture");

  krkr::visual::Surface surface(kSceneWidth, kSceneHeight);
  krkr::visual::RenderStats renderStats;
  Check(krkr::visual::Render(list, surface, &renderStats), "software backend renders the scene");
  Check(renderStats.itemsDrawn == 2, "both items reached the backend");

  // 底图：8x8 四色位图，位于 (4,2)，不透明；clip 只留左上 4x4 的红色象限。
  CheckPixel(surface, 4, 2, 0xFFFF0000u, "opaque layer paints its red quadrant");
  CheckPixel(surface, 7, 5, 0xFFFF0000u, "clip keeps the inside of the layer");
  CheckPixel(surface, 8, 2, kBlack, "clip drops the green quadrant");
  CheckPixel(surface, 4, 6, kBlack, "clip drops the blue quadrant below the clip rect");

  // 子层：相对父层偏移 (8,8) → 绝对 (12,10)，opacity 128 的 alpha 混合。
  // 混合数学来自 vendor/krkrz：opacity 先折进源 alpha（(255*128)>>8 = 127），
  // 再走 alpha_blend 的 `>>8`，所以 128 不透明度落到 126（0x7E），而不是手算的 128。
  CheckPixel(surface, 12, 10, 0xFF7E0000u, "child layer blends red at opacity 128");
  CheckPixel(surface, 16, 10, 0xFF007E00u, "child layer blends green at opacity 128");
  CheckPixel(surface, 12, 14, 0xFF00007Eu, "child layer blends blue at opacity 128");
  CheckPixel(surface, 0, 0, kBlack, "the cleared background is still black");

  if (argc >= 4) {
    Check(surface.SavePpm(argv[3]), "PPM written");
    std::printf("      wrote %s\n", argv[3]);
  }

  krkr::log::Logger::Instance().Flush();
  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
