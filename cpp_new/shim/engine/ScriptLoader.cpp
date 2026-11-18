#include "shim/engine/ScriptLoader.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "core/contracts/DebugServices.h"
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

  krkr::debug::IDebugSink *sink = GetContext().services.debug;
  krkr::debug::DebugEvent begin;
  begin.kind = krkr::debug::EventKind::ScriptBegin;
  begin.channel = "script";
  begin.name = scriptName;
  begin.size = static_cast<std::int64_t>(bytes.size());
  begin.ok = bytecode;
  krkr::debug::Emit(sink, begin);
  const auto started = std::chrono::steady_clock::now();

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
    const std::string text = DescribeScriptException(e, context);
    krkr::log::Error(text);
    krkr::debug::DebugEvent failure;
    failure.kind = krkr::debug::EventKind::ScriptFailure;
    failure.channel = "script";
    failure.name = scriptName;
    failure.detail = text;
    if (const auto *located = dynamic_cast<const TJS::eTJSScriptError *>(&e)) {
      failure.line = located->GetSourceLine();
    }
    krkr::debug::Emit(sink, failure);
    throw;
  } catch (const std::exception &e) {
    krkr::log::Error(context, ": ", e.what());
    ThrowScriptException(context, e.what());
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  krkr::debug::DebugEvent end;
  end.kind = krkr::debug::EventKind::ScriptEnd;
  end.channel = "script";
  end.name = scriptName;
  end.size = elapsed.count();
  krkr::debug::Emit(sink, end);
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
