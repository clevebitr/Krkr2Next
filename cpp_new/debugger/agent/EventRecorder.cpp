#include "debugger/agent/EventRecorder.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace krkr::debugger {

namespace {

// JSON 字符串转义。事件里可能出现路径与脚本消息（含引号、换行、非 ASCII），
// 非 ASCII 字节按原字节透传，JSON 允许 UTF-8。
void AppendEscaped(std::string &out, const std::string &text) {
  for (const char ch : text) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(ch));
          out += buffer;
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
}

std::string FormatEvent(const krkr::debug::DebugEvent &event) {
  std::string line = "{\"ev\":\"";
  line += krkr::debug::EventKindName(event.kind);
  line += "\",\"frame\":";
  line += std::to_string(event.frame);
  line += ",\"ok\":";
  line += event.ok ? "true" : "false";
  line += ",\"line\":";
  line += std::to_string(event.line);
  if (event.size >= 0) {
    line += ",\"size\":";
    line += std::to_string(event.size);
  }
  line += ",\"channel\":\"";
  AppendEscaped(line, event.channel);
  line += "\",\"name\":\"";
  AppendEscaped(line, event.name);
  line += "\",\"detail\":\"";
  AppendEscaped(line, event.detail);
  line += "\"}\n";
  return line;
}

} // namespace

EventRecorder::~EventRecorder() { Close(); }

std::unique_ptr<EventRecorder> EventRecorder::FromEnvironment() {
  const char *path = std::getenv("KRKR_DEBUG_EVENTS");
  if (path == nullptr || path[0] == '\0') {
    return nullptr;
  }
  auto recorder = std::make_unique<EventRecorder>();
  if (!recorder->Open(path)) {
    std::fprintf(stderr, "debug events disabled: %s\n", recorder->GetLastErrorText());
    return nullptr;
  }
  std::fprintf(stderr, "[debug] events -> %s\n", path);
  return recorder;
}

bool EventRecorder::Open(const std::string &pathUtf8) {
  Close();
  file_ = std::fopen(pathUtf8.c_str(), "wb");
  if (file_ == nullptr) {
    lastError_ = "cannot open event file: " + pathUtf8;
    return false;
  }
  lastError_.clear();
  return true;
}

void EventRecorder::Close() {
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

void EventRecorder::OnDebugEvent(const krkr::debug::DebugEvent &event) {
  ++count_;
  const std::string line = FormatEvent(event);
  if (file_ != nullptr) {
    std::fwrite(line.data(), 1, line.size(), file_);
    std::fflush(file_);
  }
  if (echo_) {
    std::fputs(line.c_str(), stderr);
  }
}

} // namespace krkr::debugger
