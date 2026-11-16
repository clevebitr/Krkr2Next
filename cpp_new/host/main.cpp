#include <cstdio>
#include <cstddef>
#include <cstdlib>

#include "core/env/app/EngineRuntime.h"
#include "core/log/Logger.h"
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


int main(int argc, char **argv) {
    if(argc < 2) {
        std::printf("usage:krkr_next <game_dir> [log_file]\n");
        return 2;
    }

    krkr::log::Logger &logger = krkr::log::Logger::Instance();
    ApplyEnvOverrides(logger);

    if (argc >= 3 && argv[2][0] != '\0') {
        if(!logger.SetFile(argv[2])){
            std::fprintf(stderr,"log file disabled: %s\n",logger.GetFileError().c_str());
        }
    }
    

    krkr::env::EngineRuntime *engine = krkr::env::EngineRuntime::Create();
    if(engine == nullptr) {
        std::printf("create failed\n");
        return 1;
    }

    krkr::env::EngineResult res = engine->OpenGame(argv[1]);
    if(res != krkr::env::EngineResult::Ok) {
        std::printf("open failed (%d):%s\n", static_cast<int>(res),
                    engine->GetLastErrorText());
        krkr::env::EngineRuntime::Destroy(engine);
        return 1;
    }

    std::printf("root = \"%s\" kind = %d\n", engine->GetGameRoot().c_str(),
                static_cast<int>(engine->GetGameKind()));

    for(int i = 0; i < 180; ++i) {
        res = engine->Tick(1.0 / 60.0);
        if(res != krkr::env::EngineResult::Ok) {
            std::printf("tick %d failed (%d):%s\n", i, static_cast<int>(res),
                        engine->GetLastErrorText());
            break;
        }
    }

    std::printf("frames = %llu,game = %s\n",
                static_cast<unsigned long long>(engine->GetFrameCount()),
                argv[1]);

    krkr::env::EngineRuntime::Destroy(engine);

    return 0;
}
