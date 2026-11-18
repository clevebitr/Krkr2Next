// 旧渲染层的消息与异常文本实现（替代上游 msg/MsgIntf.cpp 的 Win32 资源路径）。
//
// 与上游的差异（见 vendor/krkrz/VENDOR.md §4）：
//   - 消息文本来自 `generated/messages.inc`（由 tools/gen_legacy_messages.py 从
//     上游 Messages.xlsx 生成），不依赖 Win32 字符串资源与 Perl/Excel；
//   - `TVPFormatMessage` 用 std::basic_string 直接拼装（上游是裸缓冲区 + stride 指针），
//     `%1`/`%2`/`%%` 的语义保持完全一致；
//   - 版本号只保留上游导出的四个整数，`TVPGetVersion` 是空实现（Android 不读 VERSIONINFO）。

#include "tjs.h"
#include "tjsError.h"
#include "tjsMessage.h"

#include "MsgIntf.h"

#include <string>

namespace {

using WideString = std::basic_string<tjs_char>;

// 与上游一致：`%%` → `%`，`%1` → p1，`%2` → p2；其余原样。
WideString Format(const tjs_char *message, const ttstr *first, const ttstr *second) {
  WideString out;
  if (message == nullptr) {
    return out;
  }
  for (const tjs_char *cursor = message; *cursor != 0; ++cursor) {
    if (*cursor != TJS_W('%')) {
      out.push_back(*cursor);
      continue;
    }
    if (cursor[1] == TJS_W('%')) {
      out.push_back(TJS_W('%'));
      ++cursor;
      continue;
    }
    if (cursor[1] == TJS_W('1') && first != nullptr) {
      out.append(first->c_str());
      ++cursor;
      continue;
    }
    if (cursor[1] == TJS_W('2') && second != nullptr) {
      out.append(second->c_str());
      ++cursor;
      continue;
    }
    out.push_back(*cursor);
  }
  return out;
}

} // namespace

//---------------------------------------------------------------------------
// 消息持有者：文案在这里一次性构造，之后运行期只读。
//---------------------------------------------------------------------------
#define LEGACY_MESSAGE(name, text) tTJSMessageHolder name(TJS_W(#name), text);
#include "generated/messages.inc"
#undef LEGACY_MESSAGE

//---------------------------------------------------------------------------
// TVPFormatMessage
//---------------------------------------------------------------------------
ttstr TVPFormatMessage(const tjs_char *msg, const ttstr &p1) {
  return ttstr(Format(msg, &p1, nullptr));
}

ttstr TVPFormatMessage(const tjs_char *msg, const ttstr &p1, const ttstr &p2) {
  return ttstr(Format(msg, &p1, &p2));
}

//---------------------------------------------------------------------------
// TVPThrowExceptionMessage
//---------------------------------------------------------------------------
void TVPThrowExceptionMessage(const tjs_char *msg) { throw TJS::eTJSError(msg); }

void TVPThrowExceptionMessage(const tjs_char *msg, const ttstr &p1) {
  throw TJS::eTJSError(TVPFormatMessage(msg, p1));
}

void TVPThrowExceptionMessage(const tjs_char *msg, const ttstr &p1, const ttstr &p2) {
  throw TJS::eTJSError(TVPFormatMessage(msg, p1, p2));
}

void TVPThrowExceptionMessage(const tjs_char *msg, const ttstr &p1, tjs_int num) {
  throw TJS::eTJSError(TVPFormatMessage(msg, p1, ttstr(num)));
}

//---------------------------------------------------------------------------
// 版本号（上游由 VERSIONINFO 资源填写；这里给编译期常量，供 System/W 相关代码读取）
//---------------------------------------------------------------------------
tjs_int TVPVersionMajor = 1;
tjs_int TVPVersionMinor = 4;
tjs_int TVPVersionRelease = 0;
tjs_int TVPVersionBuild = 0;

void TVPGetVersion() {}
