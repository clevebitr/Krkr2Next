#include "engine/CompatProfile.h"

#include "shim/classes/TjsDebug.h"
#include "shim/classes/TjsLayer.h"
#include "shim/classes/TjsPlugins.h"
#include "shim/classes/TjsScripts.h"
#include "shim/classes/TjsStorages.h"
#include "shim/classes/TjsSystem.h"
#include "shim/classes/TjsWindow.h"

namespace krkr::engine {

Profile MakeKiriKiri2Profile() {
  Profile profile;
  profile.name = "kirikiri2";

  // 参考实现同样把 kirikiriz 置 0：脚本据此判断"没有 Z 扩展"，
  // 从而不进入 linkZ / bres:// 一类分支，而不是让那些调用在运行期才失败。
  profile.preprocessorValues.emplace_back("kirikiriz", 0);
  profile.preprocessorValues.emplace_back("k2compat", 1);

  profile.nativeClasses = {
    &krkr::script::RegisterStoragesClass,
    &krkr::script::RegisterScriptsClass,
    &krkr::script::RegisterSystemClass,
    &krkr::script::RegisterDebugClass,
    &krkr::script::RegisterPluginsClass,
    // 窗口与图层：Window 是场景树的根，Layer 只能挂在已有的 Window 下。
    &krkr::script::RegisterWindowClass,
    &krkr::script::RegisterLayerClass,
  };

  // 脚本前奏留给"必须由脚本层提供、引擎无法实现"的兼容 stub。
  // 目前没有证据表明需要，保持为空；新增 stub 必须附上触发它的游戏与行为依据。
  profile.preludeScript.clear();
  return profile;
}

} // namespace krkr::engine
