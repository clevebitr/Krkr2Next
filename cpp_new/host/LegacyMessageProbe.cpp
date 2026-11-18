// 旧渲染层消息表的断言（R1-2 的第一块:seam 必须先可验证，再谈像素）。
//
//   legacy_message_probe
//
// 断言三件事：
//   1. 生成的消息表非空（条数与上游 MsgIntfInc.h 的 152 条对齐以上）；
//   2. 上游语义的 `%1` / `%2` / `%%` 替换与 `TVPFormatMessage` 一致；
//   3. `TVPThrowExceptionMessage` 抛出的是带文本的 TJS 异常（不是空消息）。

#include <cstdio>
#include <string>

#include "vendor/krkrz/msg/MsgIntf.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const char *what) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("FAIL  %s\n", what);
  }
}

std::string Narrow(const tjs_char *text) {
  std::string out;
  for (const tjs_char *cursor = text; cursor != nullptr && *cursor != 0; ++cursor) {
    out.push_back(*cursor < 0x80 ? static_cast<char>(*cursor) : '?');
  }
  return out;
}

} // namespace

int main() {
  std::printf("legacy_message_probe: %d message(s) generated\n", LEGACY_MESSAGE_COUNT);
  Check(LEGACY_MESSAGE_COUNT >= 152, "message catalog covers MsgIntfInc.h entries");

  const tjs_char *cannotOpen = TVPCannotOpenStorage;
  Check(cannotOpen != nullptr && cannotOpen[0] != 0, "TVPCannotOpenStorage has text");
  const tjs_char *internal = TVPInternalError;
  Check(internal != nullptr && internal[0] != 0, "TVPInternalError has text");

  const ttstr formatted = TVPFormatMessage(TVPCannotOpenStorage, ttstr("data.xp3"));
  const std::string formattedText = Narrow(formatted.c_str());
  Check(formattedText.find("data.xp3") != std::string::npos,
        "TVPFormatMessage substitutes %1");
  Check(formattedText.find("%1") == std::string::npos,
        "TVPFormatMessage removes the %1 placeholder");

  const ttstr doubled = TVPFormatMessage(TJS_W("100%% and 1%%"), ttstr("x"));
  Check(doubled == ttstr(TJS_W("100% and 1%")), "TVPFormatMessage collapses %%");

  // 抛出的异常必须带文本：这是"诊断可用"的最低要求（对应技术债 #40 的同类问题）。
  bool caught = false;
  std::string thrown;
  try {
    TVPThrowExceptionMessage(TVPInternalError, ttstr("probe"), 7);
  } catch (const TJS::eTJS &error) {
    caught = true;
    thrown = Narrow(error.GetMessage().c_str());
  } catch (...) {
    caught = true;
    thrown = "<non-TJS exception>";
  }
  Check(caught, "TVPThrowExceptionMessage throws");
  Check(thrown.find("probe") != std::string::npos && thrown.find("7") != std::string::npos,
        "thrown message contains the formatted arguments");

  std::printf("%s (%d failure(s) / %d check(s))\n", failures == 0 ? "ALL PASS" : "FAILED",
              failures, checks);
  return failures == 0 ? 0 : 1;
}
