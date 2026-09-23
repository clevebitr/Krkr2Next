//---------------------------------------------------------------------------
// FONTCHANGER.dll 兼容（汉化补丁换字体）
//---------------------------------------------------------------------------
// 为什么需要：汉化/机翻补丁普遍带一套 "FontChanger"（`FONTCHANGER.dll` +
// `hook.ini` + 自带字体文件）——游戏原字体没有中文字形，补丁靠它把**所有**文字
// 换成自带字体（典型 `FONTNAME=ShiraYukiNoa` / `FONTFILENAME=ShiraYukiNoa.otf`）。
// 该插件是原生 Win32 DLL（hook CreateFontA / CreateFontIndirectA），移动端加载
// 不了，于是字体没被换 → 游戏里中文全是实心方块（真机 2026-09-23 11:46 的
// チート緊縛術：hook.ini 里 CHANGEFONT=1，而日志只有 `FONTCHANGER.dll Failed`，
// 字体表里只有系统字体）。
//
// 做法：**不 hook Win32，只复现"效果"**：
//   1. 读游戏目录 `hook.ini` 的 `[FONT]` 段；
//   2. `CHANGEFONT=1` 且 `FONTFILENAME` 存在 → `TVPEnumFontsProc()` 把该字体文件
//      注册进字体表（否则 `FONTNAME` 只是一个"不存在"的字面名）；
//   3. 把 `FONTNAME` 设为**强制字面**（`TVPSetForcedFontName`）：此后
//      `FontSystem::GetBeingFont()` 一律返回它，等价于补丁 hook 掉每次建字体的效果。
//
// 不复现的部分（按需再补，都只影响观感）：
//   - `cWeight`（如 600）：Win32 下传给 CreateFont 的粗细。这里不逐处拦截脚本设的
//     字体标志，故不加粗；若某作必须靠它区分标题/正文，再单独处理。
//   - `HeightScaleFactor` / `WidthScaleFactor`：非 100 时才需要缩放度量。
//   - `[ConditCHANGEFONT]`：条件换字体（满足条件才换）。目前按"无条件换"处理，
//     与 `ConditCHANGEFONT=0` 的常见配置一致。
//---------------------------------------------------------------------------
#include "ncbind.hpp"

#include <cctype>
#include <string>

#include <spdlog/spdlog.h>

#include "FontImpl.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"

namespace {

    std::string TrimAscii(std::string s) {
        auto space = [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };
        while(!s.empty() && space(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while(!s.empty() && space(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    }

    std::string LowerAscii(std::string s) {
        for(auto &c : s) {
            if(c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        }
        return s;
    }

    /** `hook.ini` 的 `[FONT]` 段里我们真正需要的三项。 */
    struct FontChangerConfig {
        bool changeFont = false;
        std::string fontName;
        std::string fontFileName;
    };

    /**
     * 解析 `hook.ini` 的 `[FONT]` 段。
     *
     * 按**字节**解析而不是按 Shift_JIS 解码：文件本身可能是 Shift_JIS，但
     * `CHANGEFONT` / `FONTNAME` / `FONTFILENAME` 的键与值都是纯 ASCII，按字节
     * 处理既够用又不会踩编码转换的坑（`FontChanger` 的字体名也是 ASCII）。
     */
    FontChangerConfig ParseHookIni(const std::string &text) {
        FontChangerConfig cfg;
        std::string section;
        size_t pos = 0;
        while(pos <= text.size()) {
            const size_t nl = text.find('\n', pos);
            std::string line = text.substr(
                pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

            line = TrimAscii(line);
            if(line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            if(line.front() == '[' && line.back() == ']') {
                section =
                    LowerAscii(TrimAscii(line.substr(1, line.size() - 2)));
                continue;
            }
            if(section != "font")
                continue;

            const size_t eq = line.find('=');
            if(eq == std::string::npos)
                continue;
            const std::string key = LowerAscii(TrimAscii(line.substr(0, eq)));
            std::string value = TrimAscii(line.substr(eq + 1));
            if(value.size() >= 2 &&
               ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }

            if(key == "changefont")
                cfg.changeFont = (value == "1");
            else if(key == "fontname")
                cfg.fontName = value;
            else if(key == "fontfilename")
                cfg.fontFileName = value;
        }
        return cfg;
    }

    /** 读整个文件（只用于 ini/字体这种小文件；上限 1MiB 防止误读大文件）。 */
    std::string ReadAllBytes(const ttstr &path) {
        tTJSBinaryStream *stream = TVPCreateStream(path, TJS_BS_READ);
        if(!stream)
            return {};
        const tjs_uint64 size = stream->GetSize();
        std::string out;
        if(size > 0 && size < (1u << 20)) {
            out.resize(static_cast<size_t>(size));
            stream->ReadBuffer(&out[0], static_cast<tjs_uint>(size));
        }
        delete stream;
        return out;
    }

    /** 先按存储解析（散装文件 / auto-path / 档案内条目），失败再按应用目录拼一次。 */
    ttstr LocateFile(const ttstr &name) {
        ttstr placed = TVPGetPlacedPath(name);
        if(!placed.IsEmpty())
            return placed;
        return TVPGetPlacedPath(TVPGetAppPath() + name);
    }

    void FontChangerInit() {
        const ttstr hookPath = LocateFile(TJS_W("hook.ini"));
        if(hookPath.IsEmpty()) {
            spdlog::info("fontchanger: 未找到 hook.ini，按未配置处理");
            return;
        }
        const std::string text = ReadAllBytes(hookPath);
        if(text.empty()) {
            spdlog::warn("fontchanger: hook.ini 为空或读取失败（{}）",
                         hookPath.AsStdString());
            return;
        }

        const FontChangerConfig cfg = ParseHookIni(text);
        if(!cfg.changeFont) {
            spdlog::info("fontchanger: hook.ini [FONT] CHANGEFONT != 1，不改字体");
            return;
        }
        if(cfg.fontName.empty()) {
            spdlog::warn("fontchanger: CHANGEFONT=1 但 FONTNAME 为空，忽略");
            return;
        }

        int faces = 0;
        if(!cfg.fontFileName.empty()) {
            const ttstr fontPath =
                LocateFile(ttstr(cfg.fontFileName.c_str()));
            if(fontPath.IsEmpty()) {
                spdlog::warn("fontchanger: FONTFILENAME '{}' 不存在"
                             "（补丁的字体文件缺失？）",
                             cfg.fontFileName);
            } else {
                faces = TVPEnumFontsProc(fontPath);
                if(faces <= 0)
                    spdlog::warn("fontchanger: 字体文件 '{}' 没注册出任何字面"
                                 "（文件损坏或不是字体？）",
                                 cfg.fontFileName);
            }
        }

        TVPSetForcedFontName(ttstr(cfg.fontName.c_str()));
        spdlog::info("fontchanger: hook.ini CHANGEFONT=1 -> 强制字面 '{}'"
                     "（字体文件 '{}' 注册 {} 个字面）",
                     cfg.fontName, cfg.fontFileName, faces);
    }

    static ncbCallbackAutoRegister g_fontchanger_compat(
        TJS_W("fontchanger.dll"), ncbAutoRegister::PreRegist, &FontChangerInit,
        nullptr);

} // namespace
