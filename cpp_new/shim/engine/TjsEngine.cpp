#include "shim/engine/TjsEngine.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// 垫层：平台与工具链差异在此处理，core/tjs2/ 保持上游原样。

// 1) Windows（含 MinGW）：tjsCommHead.h 会 include windows.h，而 winuser.h 把
//    GetMessage 定义成 GetMessageA 宏，撞到 TJS 自己的 eTJS::GetMessage()。
//    先自己把 windows.h 拉进来并清掉这些宏；tjsCommHead 里的 #include <windows.h>
//    会因为 include guard 不再重新定义它们。
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef GetMessage
#undef GetObject
#undef PlaySound
#undef min
#undef max
#endif

#include "core/log/Logger.h"
#include "shim/TjsDiagnostics.h"
#include "shim/engine/ScriptLoader.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsError.h"

namespace krkr::script {

namespace {

void AppendUtf8(std::string &out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

std::string TjsStringToUtf8(const tjs_char *text) {
  std::string out;
  if (text == nullptr) {
    return out;
  }
  for (const tjs_char *p = text; *p != 0; ++p) {
    std::uint32_t cp = static_cast<std::uint32_t>(*p);
    if (sizeof(wchar_t) == 2 && cp >= 0xd800 && cp <= 0xdbff && p[1] != 0) {
      const std::uint32_t low = static_cast<std::uint32_t>(p[1]);
      if (low >= 0xdc00 && low <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        ++p;
      }
    }
    AppendUtf8(out, cp);
  }
  return out;
}

// tjs2 通过该接口回传脚本输出与异常文本
class ConsoleSink final : public TJS::iTJSConsoleOutput {
  public:
    void Print(const tjs_char *msg) override { Emit(msg); }
    void ExceptionPrint(const tjs_char *msg) override {
      hadError = true;
      lastMessage = TjsStringToUtf8(msg);
      krkr::log::Error("tjs: ", lastMessage);
    }

    bool hadError = false;
    std::string lastMessage;
    void (*printer)(const char *lineUtf8, void *user) = nullptr;
    void *user = nullptr;

  private:
    void Emit(const tjs_char *msg) {
      lastMessage = TjsStringToUtf8(msg);
      if (printer != nullptr) {
        printer(lastMessage.c_str(), user);
      } else {
        std::fprintf(stdout, "%s\n", lastMessage.c_str());
        std::fflush(stdout);
      }
    }
};

} // namespace

class Engine::Impl {
  public:
    TJS::tTJS *tjs = nullptr;
    ConsoleSink console;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {
  InstallTjsMessageMap();
  impl_->tjs = new TJS::tTJS();
  impl_->tjs->SetConsoleOutput(&impl_->console);
}

Engine::~Engine() {
  if (impl_->tjs != nullptr) {
    impl_->tjs->SetConsoleOutput(nullptr);
    impl_->tjs->Shutdown();
    impl_->tjs->Release();
    impl_->tjs = nullptr;
  }
}

bool Engine::IsReady() const { return impl_->tjs != nullptr; }

void *Engine::GetGlobalObjectRaw() {
  return impl_->tjs == nullptr ? nullptr : impl_->tjs->GetGlobalNoAddRef();
}

void *Engine::GetVMScriptEngineRaw() { return impl_->tjs; }

void Engine::SetPrinter(void (*printer)(const char *lineUtf8, void *user), void *user) {
  impl_->console.printer = printer;
  impl_->console.user = user;
}

bool Engine::RunScript(std::string_view sourceUtf8, const char *scriptName,
                       std::string *errorOutUtf8) {
  const std::string name = scriptName == nullptr ? "script" : scriptName;

  impl_->console.hadError = false;
  if (errorOutUtf8 != nullptr) {
    errorOutUtf8->clear();
  }

  try {
    RunScriptBytes(*this, std::string(sourceUtf8), name, name, ScriptMode::Execute, nullptr,
                   nullptr);
  } catch (const TJS::eTJS &e) {
    // 脚本名在此已知，异常自带块名与行号；两者一起写进错误文本，
    // 否则上层只能看到一条没有定位信息的消息。
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = DescribeScriptException(e, name);
    }
    return false;
  } catch (...) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = "unknown TJS exception";
    }
    return false;
  }
  return true;
}

bool Engine::RunStorageScript(const std::string &nameUtf8, std::string *errorOutUtf8,
                              bool evaluate) {
  if (errorOutUtf8 != nullptr) {
    errorOutUtf8->clear();
  }
  try {
    RunScriptFromStorage(*this, nameUtf8, evaluate ? ScriptMode::Evaluate : ScriptMode::Execute,
                         "Engine.runStorage", nullptr, nullptr);
  } catch (const TJS::eTJS &e) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = DescribeScriptException(e, nameUtf8);
    }
    return false;
  } catch (...) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = "unknown TJS exception while running " + nameUtf8;
    }
    return false;
  }
  return true;
}

void Engine::SetPreprocessorValue(const std::string &nameUtf8, int value) {
  if (impl_->tjs == nullptr) {
    return;
  }
  impl_->tjs->SetPPValue(WideFromUtf8(nameUtf8).c_str(), value);
}

bool Engine::RunScriptFile(const char *pathUtf8, std::string *errorOutUtf8) {
  std::ifstream file(pathUtf8, std::ios::binary);
  if (!file.is_open()) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = std::string("cannot open script file: ") + pathUtf8;
    }
    return false;
  }
  const std::string bytes{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
  return RunScript(bytes, pathUtf8, errorOutUtf8);
}

} // namespace krkr::script
