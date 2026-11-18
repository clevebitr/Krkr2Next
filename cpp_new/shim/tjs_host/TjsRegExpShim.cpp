// 垫层：TJS 的 RegExp 类依赖 oniguruma（上游 vendored 在 krkrz/external/onig，
// 本地克隆中为空子模块）。当前版本不提供正则支持：
//   - TJSCreateRegExpClass() 返回 nullptr：脚本里访问 RegExp 会得到"未定义"错误，
//     而不是崩溃或静默失败；
//   - TJSReleaseRegex() 是空实现（上游在 TJS_NO_REGEXP 下仍会调用它）。
// 记录在 todo.md 技术债，等真需要正则时再把 oniguruma 作为第三方源码引入。

#include "tjs/tjsNative.h"

namespace TJS {

iTJSDispatch2 *TJSCreateRegExpClass() { return nullptr; }

void TJSReleaseRegex() {}

} // namespace TJS
