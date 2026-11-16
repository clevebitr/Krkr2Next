#include <cstdio>
#include "core/env/app/EngineRuntime.h"

int main(int argc,char** argv) {
  std::printf("krkr_next:up\n");
  if (argc < 2) {
    std::printf("usage:krkr_next <game_dir>\n");
    return 2;
  }

  krkr::env::EngineRuntime *engine = krkr::env::EngineRuntime::Create();
  if (engine == nullptr) {
    std::printf("create failed\n");
    return 1;
  }

  krkr::env::EngineResult res = engine -> OpenGame(argv[1]);
  if (res != krkr::env::EngineResult::Ok) {
    std::printf("open failed (%d):%s\n",static_cast<int>(res),engine -> GetLastErrorText());
    krkr::env::EngineRuntime::Destroy(engine);
    return 1;
  }
 
  std::printf("root = \"%s\" kind = %d\n",engine -> GetGameRoot().c_str(),
                                          static_cast<int>(engine -> GetGameKind()));

  for (int i = 0; i < 180; ++i) {
    res = engine -> Tick(1.0 / 60.0);
    if (res != krkr::env::EngineResult::Ok) {
      std::printf("tick %d failed (%d):%s\n",
          i,
          static_cast<int>(res),
          engine -> GetLastErrorText()
      );
      break;
    }
  }
  
  std::printf("frames = %llu,game = %s\n",
      static_cast<unsigned long long>(engine -> GetFrameCount()),
      argv[1]
  );

  krkr::env::EngineRuntime::Destroy(engine);

  return 0;
}
