#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "tjs/tjs.h"
#include "tjs/tjsError.h"

// TJS2 测试台：验证上游实现可直接编译并执行脚本。
//
//   tjs_probe <script.tjs>
//
// 脚本可以是 UTF-16LE（KiriKiri 的 .tjs 默认，带 FF FE BOM）或 UTF-8（带/不带 BOM）。
// 只做三件事：初始化 TJS2、执行脚本、把 console 输出和异常打到 stdout/stderr。

namespace {

// 打给 TJS 的 console（System.inform / 异常都会走到这里）
class Console final : public TJS::iTJSConsoleOutput {
  public:
    void Print(const tjs_char *msg) override { PrintLine("[tjs] ", msg); }

    void ExceptionPrint(const tjs_char *msg) override {
        PrintLine("[tjs:exception] ", msg);
        hadException = true;
    }

    bool hadException = false;

    // tjs_char 转 UTF-8，仅处理 BMP；脚本错误信息不涉及增补平面字符。
    static std::string ToUtf8(const tjs_char *text) {
        std::string out;
        for (const tjs_char *p = text; p != nullptr && *p != 0; ++p) {
            const std::uint32_t cp = *p;
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            } else {
                out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
        }
        return out;
    }

    static std::string Utf16ToUtf8(const std::u16string &text) {
        return ToUtf8(reinterpret_cast<const tjs_char *>(text.c_str()));
    }

  private:
    static void PrintLine(const char *prefix, const tjs_char *msg) {
        std::fprintf(stdout, "%s%s\n", prefix, ToUtf8(msg).c_str());
        std::fflush(stdout);
    }
};

bool ReadWholeFile(const char *path, std::string &out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// UTF-8 转 UTF-16，含代理对
void AppendUtf16(std::u16string &out, std::uint32_t cp) {
    if (cp <= 0xffff) {
        out.push_back(static_cast<char16_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<char16_t>(0xd800 + (cp >> 10)));
        out.push_back(static_cast<char16_t>(0xdc00 + (cp & 0x3ff)));
    }
}

std::u16string Utf8ToUtf16(const std::string &utf8) {
    std::u16string out;
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
        AppendUtf16(out, cp);
    }
    return out;
}

// 解出脚本文本（UTF-16LE 或 UTF-8）
std::u16string DecodeScript(const std::string &bytes) {
    const auto *raw = reinterpret_cast<const unsigned char *>(bytes.data());
    if (bytes.size() >= 2 && raw[0] == 0xff && raw[1] == 0xfe) {
        std::u16string text;
        text.resize((bytes.size() - 2) / 2);
        std::memcpy(text.data(), bytes.data() + 2, text.size() * 2);
        return text;
    }
    if (bytes.size() >= 3 && raw[0] == 0xef && raw[1] == 0xbb && raw[2] == 0xbf) {
        return Utf8ToUtf16(bytes.substr(3));
    }
    return Utf8ToUtf16(bytes);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: tjs_probe <script.tjs>\n");
        return 2;
    }

    std::string bytes;
    if (!ReadWholeFile(argv[1], bytes)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    const std::u16string script = DecodeScript(bytes);
    std::printf("script   = %s\n", argv[1]);
    std::printf("bytes    = %zu (UTF-16 单元 %zu)\n", bytes.size(), script.size());

    Console console;
    auto *tjs = new TJS::tTJS();
    tjs->SetConsoleOutput(&console);

    try {
        tjs->ExecScript(reinterpret_cast<const tjs_char *>(script.c_str()), nullptr, nullptr,
                        reinterpret_cast<const tjs_char *>(u"probe"));
        std::printf("exec     = ok\n");
    } catch (const TJS::eTJSScriptException &e) {
        std::printf("exec     = script exception: %s\n", e.GetMessage().AsStdString().c_str());
    } catch (const TJS::eTJSScriptError &e) {
        std::printf("exec     = script error: %s\n", e.GetMessage().AsStdString().c_str());
    } catch (...) {
        std::printf("exec     = unknown exception\n");
    }

    tjs->SetConsoleOutput(nullptr);
    tjs->Shutdown();
    tjs->Release();

    return console.hadException ? 1 : 0;
}
