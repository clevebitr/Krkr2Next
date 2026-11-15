#include "tjsCommHead.h"

#include "krkrMsgImpl.h"
#include "krkrMsgIntf.h"

namespace krkr::base {
  void krkrGetVersion() {
    static bool DoGet = true;
    if (DoGet) {
        DoGet = false;

        krkrVerisonMajor   = 2;
        krkrVersionMinor   = 32;
        krkrVersionRelease = 2;
        krkrVersionBuild   = 426;
    }
  }

  const tjs_char *krkrCompileDare;
  const tjs_char *krkrCompileTime;

  ttstr krkrReadAboutStringFromResource() {
    return TJS_W("Kirikiri2 Runtime Core version %1(TJS verison %2)");
  }
} // namespace krkr::base
