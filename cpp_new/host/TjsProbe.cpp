#include <cstdio>
#include <memory>
#include <string>

#include "core/io/Storage.h"
#include "core/log/Logger.h"
#include "debugger/agent/EventRecorder.h"
#include "engine/ScriptRuntime.h"
#include "host/ProbeHost.h"
#include "shim/classes/TjsPlugins.h"
#include "shim/engine/TjsEngine.h"

// TJS2 测试台：宿主角色的最小实现。
//
//   tjs_probe <script.tjs> <root-path-file> [launch arguments...]
//
// 装配（引擎 + 原生类 + 兼容声明）由 cpp_new/engine 的 ScriptRuntime 完成，
// 与产品入口共用同一条链路；宿主能力来自 host/ProbeHost.h（与 window_probe 共用）。
// 断言约定：脚本正常结束、或抛出的异常消息含有 "-ok"，视为通过。

using krkr::hostprobe::IsAssertionPass;
using krkr::hostprobe::ProbeHost;
using krkr::hostprobe::ReadRootPath;
using krkr::hostprobe::ReadWholeFile;

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: tjs_probe <script.tjs> <root-path-file> [args...]\n");
    return 2;
  }

  std::string script;
  if (!ReadWholeFile(argv[1], script)) {
    std::fprintf(stderr, "cannot read script: %s\n", argv[1]);
    return 2;
  }

  std::string root;
  if (!ReadRootPath(argv[2], root) || root.empty()) {
    std::fprintf(stderr, "cannot read root path file: %s\n", argv[2]);
    return 2;
  }

  krkr::io::Storage storage(root);
  if (!storage.Mounted()) {
    std::fprintf(stderr, "cannot mount %s: %s\n", root.c_str(), storage.GetLastErrorText());
    return 2;
  }

  std::printf("script   = %s\n", argv[1]);
  std::printf("root     = %s\n", root.c_str());
  std::printf("bytes    = %zu\n", script.size());

  // 事件记录由环境变量开启（KRKR_DEBUG_EVENTS=<file>），不影响正常路径。
  const std::unique_ptr<krkr::debugger::EventRecorder> recorder =
      krkr::debugger::EventRecorder::FromEnvironment();
  if (recorder != nullptr) {
    krkr::log::Logger::Instance().SetDebugSink(recorder.get());
    storage.SetDebugSink(recorder.get());
  }

  // 断言用的静态插件：覆盖 Plugins.link 的命中路径（注册表与 TJS 无关）。
  krkr::script::RegisterStaticPlugin("probe_static.dll");

  ProbeHost host(argc, argv);
  host.SetExecutablePath(argv[0]);
  host.SetDataDirectory(root);
  if (recorder != nullptr) {
    host.SetDebugSink(recorder.get());
  }
  krkr::engine::ScriptRuntime runtime;
  std::string error;
  if (!runtime.Startup(&storage, host.Services(), &error)) {
    std::fprintf(stderr, "script runtime startup failed: %s\n", error.c_str());
    return 2;
  }

  const bool ok = runtime.GetEngine()->RunScript(script, argv[1], &error);
  if (ok) {
    std::printf("exec     = ok\n");
  } else {
    std::printf("exec     = script exception: %s\n", error.c_str());
  }
  if (host.IsExitRequested()) {
    std::printf("exit     = requested\n");
  }

  krkr::log::Logger::Instance().Flush();
  if (ok) {
    return 0;
  }
  return IsAssertionPass(error) ? 0 : 1;
}
