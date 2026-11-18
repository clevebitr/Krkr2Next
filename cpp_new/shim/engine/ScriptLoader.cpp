#include "shim/engine/ScriptLoader.h"

#include <cstdint>
#include <memory>
#include <string>

#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "shim/TjsBridge.h"
#include "shim/TjsDiagnostics.h"
#include "shim/engine/TjsEngine.h"
#include "shim/text/TjsText.h"
#include "tjs/tjs.h"
#include "tjs/tjsByteCodeLoader.h"
#include "tjs/tjsError.h"

namespace krkr::script {

namespace {

// KiriKiri 的整合包会把部分脚本预编译成 TJS2 字节码（magic "TJS2" + 版本头），
// 包内扩展名仍是 .tjs。按文本源码解析这类文件会在第 1 行直接语法错误。
bool LooksLikeByteCode(const std::string &bytes) {
  if (bytes.size() <
      static_cast<std::size_t>(TJS::tTJSScriptBlock::BYTECODE_FILE_TAG_SIZE)) {
    return false;
  }
  return TJS::tTJSByteCodeLoader::IsTJS2ByteCode(
      reinterpret_cast<const tjs_uint8 *>(bytes.data()));
}

std::string ReadAllFromStorage(const std::string &name, const std::string &context) {
  krkr::io::Storage *storage = GetContext().storage;
  if (storage == nullptr) {
    ThrowScriptException(context, "storage is not attached");
  }

  const std::unique_ptr<krkr::io::IStream> stream = storage->Open(name);
  if (stream == nullptr) {
    ThrowScriptException(context, storage->GetLastErrorText());
  }

  std::string bytes;
  char buffer[16 * 1024];
  std::int64_t read = 0;
  while ((read = stream->Read(buffer, static_cast<std::int64_t>(sizeof(buffer)))) > 0) {
    bytes.append(buffer, static_cast<std::size_t>(read));
  }
  if (read < 0) {
    ThrowScriptException(context, "read error");
  }
  return bytes;
}

} // namespace

void RunScriptBytes(Engine &engine, const std::string &bytes, const std::string &scriptName,
                    const std::string &context, ScriptMode mode,
                    TJS::tTJSVariant *result, TJS::iTJSDispatch2 *scope) {
  if (!engine.IsReady()) {
    ThrowScriptException(context, "script engine is not ready");
  }

  auto *tjs = static_cast<TJS::tTJS *>(engine.GetVMScriptEngineRaw());
  const bool bytecode = LooksLikeByteCode(bytes);
  std::wstring source;
  if (!bytecode) {
    source = WideFromUtf8(DecodeScriptSource(bytes));
  }
  const std::wstring wideName = WideFromUtf8(scriptName);

  try {
    if (bytecode) {
      tjs->LoadByteCode(reinterpret_cast<const tjs_uint8 *>(bytes.data()), bytes.size(),
                        result, scope, wideName.c_str());
    } else if (mode == ScriptMode::Evaluate) {
      TJS::tTJSVariant evaluated;
      tjs->EvalExpression(source.c_str(), &evaluated, scope, wideName.c_str());
      if (result != nullptr) {
        *result = evaluated;
      }
    } else {
      tjs->ExecScript(source.c_str(), result, scope, wideName.c_str());
    }
  } catch (const TJS::eTJS &e) {
    // 记录带定位信息的上下文，然后原样重抛：虚拟机负责转成脚本侧异常并保留 trace。
    krkr::log::Error(DescribeScriptException(e, context));
    throw;
  } catch (const std::exception &e) {
    krkr::log::Error(context, ": ", e.what());
    ThrowScriptException(context, e.what());
  }
}

void RunScriptFromStorage(Engine &engine, const std::string &name, ScriptMode mode,
                          const char *caller, TJS::tTJSVariant *result,
                          TJS::iTJSDispatch2 *scope) {
  std::string context = caller == nullptr ? "Scripts" : caller;
  context += "(\"";
  context += name;
  context += "\")";

  const std::string bytes = ReadAllFromStorage(name, context);
  krkr::log::Debug(context);
  RunScriptBytes(engine, bytes, name, context, mode, result, scope);
}

} // namespace krkr::script
