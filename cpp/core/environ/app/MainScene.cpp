/**
 * @file MainScene.cpp
 * @brief Compatibility wrapper — delegates to EngineLoop.
 *
 * MainScene is kept as a thin compatibility layer so that existing code
 * (engine_api.cpp, standalone main.cpp) can still reference MainScene.
 * All real logic now lives in EngineLoop (Phase 3).
 */

#include "MainScene.h"
#include "EngineLoop.h"

#include <spdlog/spdlog.h>

namespace krkr::environ {

// ---------------------------------------------------------------------------
// Global functions — now defined in EngineLoop.cpp, only declared here
// for backward compatibility with headers that expect them in MainScene.
// (SetPostUpdateEvent, GetKeyMouseAsyncState, GetJoyPadAsyncState,
//  DrawSceneOnce are all implemented in EngineLoop.cpp)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MainScene implementation — thin delegation to EngineLoop
// ---------------------------------------------------------------------------

static MainScene *_instance = nullptr;

MainScene::MainScene() = default;
MainScene::~MainScene() {
    if(_instance == this) {
        _instance = nullptr;
    }
}

MainScene *MainScene::GetInstance() { return _instance; }

MainScene *MainScene::CreateInstance() {
    if(!_instance) {
        _instance = new MainScene();
        // Ensure EngineLoop singleton is also created
        EngineLoop::CreateInstance();
    }
    return _instance;
}

void MainScene::scheduleUpdate() {
    _updateScheduled = true;
    if(auto *loop = EngineLoop::GetInstance()) {
        loop->Start();
    }
}

void MainScene::update(float delta) {
    if(auto *loop = EngineLoop::GetInstance()) {
        loop->Tick(delta);
    }
}

bool MainScene::startupFrom(const std::string &path) {
    if(auto *loop = EngineLoop::GetInstance()) {
        bool result = loop->StartupFrom(path);
        if(result) {
            _started = true;
            _updateScheduled = true;
        }
        return result;
    }
    return false;
}

void MainScene::doStartup(const std::string &path) {
    // Delegated — EngineLoop::StartupFrom calls DoStartup internally
}

} // namespace krkr::environ
