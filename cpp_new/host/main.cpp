#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <fstream>
#include <string>
#include <vector>

#include "core/api/EngineRuntime.h"
#include "core/log/Logger.h"
#include "engine/ScriptRuntime.h"

namespace {
    void ApplyEnvOverrides(krkr::log::Logger &logger) {
        if(const char *levelText = std::getenv("KRKR_LOG_LEVEL");
           levelText != nullptr) {
            krkr::log::Level level = logger.GetLevel();
            if(krkr::log::ParseLevel(levelText, level)) {
                logger.SetLevel(level);
            } else {
                std::fprintf(stderr,
                             "warning: unknown KRKR_LOG_LEVEL=\"%s\"\n",
                             levelText);
            }
        }
        if(const char *bytesText = std::getenv("KRKR_LOG_MAX_BYTES");
           bytesText != nullptr) {
            const long bytes = std::strtol(bytesText, nullptr, 10);
            if(bytes > 0) {
                logger.SetMaxFileBytes(static_cast<std::size_t>(bytes));
            }
        }
    }
} // namespace

namespace {

void PrintStartupLogs(krkr::log::Logger &logger) {
    std::fflush(stdout);
    char buffer[4096];
    const std::size_t written = logger.Drain(buffer, sizeof(buffer));
    if(written == 0) {
        std::fprintf(stderr, "[drain] (empty)\n");
        return;
    }
    std::size_t start = 0;
    for(std::size_t i = 0; i < written; ++i) {
        if(buffer[i] != '\n') {
            continue;
        }
        std::fprintf(stderr, "[drain] %.*s\n", static_cast<int>(i - start), buffer + start);
        start = i + 1;
    }
    if(start < written) {
        std::fprintf(stderr, "[drain] %.*s\n", static_cast<int>(written - start), buffer + start);
    }
}

} // namespace

int main(int argc, char **argv) {
    if(argc < 2) {
        std::printf("usage:krkr_next <game_dir|game.xp3> [log_file]\n");
        std::printf("      krkr_next --path-file <file> [log_file]\n");
        return 2;
    }

    std::string gameRoot = argv[1];
    const char *logFilePath = argc >= 3 ? argv[2] : nullptr;
    if(gameRoot == "--path-file") {
        if(argc < 3) {
            std::printf("--path-file needs a file argument\n");
            return 2;
        }
        std::ifstream pathFile(argv[2], std::ios::binary);
        if(!pathFile.is_open()) {
            std::printf("cannot open path file: %s\n", argv[2]);
            return 2;
        }
        std::getline(pathFile, gameRoot);
        while(!gameRoot.empty() &&
              (gameRoot.back() == '\r' || gameRoot.back() == '\n')) {
            gameRoot.pop_back();
        }
        logFilePath = argc >= 4 ? argv[3] : nullptr;
    }

    krkr::log::Logger &logger = krkr::log::Logger::Instance();
    ApplyEnvOverrides(logger);

    if (logFilePath != nullptr && logFilePath[0] != '\0') {
        if(!logger.SetFile(logFilePath)){
            std::fprintf(stderr,"log file disabled: %s\n",logger.GetFileError().c_str());
        }
    }
    
    krkr::env::EngineRuntime *engine = krkr::env::EngineRuntime::Create();
    if(engine == nullptr) {
        std::printf("create failed\n");
        return 1;
    }

    // 装配根与产品入口共用：脚本运行时由宿主注入，引擎侧不引用 shim/tjs。
    krkr::engine::ScriptRuntime scriptRuntime;
    engine->AttachScriptRuntime(&scriptRuntime);
    engine->SetExecutablePath(argv[0]);
    if(argc >= 3) {
        std::vector<std::string> arguments;
        for(int i = 3; i < argc; ++i) {
            arguments.emplace_back(argv[i]);
        }
        engine->SetLaunchArguments(std::move(arguments));
    }

    krkr::env::EngineResult res = engine->OpenGame(gameRoot.c_str());
    if(res != krkr::env::EngineResult::Ok) {
        std::printf("open failed (%d): %s\n", static_cast<int>(res),
                    engine->GetLastErrorText());
        std::printf("startup = %u\n", static_cast<unsigned>(engine->GetStartupState()));
        PrintStartupLogs(logger);
        engine->DetachScriptRuntime();
        krkr::env::EngineRuntime::Destroy(engine);
        return 1;
    }

    std::printf("root = \"%s\" kind = %d\n", engine->GetGameRoot().c_str(),
                static_cast<int>(engine->GetGameKind()));
    std::printf("startup = %u\n", static_cast<unsigned>(engine->GetStartupState()));
    PrintStartupLogs(logger);

    for(int i = 0; i < 180; ++i) {
        res = engine->Tick(1.0 / 60.0);
        if(res != krkr::env::EngineResult::Ok) {
            std::printf("tick %d failed (%d): %s\n", i, static_cast<int>(res),
                        engine->GetLastErrorText());
            break;
        }
    }

    std::printf("frames = %llu,game = %s\n",
                static_cast<unsigned long long>(engine->GetFrameCount()),
                gameRoot.c_str());

    engine->DetachScriptRuntime();
    krkr::env::EngineRuntime::Destroy(engine);

    return 0;
}
