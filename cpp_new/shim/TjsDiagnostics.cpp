#include "shim/TjsDiagnostics.h"

#include <cstddef>
#include <string>

#include "core/log/Logger.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"

namespace krkr::script {

namespace {

struct MessageEntry {
  const tjs_char *name;
  const tjs_char *text;
};

const MessageEntry kBuiltinMessages[] = {
#include "shim/tjs_host/generated/tjsMessageMap.inc"
};

constexpr const char *kEmptyMessage = "<no message>";

} // namespace

void InstallTjsMessageMap() {
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;

  std::size_t assigned = 0;
  for (const MessageEntry &entry : kBuiltinMessages) {
    if (TJS::TJSAssignMessage(entry.name, entry.text)) {
      ++assigned;
    }
  }
  krkr::log::Debug("tjs: message map installed (", assigned, "/",
                   sizeof(kBuiltinMessages) / sizeof(kBuiltinMessages[0]), ")");
}

std::string DescribeScriptException(const TJS::eTJS &error, const std::string &context) {
  std::string text;
  if (!context.empty()) {
    text += context;
    text += ": ";
  }

  std::string message = Utf8FromWide(error.GetMessage().c_str());
  text += message.empty() ? std::string(kEmptyMessage) : message;

  // eTJSScriptError 才带源码位置；纯 eTJSError（含桥接层主动抛出的那些）没有。
  const auto *located = dynamic_cast<const TJS::eTJSScriptError *>(&error);
  if (located != nullptr) {
    const std::string block = Utf8FromWide(located->GetBlockName());
    text += " [script=";
    text += block.empty() ? std::string("<anonymous>") : block;
    text += " line=";
    text += std::to_string(located->GetSourceLine());
    const std::string trace = Utf8FromWide(located->GetTrace().c_str());
    if (!trace.empty()) {
      text += " trace=";
      text += trace;
    }
    text += "]";
  }

  return text;
}

void ThrowScriptException(const std::string &context, const std::string &detail) {
  std::string text = context.empty() ? detail : context + ": " + detail;
  throw TJS::eTJSError(WideFromUtf8(text).c_str());
}

} // namespace krkr::script
