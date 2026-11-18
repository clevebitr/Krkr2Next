// 垫层：TJS2 期望「宿主」提供的一组符号。
//
// 上游将这些符号留给宿主程序实现，单独编译 TJS 时必须提供。
// 这里**不改 tjs2 一行代码**，只提供：
//   1. 调试设施的空实现（对象哈希表 / 栈追溯器）：TJS2 用它们做内存泄漏与调用栈诊断，
//      当前不使用，全部为空实现且指针保持 nullptr。
//   2. 窄字符转换：上游是 SJIS（日文 Windows 代码页），在 Android 上应当用 UTF-8。
//      此处以 UTF-8 实现，语义等价且跨平台一致。
//
// 符号声明全部来自 tjs2 自己的头（tjsDebug.h / tjsConfig.cpp 的 extern 声明），
// 所以签名天然对齐，不需要猜。

#include "tjs/tjs.h"
#include "tjs/tjsDebug.h"
#include "tjs/tjsError.h"

extern tjs_size SJISToUnicodeString(const char *in, tjs_char *out);
extern tjs_size SJISToUnicodeString(const char *in, tjs_char *out, tjs_size limit);
extern bool IsSJISLeadByte(tjs_nchar b);
extern tjs_uint UnicodeToSJIS(tjs_char in);
extern tjs_size UnicodeToSJISString(const tjs_char *in, tjs_nchar *out);
extern tjs_size UnicodeToSJISString(const tjs_char *in, tjs_nchar *out, tjs_size limit);

namespace TJS {

// 对象哈希表与栈追溯器：空实现
tTJSObjectHashMap *TJSObjectHashMap = nullptr;
tTJSBinaryStream *TJSObjectHashMapLog = nullptr;
tTJSStackTracer *TJSStackTracer = nullptr;

void TJSAddRefObjectHashMap() {}
void TJSReleaseObjectHashMap() {}
void TJSAddObjectHashRecord(void * /*object*/) {}
void TJSRemoveObjectHashRecord(void * /*object*/) {}
void TJSObjectHashSetType(void * /*object*/, const ttstr & /*type*/) {}
void TJSSetObjectHashFlag(void * /*object*/, tjs_uint32 /*flags_to_change*/,
                          tjs_uint32 /*bits*/) {}
void TJSWarnIfObjectIsDeleting(iTJSConsoleOutput * /*output*/, void * /*object*/) {}
ttstr TJSGetObjectTypeInfo(void * /*object*/) { return ttstr(); }

void TJSAddRefStackTracer() {}
void TJSReleaseStackTracer() {}
void TJSStackTracerPush(tTJSInterCodeContext * /*context*/, bool /*in_try*/) {}
void TJSStackTracerSetCodePointer(const tjs_int32 * /*codebase*/,
                                  tjs_int32 *const * /*codeptr*/) {}
void TJSStackTracerPop() {}

} // namespace TJS

// 窄字符转换：UTF-8（上游实现为 SJIS）
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

std::string Utf8FromWide(const tjs_char *in, tjs_size limit, bool *overflow) {
    std::string out;
    if (in == nullptr) {
        return out;
    }
    for (tjs_size i = 0; in[i] != 0; ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(in[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xd800 && cp <= 0xdbff && in[i + 1] != 0) {
            const std::uint32_t low = static_cast<std::uint32_t>(in[i + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                ++i;
            }
        }
        std::string piece;
        AppendUtf8(piece, cp);
        if (limit != 0 && out.size() + piece.size() > limit) {
            if (overflow != nullptr) {
                *overflow = true;
            }
            break;
        }
        out += piece;
    }
    return out;
}

std::wstring WideFromUtf8(const char *in, tjs_size limit) {
    std::wstring out;
    if (in == nullptr) {
        return out;
    }
    tjs_size produced = 0;
    std::size_t i = 0;
    while (in[i] != 0 && (limit == 0 || produced < limit)) {
        const unsigned char lead = static_cast<unsigned char>(in[i]);
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
        for (std::size_t k = 1; k <= extra; ++k) {
            if (in[i + k] == 0) {
                i += k;
                break;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3f);
        }
        if (in[i + extra] == 0) {
            break;
        }
        i += extra + 1;
        if (sizeof(wchar_t) == 2 && cp > 0xffff) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff)));
            produced += 2;
        } else {
            out.push_back(static_cast<wchar_t>(cp));
            ++produced;
        }
    }
    return out;
}

} // namespace

tjs_size SJISToUnicodeString(const char *in, tjs_char *out) {
    const std::wstring wide = WideFromUtf8(in, 0);
    if (out != nullptr) {
        for (std::size_t i = 0; i < wide.size(); ++i) {
            out[i] = wide[i];
        }
    }
    return wide.size();
}

tjs_size SJISToUnicodeString(const char *in, tjs_char *out, tjs_size limit) {
    const std::wstring wide = WideFromUtf8(in, limit);
    if (out != nullptr) {
        for (std::size_t i = 0; i < wide.size(); ++i) {
            out[i] = wide[i];
        }
    }
    return wide.size();
}

bool IsSJISLeadByte(tjs_nchar b) {
    // UTF-8：>= 0x80 的字节都是多字节序列的一部分
    return (static_cast<unsigned char>(b) & 0x80) != 0;
}

tjs_uint UnicodeToSJIS(tjs_char in) {
    // 单字符版本：非 ASCII 直接返回 '?'（窄字符单字节装不下多字节序列）
    const unsigned char ch = static_cast<unsigned char>(in);
    return ch < 0x80 ? ch : static_cast<tjs_uint>('?');
}

tjs_size UnicodeToSJISString(const tjs_char *in, tjs_nchar *out) {
    const std::string utf8 = Utf8FromWide(in, 0, nullptr);
    if (out != nullptr) {
        for (std::size_t i = 0; i < utf8.size(); ++i) {
            out[i] = utf8[i];
        }
    }
    return utf8.size();
}

tjs_size UnicodeToSJISString(const tjs_char *in, tjs_nchar *out, tjs_size limit) {
    bool overflow = false;
    const std::string utf8 = Utf8FromWide(in, limit, &overflow);
    if (out != nullptr) {
        const std::size_t count = utf8.size() < limit ? utf8.size() : limit;
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = utf8[i];
        }
    }
    return utf8.size();
}
