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
#include "tjs/tjs.h"
#include "tjs/tjsError.h"

namespace krkr::script {

namespace {

// UTF-8 与 tjs_char 的转换。tjs_char 的宽度由平台决定（Windows 为 2 字节），
// 该差异仅在此处处理。
std::wstring Utf8ToTjsString(const std::string &utf8) {
  std::wstring out;
  out.reserve(utf8.size());
  std::size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    std::uint32_t cp = lead;
    std::size_t extra = 0;
    if (lead >= 0xf0) {
      cp = lead & 0x07;
      extra = 3;
    } else if (lead >= 0xe0) {
      cp = lead & 0x0f;
      extra = 2;
    } else if (lead >= 0xc0) {
      cp = lead & 0x1f;
      extra = 1;
    }
    if (i + extra >= utf8.size()) {
      break;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3f);
    }
    i += extra + 1;
    if (sizeof(wchar_t) == 2 && cp > 0xffff) {
      cp -= 0x10000;
      out.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10)));
      out.push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff)));
    } else {
      out.push_back(static_cast<wchar_t>(cp));
    }
  }
  return out;
}

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

std::string ToUtf8(const ttstr &text) {
  return TjsStringToUtf8(text.c_str());
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

// KiriKiri 的 .tjs 为 UTF-16LE（带 FF FE BOM）。入口统一转成 UTF-8，
// 使后续流程只处理单一编码。
std::string DecodeScriptSource(const std::string &bytes) {
  if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
      static_cast<unsigned char>(bytes[1]) == 0xfe) {
    std::string out;
    out.reserve((bytes.size() - 2) / 2);
    std::size_t i = 2;
    while (i + 1 < bytes.size()) {
      const std::uint32_t unit = static_cast<unsigned char>(bytes[i]) |
                                 (static_cast<std::uint32_t>(
                                      static_cast<unsigned char>(bytes[i + 1]))
                                  << 8);
      i += 2;
      if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < bytes.size()) {
        const std::uint32_t low = static_cast<unsigned char>(bytes[i]) |
                                  (static_cast<std::uint32_t>(
                                       static_cast<unsigned char>(bytes[i + 1]))
                                   << 8);
        if (low >= 0xdc00 && low <= 0xdfff) {
          i += 2;
          AppendUtf8(out, 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
          continue;
        }
      }
      AppendUtf8(out, unit);
    }
    return out;
  }
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf) {
    return bytes.substr(3);
  }
  return bytes;
}

} // namespace

class Engine::Impl {
  public:
    TJS::tTJS *tjs = nullptr;
    ConsoleSink console;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {
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
  const std::string source = DecodeScriptSource(std::string(sourceUtf8));
  const std::wstring wide = Utf8ToTjsString(source);
  const std::wstring name = Utf8ToTjsString(scriptName == nullptr ? "script" : scriptName);

  impl_->console.hadError = false;
  if (errorOutUtf8 != nullptr) {
    errorOutUtf8->clear();
  }

  try {
    impl_->tjs->ExecScript(wide.c_str(), nullptr, nullptr, name.c_str());
  } catch (const TJS::eTJSScriptException &e) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = ToUtf8(e.GetMessage());
    }
    return false;
  } catch (const TJS::eTJSScriptError &e) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = ToUtf8(e.GetMessage());
    }
    return false;
  } catch (const TJS::eTJS &e) {
    if (errorOutUtf8 != nullptr) {
      *errorOutUtf8 = ToUtf8(e.GetMessage());
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
