#pragma once

#include <string>

namespace krkr::script {
  // 编码转换的统一入口。tjs2 的字符宽度由平台决定（Windows 为 UTF-16，
  // Android/Linux 为 UTF-32），引擎其余部分只处理 UTF-8，转换集中于此。

  std::string Utf8FromWide(const wchar_t *text);

  std::wstring WideFromUtf8(const std::string &utf8);

  // 读取的脚本字节转为 UTF-8。识别 UTF-16LE（FF FE）与 UTF-8 BOM，
  // 其余情况按 UTF-8 处理。
  std::string DecodeScriptSource(const std::string &bytes);
} // namespace krkr::script
