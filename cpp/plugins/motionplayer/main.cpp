//
// Created by LiDon on 2025/9/13.
// TODO: implement emoteplayer.dll plugin
//
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "tjsDictionary.h"
#include "EventIntf.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"

using namespace motion;
using namespace TJS;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

// ─────────────────────────────────────────────────────────────────────────────
// 每帧自动推进 + 自动重绘（自 AetherKiri 的 autoProgress / presentationHold 驱动
// 移植）。PSB 动画必须"每帧推进时钟 + 每帧重绘"才会动；本仓库原先缺这条驱动，
// 真机实测游戏只在开播时调一两次 progress（delta=0/1ms），于是
// `drawAnimated ... at tick=0` 永远是第一帧 —— 表现就是 Q版/SD 动画"只闪一两帧"。
//
// 自门控（与参考实现同一判据）：游戏自己在最近 ~120ms 内调过 progress/draw 时，
// 自动驱动让路，避免把时间线推快或重复绘制。对驱动完整的游戏零行为变化。
// ─────────────────────────────────────────────────────────────────────────────
namespace {

    class MotionAutoDriveHook : public tTVPContinuousEventCallbackIntf {
    public:
        void OnContinuousCallback(tjs_uint64 tick) override;
    };

    std::mutex g_autoDriveMutex;
    std::vector<motion::Player *> g_autoDrivePlayers;
    MotionAutoDriveHook g_autoDriveHook;
    bool g_autoDriveHooked = false;
    /// 上次推进的墙钟（脚本线程会重置、钩子在引擎线程读，所以用原子）。
    std::atomic<int64_t> g_autoDriveLastMs{ 0 };

    std::atomic<uint64_t> g_autoDriveRegisters{ 0 };
    /// 每次"（重）挂钩子" +1：钩子回调里据此打印一次"确认存活"。
    std::atomic<uint64_t> g_autoDriveHookGen{ 0 };

    void AutoDriveRegister(motion::Player *player) {
        if(!player)
            return;
        size_t count = 0;
        bool added = false;
        {
            std::lock_guard<std::mutex> lock(g_autoDriveMutex);
            if(std::find(g_autoDrivePlayers.begin(), g_autoDrivePlayers.end(),
                         player) == g_autoDrivePlayers.end()) {
                g_autoDrivePlayers.push_back(player);
                added = true;
            }
            count = g_autoDrivePlayers.size();
            // **无条件重挂**（新登记或有新玩家时）：TVPRemoveContinuousEventHook
            // 只把已有条目置空、TVPAddContinuousEventHook 追加一条，派发时会压缩，
            // 结果永远恰好一条活条目。这样即使 g_autoDriveHooked 与实际钩子表
            // 不一致（真机 2026-09-18 13:20:27 之后：登记照常发生，钩子却再没被
            // 回调过，SD 动画 tick 永远是 0），也能重新活过来。
            if(!g_autoDriveHooked || added) {
                TVPRemoveContinuousEventHook(&g_autoDriveHook);
                TVPAddContinuousEventHook(&g_autoDriveHook);
                g_autoDriveHooked = true;
                g_autoDriveLastMs = 0; // 下一帧重新取基准，避免停顿时跳一大步
                g_autoDriveHookGen.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // 登记探针：确认"游戏确实调了 play/draw、我们把玩家登记进来了"。
        // 没有这条日志时，无法区分"没登记"与"钩子没跑"。
        // 另外记录挂了几次钩子（代数）：代数在涨而"钩子确认存活"不出现，
        // 就说明钩子挂了却不再被引擎回调。
        const uint64_t n = g_autoDriveRegisters.fetch_add(1) + 1;
        if(added && (n <= 5 || (n % 200) == 0) && LOGGER)
            LOGGER->info("MCP 自动驱动: 登记玩家 {}（表内 {} 个，第 {} 次登记，"
                         "钩子代数 {}）",
                         static_cast<const void *>(player), count, n,
                         g_autoDriveHookGen.load(std::memory_order_relaxed));
    }

    void AutoDriveUnregister(motion::Player *player) {
        std::lock_guard<std::mutex> lock(g_autoDriveMutex);
        g_autoDrivePlayers.erase(
            std::remove(g_autoDrivePlayers.begin(), g_autoDrivePlayers.end(),
                        player),
            g_autoDrivePlayers.end());
        if(g_autoDriveHooked && g_autoDrivePlayers.empty()) {
            TVPRemoveContinuousEventHook(&g_autoDriveHook);
            g_autoDriveHooked = false;
        }
    }

    void AutoDriveClearAll() {
        std::vector<motion::Player *> players;
        {
            std::lock_guard<std::mutex> lock(g_autoDriveMutex);
            players.swap(g_autoDrivePlayers);
        }
        if(g_autoDriveHooked) {
            TVPRemoveContinuousEventHook(&g_autoDriveHook);
            g_autoDriveHooked = false;
        }
        g_autoDriveLastMs = 0;
        (void)players; // 只清登记表：对象生命周期由各自的所有者负责
    }

    // 每 600 帧一条心跳，便于在真机日志里确认这条驱动确实在跑。
    std::atomic<uint64_t> g_autoDriveCalls{ 0 };

    int64_t AutoDriveNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

} // namespace

// 供 Player 析构调用（声明在 Player.h）：把死掉的 player 从登记表里摘掉，
// 否则连续钩子会解引用悬垂指针（真机即 SIGSEGV）。
void motion::AutoDriveForget(motion::Player *player) {
    if(!player)
        return;
    std::lock_guard<std::mutex> lock(g_autoDriveMutex);
    g_autoDrivePlayers.erase(
        std::remove(g_autoDrivePlayers.begin(), g_autoDrivePlayers.end(), player),
        g_autoDrivePlayers.end());
}

void MotionAutoDriveHook::OnContinuousCallback(tjs_uint64 /*tick*/) {
    static std::atomic<uint64_t> s_hookCalls{ 0 };
    const uint64_t hookCall = s_hookCalls.fetch_add(1) + 1;

    std::vector<motion::Player *> players;
    {
        std::lock_guard<std::mutex> lock(g_autoDriveMutex);
        players = g_autoDrivePlayers;
    }
    // "钩子确认存活"：每次（重）挂后打印一次。真机上出现过"登记照常发生、钩子却
    // 再没被回调过"的状态，只靠 first-3 计数看不出来（计数器跨重挂递增）。
    {
        static uint64_t s_provenGen = 0;
        const uint64_t gen = g_autoDriveHookGen.load(std::memory_order_relaxed);
        if(gen != s_provenGen) {
            s_provenGen = gen;
            if(LOGGER)
                LOGGER->info("MCP 自动驱动: 钩子确认存活（代数 {}，回调第 {} 次，"
                             "表内 {} 个玩家）",
                             gen, hookCall, players.size());
        }
    }
    // 钩子调用探针：first 3 + every 600 —— 区分"钩子根本没被调用"与"调用了但表为空"。
    if(hookCall <= 3 || (hookCall % 600) == 0) {
        if(LOGGER)
            LOGGER->info("MCP 自动驱动: 连续事件第 {} 次（表内 {} 个玩家）",
                         hookCall, players.size());
    }
    if(players.empty())
        return;

    const int64_t now = AutoDriveNowMs();
    int64_t deltaMs = 16;
    const int64_t lastMs = g_autoDriveLastMs.load(std::memory_order_relaxed);
    if(lastMs != 0) {
        deltaMs = now - lastMs;
        if(deltaMs < 0)
            deltaMs = 0;
        if(deltaMs > 100) // 卡顿后不要一次跳完，参考实现同样是 clamp
            deltaMs = 100;
    }
    g_autoDriveLastMs.store(now, std::memory_order_relaxed);

    const uint64_t call = g_autoDriveCalls.fetch_add(1) + 1;
    const bool heartbeat = (call == 1 || (call % 600) == 0);

    for(auto *player : players) {
        if(!player)
            continue;
        // 门控探针：① 每个玩家**首次**被钩子看到时记一条（每个 motion 一条，约
        // 1~2 条/秒，SD/动效那段才看得到状态）；② 外加前 10 次回调 + 每 600 次一条
        // 的常规采样。自动驱动"登记了、钩子也在跑、却既不推进也不重绘"时，只有这组
        // 状态能说明是被哪一道门拦住的（真机 SD 动效只画一帧就是这种情况）。
        bool firstSight = false;
        {
            static std::mutex s_seenMutex;
            static std::set<motion::Player *> s_seen;
            std::lock_guard<std::mutex> lk(s_seenMutex);
            firstSight = s_seen.insert(player).second;
            if(s_seen.size() > 4096)
                s_seen.clear();
        }
        if((firstSight || call <= 10 || (call % 600) == 0) && LOGGER)
            LOGGER->info(
                "MCP 自动驱动: 门控{} player={} playing={} progressRecent={} "
                "capture={} drawRecent={} hasTarget={} tick={}",
                firstSight ? "(首次)" : "",
                static_cast<const void *>(player),
                player->autoProgressEligible() ? 1 : 0,
                player->manualProgressRecent() ? 1 : 0,
                player->captureActive() ? 1 : 0,
                player->manualDrawRecent() ? 1 : 0,
                player->lastDrawTarget() ? 1 : 0, player->getTickCount());
        // 游戏没在播 / 已经停了：摘掉登记。
        if(!player->autoProgressEligible()) {
            if(LOGGER)
                LOGGER->info("MCP 自动驱动: 摘除 player={}（playing=0，tick={}）",
                             static_cast<const void *>(player),
                             player->getTickCount());
            AutoDriveUnregister(player);
            continue;
        }

        // 时间线推进：游戏自己在最近 120ms 内推过就让路（避免把时间线推快）。
        // ⚠️ 只让"推进"，**不能让每帧重绘也跟着让路**：真机 2026-09-18 的 SD/片头
        // 动画里游戏每帧调 Player.progress（Player.progress 探针计数已过 600），却
        // 只在开播时调一次 draw（drawAnimated 每个 motion 只有一条、tick 恒为 0）——
        // 结果是"只闪一帧"。这里改成：推进可以让路，重绘照旧每帧做。
        bool finished = false;
        if(!player->manualProgressRecent()) {
            finished = player->progress(static_cast<tjs_int>(deltaMs));
            if(heartbeat && LOGGER)
                LOGGER->info("MCP 自动驱动: 推进 {}ms -> tick={} finished={} "
                             "(player={})",
                             static_cast<int>(deltaMs), player->getTickCount(),
                             finished ? 1 : 0,
                             static_cast<const void *>(player));
        }

        // 动画播完并且是脚本在等 onSync 的情形：由脚本自己处理（我们不冒充脚本
        // 事件），把登记摘掉，避免空转。
        if(finished) {
            AutoDriveUnregister(player);
            continue;
        }

        // 每帧重绘。游戏自己在最近 120ms 内画过就让路（那是它自己的驱动）。
        //
        // capture 通道（游戏调用过 captureCanvas）**不能再无条件让路**：原实现认为
        // "capture 交付自己每帧画（drawOnto）"，可真机实测游戏只在开播时调一次，
        // 整段 SD 动效一条 drawOnto 日志都没有 —— 目标层于是永远停在第一帧，画面只剩
        // UI（用户报的"Q版/SD 动效不显示"）。这里改成由我们补上每帧的 capture 交付
        // （captureDrawTo 就是 drawOnto 的公开入口）。
        if(player->manualDrawRecent())
            continue;
        if(auto *target = player->lastDrawTarget()) {
            const bool capture = player->captureActive();
            if(capture)
                player->captureDrawTo(target);
            else
                player->draw(target);
            // 自动重绘探针：确认"每帧重绘"这条真的发生了（Q版/SD 动画只闪一帧时，
            // 有推进日志却没有这条，就说明重绘被让路逻辑吃掉了）。
            static std::atomic<uint64_t> s_redraws{ 0 };
            const uint64_t redrawCount = s_redraws.fetch_add(1) + 1;
            if((redrawCount <= 5 || (redrawCount % 300) == 0) && LOGGER)
                LOGGER->info("MCP 自动驱动: 自动重绘[{}] player={} tick={}（第 {} 次）",
                             capture ? "capture" : "direct",
                             static_cast<const void *>(player),
                             player->getTickCount(), redrawCount);
        }
    }
}


static motion::SeparateLayerAdaptor *
GetSeparateLayerAdaptorInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(
        objthis);
}

static iTJSDispatch2 *
GetSeparateAdaptorRenderTarget(motion::SeparateLayerAdaptor *adaptor);

iTJSDispatch2 *ResolveLayerTreeOwnerBase(iTJSDispatch2 *base) {
    if(!base)
        return nullptr;
    auto *adaptor =
        ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(
            base);
    if(adaptor)
        return GetSeparateAdaptorRenderTarget(adaptor);
    return base;
}

static iTJSDispatch2 *
GetSeparateAdaptorRenderTarget(motion::SeparateLayerAdaptor *adaptor) {
    if(!adaptor)
        return nullptr;
    if(adaptor->getTarget())
        return adaptor->getTarget();

    auto *owner = adaptor->getOwner();
    if(!owner)
        return nullptr;

    tTJSVariant windowVar;
    iTJSDispatch2 *windowObj = owner;
    if(TJS_SUCCEEDED(
           owner->PropGet(0, TJS_W("window"), nullptr, &windowVar, owner)) &&
       windowVar.Type() == tvtObject && windowVar.AsObjectNoAddRef()) {
        windowObj = windowVar.AsObjectNoAddRef();
    }

    // 父层：参考实现（krkrsdl3；AetherKiri PlayerRender::
    // resolveSeparateLayerRenderTarget）把适配器的私有渲染层建成**构造函数 owner
    // 层的子层**，绝不挂到 window.primaryLayer。owner 是游戏放在正确 z 序位置上的
    // AffineLayer（脚本随后把 owner.type 改成 ltBinder，让渲染层紧贴其上绘制）；
    // 挂到 primaryLayer 会让 SD/emote 整体落到错误的层级——真机表现为「SD 渲染不
    // 到 UI 之上」。owner 不是真实 Layer 时退回旧的 primaryLayer 行为。
    iTJSDispatch2 *parentObj = nullptr;
    bool parentIsOwner = false;
    {
        tTJSNI_BaseLayer *ownerNative = nullptr;
        if(TJS_SUCCEEDED(owner->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&ownerNative))) &&
           ownerNative) {
            parentObj = owner;
            parentIsOwner = true;
        }
    }

    tTJSVariant parentVar;
    if(!parentObj) {
        if(TJS_FAILED(owner->PropGet(0, TJS_W("primaryLayer"), nullptr,
                                     &parentVar, owner)) ||
           parentVar.Type() != tvtObject || !parentVar.AsObjectNoAddRef()) {
            if(TJS_FAILED(windowObj->PropGet(0, TJS_W("primaryLayer"), nullptr,
                                             &parentVar, windowObj)) ||
               parentVar.Type() != tvtObject ||
               !parentVar.AsObjectNoAddRef()) {
                return owner;
            }
        }
        parentObj = parentVar.AsObjectNoAddRef();
    }

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return owner;

    tTJSVariant layerClassVar;
    if(TJS_FAILED(global->PropGet(0, TJS_W("Layer"), nullptr, &layerClassVar,
                                  global)) ||
       layerClassVar.Type() != tvtObject || !layerClassVar.AsObjectNoAddRef()) {
        global->Release();
        return owner;
    }

    iTJSDispatch2 *layerClass = layerClassVar.AsObjectNoAddRef();
    tTJSVariant args[2] = { tTJSVariant(windowObj, windowObj),
                            tTJSVariant(parentObj, parentObj) };
    tTJSVariant *argv[] = { &args[0], &args[1] };
    iTJSDispatch2 *layerObj = nullptr;
    const auto hr = layerClass->CreateNew(0, nullptr, nullptr, &layerObj, 2,
                                          argv, layerClass);
    global->Release();
    if(TJS_FAILED(hr) || !layerObj) {
        return owner;
    }

    auto syncProp = [&](const tjs_char *name) {
        tTJSVariant value;
        if(TJS_SUCCEEDED(owner->PropGet(0, name, nullptr, &value, owner))) {
            layerObj->PropSet(TJS_MEMBERENSURE, name, nullptr, &value,
                              layerObj);
        }
    };
    if(parentIsOwner) {
        // 子层坐标相对 owner：位置必须归零，否则会被 owner 自身的位置再偏移一次
        // （参考实现同样把 SetPosition/SetImagePosition 复位）。
        tTJSVariant zero(static_cast<tjs_int>(0));
        layerObj->PropSet(TJS_MEMBERENSURE, TJS_W("left"), nullptr, &zero,
                          layerObj);
        layerObj->PropSet(TJS_MEMBERENSURE, TJS_W("top"), nullptr, &zero,
                          layerObj);
    } else {
        syncProp(TJS_W("left"));
        syncProp(TJS_W("top"));
    }
    syncProp(TJS_W("width"));
    syncProp(TJS_W("height"));
    syncProp(TJS_W("visible"));
    syncProp(TJS_W("opacity"));
    syncProp(TJS_W("name"));

    // 一次性路由日志（每个 adaptor 一条，封顶 8 条）：回答「SD/emote 渲染层挂在
    // 谁下面」——层级类问题的判定点。状态边沿日志，不是高频探针。
    {
        static std::atomic<int> s_targetRoute{0};
        if(s_targetRoute.fetch_add(1) < 8) {
            auto lg = spdlog::get("plugin");
            if(lg) {
                ttstr parentName;
                tTJSVariant nameVar;
                if(TJS_SUCCEEDED(parentObj->PropGet(0, TJS_W("name"), nullptr,
                                                    &nameVar, parentObj)))
                    parentName = ttstr(nameVar);
                lg->info("motion: SeparateLayerAdaptor 渲染层路由 owner={} "
                         "parent={} parentIsOwner={} parentName='{}'",
                         static_cast<const void *>(owner),
                         static_cast<const void *>(parentObj),
                         parentIsOwner ? 1 : 0, parentName.AsStdString());
            }
        }
    }

    // Prevent the render target from intercepting mouse events;
    // hitThreshold=256 makes hit test always fail (max alpha is 255)
    tTJSVariant htVal(static_cast<tjs_int>(256));
    layerObj->PropSet(TJS_MEMBERENSURE, TJS_W("hitThreshold"), nullptr, &htVal,
                      layerObj);

    // Ensure the owner AnimKAGLayer passes hit test even without its own
    // bitmap; hitThreshold=0 means bounds-only checking (no alpha test needed)
    tTJSVariant ownerHtVal(static_cast<tjs_int>(0));
    owner->PropSet(TJS_MEMBERENSURE, TJS_W("hitThreshold"), nullptr,
                   &ownerHtVal, owner);

    adaptor->setTarget(layerObj);
    layerObj->Release();
    return adaptor->getTarget() ? adaptor->getTarget() : owner;
}

static tjs_error SeparateLayerAdaptor_getWidth(tTJSVariant *r, tjs_int,
                                               tTJSVariant **,
                                               iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("width"), nullptr, &value, target);
    if(r) {
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    }
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_getHeight(tTJSVariant *r, tjs_int,
                                                tTJSVariant **,
                                                iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr =
        target->PropGet(0, TJS_W("height"), nullptr, &value, target);
    if(r) {
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    }
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_loadImages(tTJSVariant *r, tjs_int count,
                                                 tTJSVariant **p,
                                                 iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 1)
        return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("loadImages"), nullptr, r, count, p,
                            target);
}

static tjs_error SeparateLayerAdaptor_fillRect(tTJSVariant *r, tjs_int count,
                                               tTJSVariant **p,
                                               iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 5)
        return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("fillRect"), nullptr, r, count, p, target);
}

static tjs_error SeparateLayerAdaptor_operateRect(tTJSVariant *r, tjs_int count,
                                                  tTJSVariant **p,
                                                  iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 9)
        return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("operateRect"), nullptr, r, count, p,
                            target);
}

static tjs_error SeparateLayerAdaptor_getFace(tTJSVariant *r, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("face"), nullptr, &value, target);
    if(r)
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_setFace(tTJSVariant *r, tjs_int count,
                                              tTJSVariant **p,
                                              iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 1)
        return TJS_E_INVALIDPARAM;
    return target->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, p[0],
                           target);
}

static tjs_error SeparateLayerAdaptor_getImageWidth(tTJSVariant *r, tjs_int,
                                                    tTJSVariant **,
                                                    iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr =
        target->PropGet(0, TJS_W("imageWidth"), nullptr, &value, target);
    if(r)
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_getImageHeight(tTJSVariant *r, tjs_int,
                                                     tTJSVariant **,
                                                     iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr =
        target->PropGet(0, TJS_W("imageHeight"), nullptr, &value, target);
    if(r)
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

// 千恋万花等 Yuzusoft 作品：motion 的 work layer 由 SeparateLayerAdaptor 承载，
// helper: map a TJS value / dispatch to a short type name, for captureCanvas
// diagnostics. 辅助：把 TJS 值/调度对象映射为简短类型名，供 captureCanvas
// 诊断使用。
static ttstr VariantTypeName(const tTJSVariant &v) {
    switch(v.Type()) {
        case tvtVoid:
            return TJS_W("void");
        case tvtObject:
            return TJS_W("object");
        case tvtString:
            return TJS_W("string");
        case tvtInteger:
            return TJS_W("int");
        case tvtReal:
            return TJS_W("real");
        case tvtOctet:
            return TJS_W("octet");
        default:
            return TJS_W("?");
    }
}
static ttstr VariantTypeName(iTJSDispatch2 *o) {
    return o ? TJS_W("obj") : TJS_W("void");
}

// captureCanvas 是**每帧**调用的诊断点：逐次 info 会把日志刷爆（60 行/秒），
// 构造 ttstr 签名还有堆开销。这里只放行前 3 次（拿到真实调用契约），之后每 300
// 次留一条心跳 —— 心跳本身就回答了排查"Q 版动画只显示一两帧"时要问的问题：
// 这条交付到现在还在被驱动吗？slot 区分两个宿主（0=SeparateLayerAdaptor，
// 1=D3DAdaptor）。
static bool MotionCaptureCallDue(int slot, const char *who) {
    static std::atomic<uint64_t> s_calls[2];
    const uint64_t n = s_calls[slot & 1].fetch_add(1);
    if(n < 3)
        return true;
    if((n % 300) != 0)
        return false;
    auto lg = spdlog::get("plugin");
    if(lg)
        lg->info("MCP {}.captureCanvas: 累计 {} 次（每 300 次一条心跳，"
                 "完整契约只记前 3 次）",
                 who ? who : "?", n + 1);
    return false;
}
// 游戏脚本 affinesourcemotion.tjs 会调 captureCanvas/canvasCaptureEnabled/
// unloadUnusedTextures（Kirikiroid2 发布 APK 的 libgame.so 同款成员，实证）。
// Senren Clinic etc. Yuzusoft titles: the motion work layer is carried by
// SeparateLayerAdaptor; affinesourcemotion.tjs calls captureCanvas /
// canvasCaptureEnabled / unloadUnusedTextures on it (same members verified in
// the official Kirikiroid2 APK libgame.so).
//
// ⚠️ 空壳性质说明（与 D3DAdaptor 的成员属同一语义的两套宿主）：
//   Kirikiroid2 的 D3DAdaptor 与 SeparateLayerAdaptor 都有 captureCanvas
//   等成员； 本文件把它们按宿主分别注册（SeparateLayerAdaptor / 下方
//   D3DAdaptor）， motionWorkLayer 实际是哪一个实例就命中哪一个。两者都遵循：
//     优先转发到目标 Layer（若它能处理则交给它），无目标/不可转发则 no-op
//     兜底。
//   移动端无 D3D、motion 走 CPU/GL 已直接渲染，"捕获进另一块 canvas"可跳过。
//   升级触发条件见 D3DAdaptor 注释：仅当游戏真的取用捕获结果作为后续图像源时。
//   Empty-shell note (the two hosts share the same semantics as D3DAdaptor):
//   both D3DAdaptor and SeparateLayerAdaptor expose captureCanvas et al.; we
//   register them per host, and whichever instance motionWorkLayer actually is
//   will win. Both follow: forward to target Layer first if possible, otherwise
//   no-op fallback. Mobile has no D3D — motion renders straight to the layer
//   via CPU/GL, so "capture into another canvas" is skippable. Upgrade
//   condition is described next to D3DAdaptor: only when a game actually takes
//   the captured result as a later image source.
static tjs_error SeparateLayerAdaptor_getCanvasCaptureEnabled(tTJSVariant *r,
                                                              tjs_int,
                                                              tTJSVariant **,
                                                              iTJSDispatch2 *) {
    // "是否可用 D3D canvas 捕获"：移动端无 D3D，但该属性被游戏脚本读取以决定
    // captureCanvas 可用性；返回 true 让游戏走"可捕获"的调用路径（其后 by no-op
    // 兜底），避免误判为不支持而走另一条更复杂/未实现的路径。与 Kirikiroid2
    // 一致。 Whether D3D canvas capture is enabled: mobile has no D3D, but the
    // script reads this to decide captureCanvas usability; return true to route
    // games through the captureCanvas no-op path (instead of an unimplemented
    // branch).
    if(r)
        *r = tTJSVariant(true);
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_captureCanvas(tTJSVariant *r,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
    auto dl = spdlog::get("plugin");
    // 这是**每帧**都会走的入口，逐次 info 会把日志和性能一起吃掉（构造 ttstr
    // 签名本身也有堆开销）。保留前 3 次的完整调用契约，之后抽样心跳，既能看出
    // "这条交付还在被驱动"，也不会刷屏。
    if(dl && MotionCaptureCallDue(/*slot=*/0, "SeparateLayerAdaptor")) {
        // TEMP DIAGNOSTIC: observe the real captureCanvas call contract from
        // the running game (bytecode-encrypted script). Removed once known.
        // 临时诊断：观察 captureCanvas 的真实调用契约（游戏脚本为加密字节码）。
        ttstr sig;
        sig += TJS_W("objthis=");
        sig += VariantTypeName(objthis);
        sig += TJS_W(" count=");
        sig += ttstr((tjs_int)numparams);
        for(tjs_int i = 0; i < numparams && i < 16; i++) {
            sig += TJS_W(" p");
            sig += ttstr(i);
            sig += TJS_W("=");
            if(!param[i]) {
                sig += TJS_W("null");
                continue;
            }
            sig += VariantTypeName(*param[i]);
            if((*param[i]).Type() == tvtInteger) {
                sig += TJS_W("(");
                sig += ttstr((tjs_int)*param[i]);
                sig += TJS_W(")");
            } else if((*param[i]).Type() == tvtReal) {
                sig += TJS_W("(");
                sig += ttstr(tTJSVariant((tjs_real)*param[i]));
                sig += TJS_W(")");
            }
        }
        dl->info("MCP SeparateLayerAdaptor.captureCanvas: {}",
                 sig.AsStdString());
    }
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    if(adaptor) {
        auto *target = GetSeparateAdaptorRenderTarget(adaptor);
        if(target) {
            tTJSVariant value;
            if(TJS_SUCCEEDED(target->FuncCall(0, TJS_W("captureCanvas"),
                                              nullptr, &value, 0, nullptr,
                                              target))) {
                if(r)
                    *r = value;
                return TJS_S_OK;
            }
        }
    }
    // Fallback mirror of D3DAdaptor.captureCanvas: composite the current motion
    // frame onto the game-supplied destination layer (param[0]).
    // 与 D3DAdaptor.captureCanvas 相同的兜底：把当前 motion 帧合成到游戏传入的
    // 目标层（param[0]）。
    auto *player = motion::Player::getLastDrawSource();
    if(player && numparams >= 1 && param[0] &&
       (*param[0]).Type() == tvtObject) {
        iTJSDispatch2 *dest = (*param[0]).AsObjectNoAddRef();
        if(dest)
            player->captureDrawTo(dest);
    }
    if(r)
        r->Clear();
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_unloadUnusedTextures(
    tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    if(adaptor) {
        auto *target = GetSeparateAdaptorRenderTarget(adaptor);
        if(target) {
            tTJSVariant value;
            if(TJS_SUCCEEDED(target->FuncCall(0, TJS_W("unloadUnusedTextures"),
                                              nullptr, &value, 0, nullptr,
                                              target))) {
                if(r)
                    *r = value;
                return TJS_S_OK;
            }
        }
    }
    if(r)
        r->Clear();
    return TJS_S_OK;
}

// krkrsdl3 / 参考实现把 SeparateLayerAdaptor.assign 留成 no-op：适配器的私有子层
// 已经是可见的呈现层，把它再拷贝回 owner 只会得到第二张偏移画面（AetherKiri
// SeparateLayerAdaptor::assignCompat 同语义）。
static tjs_error SeparateLayerAdaptor_assign(tTJSVariant *r, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
    if(r)
        *r = tTJSVariant();
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *));
    NCB_PROPERTY_RAW_CALLBACK_RO(width, SeparateLayerAdaptor_getWidth, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(height, SeparateLayerAdaptor_getHeight, 0);
    NCB_PROPERTY_RAW_CALLBACK(face, SeparateLayerAdaptor_getFace,
                              SeparateLayerAdaptor_setFace, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(imageWidth, SeparateLayerAdaptor_getImageWidth,
                                 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(imageHeight,
                                 SeparateLayerAdaptor_getImageHeight, 0);
    NCB_METHOD_RAW_CALLBACK(loadImages, SeparateLayerAdaptor_loadImages, 0);
    NCB_METHOD_RAW_CALLBACK(fillRect, SeparateLayerAdaptor_fillRect, 0);
    NCB_METHOD_RAW_CALLBACK(operateRect, SeparateLayerAdaptor_operateRect, 0);
    NCB_METHOD_RAW_CALLBACK(assign, SeparateLayerAdaptor_assign, 0);
    NCB_METHOD_RAW_CALLBACK(captureCanvas, SeparateLayerAdaptor_captureCanvas,
                            0);
    NCB_METHOD_RAW_CALLBACK(unloadUnusedTextures,
                            SeparateLayerAdaptor_unloadUnusedTextures, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(
        canvasCaptureEnabled, SeparateLayerAdaptor_getCanvasCaptureEnabled, 0);
}

// 脚本意图: EmoteVariable.useD3D = (typeof Motion.Player.useD3D === "Object") ?
// Motion.Player.useD3D : Motion.enableD3D; 但 then 分支实际赋的是比较结果
// (int)1 而非对象，导致 (int)1 to Object。故让 useD3D 返回整数， 使 typeof ===
// "Integer" 走 else，赋 Motion.enableD3D（stub 对象），避免两处 int→Object
// 报错。
static tjs_error Player_getUseD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
    *r = tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error Player_setUseD3D(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *) {
    if(count >= 1 && (*p)->Type() == tvtInteger)
        motion::Player::setUseD3D(static_cast<bool>(**p));
    return TJS_S_OK;
}
static tjs_error Player_getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *) {
    iTJSDispatch2 *obj = TJSCreateDictionaryObject();
    if(obj) {
        *r = tTJSVariant(obj);
        obj->Release();
    } else {
        *r = tTJSVariant();
    }
    return TJS_S_OK;
}
static tjs_error Player_setEnableD3D(tTJSVariant *, tjs_int count,
                                     tTJSVariant **p, iTJSDispatch2 *) {
    if(count >= 1 && (*p)->Type() == tvtInteger)
        motion::Player::setEnableD3D(static_cast<bool>(**p));
    return TJS_S_OK;
}

// 取实例内部真正的 motion::Player。
//
// 两种实例都要支持：Motion.Player 本身就是 Player；Motion.EmotePlayer 是持有
// Player 的壳层（NEKOPARA 4 的 system/AffineSourceMotion.tjs 用后者创建动态立
// 绘）。Player_* 全套回调都经这里取实例，所以两条路径共用一份实现。
static motion::Player *GetPlayerInstance(iTJSDispatch2 *objthis) {
    if(auto *player =
           ncbInstanceAdaptor<motion::Player>::GetNativeInstance(objthis))
        return player;
    if(auto *emote =
           ncbInstanceAdaptor<motion::EmotePlayer>::GetNativeInstance(objthis))
        return &emote->player();
    return nullptr;
}

static tjs_error Player_setVariable(tTJSVariant *r, tjs_int count,
                                    tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2)
        return TJS_E_INVALIDPARAM;
    player->setVariable(ttstr(*p[0]), *p[1]);
    if(r)
        *r = tTJSVariant();
    return TJS_S_OK;
}

static tjs_error Player_getVariable(tTJSVariant *r, tjs_int count,
                                    tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    if(r)
        *r = player->getVariable(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error Player_getPlaying(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getPlaying() : false);
    return TJS_S_OK;
}

// loopTime / animating —— Yuzusoft canSync() 依赖的同步查询属性，见 Player.h
// 注释。 loopTime / animating — sync-query properties used by Yuzusoft
// canSync().
static tjs_error Player_getLoopTime(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getLoopTime()
                                : static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error Player_setLoopTime(tTJSVariant *, tjs_int count,
                                    tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(player && count >= 1)
        player->setLoopTime(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}
static tjs_error Player_getAnimating(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getAnimating() : false);
    return TJS_S_OK;
}
static tjs_error Player_setAnimating(tTJSVariant *, tjs_int count,
                                     tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(player && count >= 1)
        player->setAnimating(p[0]->operator bool());
    return TJS_S_OK;
}

// outline —— Yuzusoft getOptions() 遍历的成员之一（描边宽度），见 Player.h
// 注释。 outline — member enumerated by Yuzusoft getOptions() (stroke width).
static tjs_error Player_getOutline(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getOutline()
                                : static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error Player_setOutline(tTJSVariant *, tjs_int count,
                                   tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(player && count >= 1)
        player->setOutline(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}

// zpos —— getOptions() 枚举的另一成员（Z 序/深度），见 Player.h 注释。
// zpos — another member enumerated by getOptions() (Z-order/depth).
static tjs_error Player_getZpos(tTJSVariant *r, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getZpos() : static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error Player_setZpos(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(player && count >= 1)
        player->setZpos(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}

// variableKeys —— getOptions() 拷进选项字典的动态变量键名数组，缺失抛错卡白屏。
// variableKeys — dynamic-variable key array copied into the option dict by
// getOptions(); missing it throws and freezes the scene.
static tjs_error Player_getVariableKeys(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) {
        if(player) {
            iTJSDispatch2 *arr = player->getVariableKeys();
            *r = tTJSVariant(arr);
            arr->Release();
        } else {
            *r = tTJSVariant();
        }
    }
    return TJS_S_OK;
}

static tjs_error Player_getAllplaying(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getAllplaying() : false);
    return TJS_S_OK;
}

static tjs_error Player_getMotion(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getMotion() : ttstr());
    return TJS_S_OK;
}

static tjs_error Player_setMotion(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setMotion(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error Player_getChara(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getChara() : ttstr());
    return TJS_S_OK;
}

static tjs_error Player_setChara(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setChara(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error Player_getTickCount(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(
            static_cast<tjs_int>(player ? player->getTickCount() : 0));
    return TJS_S_OK;
}

static tjs_error Player_setTickCount(tTJSVariant *, tjs_int count,
                                     tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setTickCount(static_cast<tjs_int>(**p));
    return TJS_S_OK;
}

static tjs_error Player_getLastTime(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(
            static_cast<tjs_int>(player ? player->getLastTime() : 0));
    return TJS_S_OK;
}

static tjs_error Player_setLastTime(tTJSVariant *, tjs_int count,
                                    tTJSVariant **p, iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setLastTime(static_cast<tjs_int>(**p));
    return TJS_S_OK;
}

static tjs_error Player_getSpeed(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(
            static_cast<tjs_real>(player ? player->getSpeed() : 1.0));
    return TJS_S_OK;
}

static tjs_error Player_setSpeed(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setSpeed(static_cast<tjs_real>(p[0]->AsReal()));
    return TJS_S_OK;
}

static tjs_error Player_getCompletionType(tTJSVariant *r, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(
            static_cast<tjs_int>(player ? player->getCompletionType() : 0));
    return TJS_S_OK;
}

static tjs_error Player_setCompletionType(tTJSVariant *, tjs_int count,
                                          tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setCompletionType(static_cast<tjs_int>(**p));
    return TJS_S_OK;
}

static tjs_error Player_play(tTJSVariant *, tjs_int count, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    const ttstr motion = ttstr(*p[0]);
    const tjs_int all =
        count >= 2 ? static_cast<tjs_int>(p[1]->AsInteger()) : 0;
    player->play(motion, all);
    // 交给每帧自动驱动：脚本只调一两次 play/draw 时，动画才动得起来（见驱动注释）。
    AutoDriveRegister(player);
    return TJS_S_OK;
}

static tjs_error Player_stop(tTJSVariant *, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player)
        return TJS_E_INVALIDPARAM;
    player->stop();
    AutoDriveUnregister(player);
    return TJS_S_OK;
}

static tjs_error Player_progress(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    // 游戏自己在推进：自动驱动让路（见驱动注释）。
    // ⚠️ 但 **delta=0 不算"在推进"**：时间线不会前进（_tickCount 不变），而自动驱动
    // 会因为"最近有人调过 progress"而让路 —— 动画就永远停在 frame 0。真机实证
    // （千恋万花 SD 动效）：同一个 player 两次 draw 相隔 1.1s，tick 仍然 = 0，画面
    // 只有 UI。E-mote 的 progress(delta_ms) 里 delta=0 语义就是"时间没走"，忽略它
    // 不会误伤：真在驱动时间线的游戏必然传非 0 值。
    const tjs_int progressDeltaMs = static_cast<tjs_int>(p[0]->AsInteger());
    if(progressDeltaMs != 0)
        player->noteManualProgress();
    const bool finished = player->progress(progressDeltaMs);
    // 动画时钟探针（只记前 5 次 + 每 600 次一条心跳）：PSB 动画靠游戏每帧调
    // `Player.progress(ms)` 推进时间线、再 `Player.draw(layer)` 出帧。真机实测
    // `drawAnimated: drew N at tick=0` 每次都停在 0 —— 这条日志用来分清"游戏根本
    // 没调 progress"（那就要插件自己每帧推进）与"调了但没生效"。
    {
        static std::atomic<uint64_t> s_progressCalls{ 0 };
        const uint64_t n = s_progressCalls.fetch_add(1) + 1;
        if(n <= 5 || (n % 600) == 0) {
            auto lg = spdlog::get("plugin");
            if(lg)
                lg->info("MCP Player.progress: 第 {} 次 player={} delta={}ms -> "
                         "tick={}",
                         n, static_cast<const void *>(player), progressDeltaMs,
                         player->getTickCount());
        }
    }
    // On motion end (non-looping), fire the game's onSync so the script can
    // advance / replay the next round (e.g. the title screen re-plays the
    // character entrance). Mirrors reference PlayerFrameProgress dispatch.
    // motion 播完（不循环）时触发游戏 onSync，让脚本推进/重播下一轮（如主界面
    // 每轮重播角色入场）。对应参考 PlayerFrameProgress 的事件派发。
    if(finished && objthis) {
        try {
            objthis->FuncCall(0, TJS_W("onSync"), nullptr, nullptr, 0, nullptr,
                              objthis);
        } catch(...) {
            // onSync may be absent / not implemented by this Player; ignore.
            // onSync 可能未实现，忽略。
        }
    }
    return TJS_S_OK;
}

static tjs_error Player_skipToSync(tTJSVariant *, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player)
        return TJS_E_INVALIDPARAM;
    player->skipToSync();
    return TJS_S_OK;
}

static tjs_error Player_setDrawAffineTranslateMatrix(tTJSVariant *,
                                                     tjs_int count,
                                                     tTJSVariant **p,
                                                     iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 6)
        return TJS_E_INVALIDPARAM;
    player->setDrawAffineTranslateMatrix(p[0]->AsReal(), p[1]->AsReal(),
                                         p[2]->AsReal(), p[3]->AsReal(),
                                         p[4]->AsReal(), p[5]->AsReal());
    return TJS_S_OK;
}

static tjs_error Player_setCoord(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2)
        return TJS_E_INVALIDPARAM;
    player->setCoord(static_cast<tjs_real>(p[0]->AsReal()),
                     static_cast<tjs_real>(p[1]->AsReal()));
    return TJS_S_OK;
}

static tjs_error Player_contains(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2)
        return TJS_E_INVALIDPARAM;
    if(r)
        *r = tTJSVariant(
            player->contains(static_cast<tjs_int>(p[0]->AsInteger()),
                             static_cast<tjs_int>(p[1]->AsInteger())));
    return TJS_S_OK;
}

static tjs_error Player_getCommandList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player)
        return TJS_E_INVALIDPARAM;
    iTJSDispatch2 *obj = player->getCommandList();
    if(r) {
        if(obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
    } else if(obj) {
        obj->Release();
    }
    return TJS_S_OK;
}

static tjs_error Player_getLayerMotion(tTJSVariant *r, tjs_int count,
                                       tTJSVariant **, iTJSDispatch2 *objthis) {
    if(count < 1)
        return TJS_E_INVALIDPARAM;
    if(r)
        *r = tTJSVariant(objthis);
    return TJS_S_OK;
}

static tjs_error Player_getLayerGetter(tTJSVariant *r, tjs_int count,
                                       tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    ttstr name = ttstr(*p[0]);
    iTJSDispatch2 *obj = player->createLayerGetter(objthis, name);
    if(r) {
        if(obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
    } else if(obj) {
        obj->Release();
    }
    return TJS_S_OK;
}

static tjs_error Player_clear(tTJSVariant *, tjs_int count, tTJSVariant **p,
                              iTJSDispatch2 *objthis) {
    static int sClearCount = 0;
    sClearCount++;
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2)
        return TJS_E_INVALIDPARAM;
    if(sClearCount <= 5 || sClearCount % 300 == 0) {
        if(auto l = LOGGER)
            l->info("Player_clear: callCount={}", sClearCount);
    }
    player->clear(p[0]->AsObjectNoAddRef(),
                  static_cast<tjs_int>(p[1]->AsInteger()));
    return TJS_S_OK;
}

static tjs_error Player_draw(tTJSVariant *, tjs_int count, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
    static int sDrawWrapCount = 0;
    sDrawWrapCount++;
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) {
        if(auto l = LOGGER)
            l->warn("Player_draw: player={} count={} callCount={}",
                    (void *)player, count, sDrawWrapCount);
        return TJS_E_INVALIDPARAM;
    }
    if(sDrawWrapCount <= 5 || sDrawWrapCount % 300 == 0) {
        if(auto l = LOGGER)
            l->info("Player_draw: calling draw, target={} callCount={}",
                    (void *)p[0]->AsObjectNoAddRef(), sDrawWrapCount);
    }
    player->draw(p[0]->AsObjectNoAddRef());
    // 只调 draw 不调 play 的用法也登记：驱动会自己判断是否该推进。
    AutoDriveRegister(player);
    return TJS_S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// 宽容回调：本引擎没有实现的 emote 成员一律"收下参数、返回空值"
//
// 为什么必须给成员而不能让它缺：TJS 里访问不存在的成员会抛
// `Member "x" does not exist`，异常会打断调用它的整段脚本（NEKOPARA 4 的
// createPlayer/removePlayer 就是这么把动态立绘与读档流程一起毁掉的）。返回空值
// 最坏只是少一层差分表情/少一次物理抖动，脚本能继续跑。
// ─────────────────────────────────────────────────────────────────────────────
static tjs_error MotionPlayer_ignoreArgs(tTJSVariant *, tjs_int, tTJSVariant **,
                                         iTJSDispatch2 *) {
    return TJS_S_OK;
}

static tjs_error MotionPlayer_getVoid(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
    if(r)
        r->Clear();
    return TJS_S_OK;
}

static tjs_error MotionPlayer_getFalse(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *) {
    if(r)
        *r = tTJSVariant(false);
    return TJS_S_OK;
}

static tjs_error MotionPlayer_getZero(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
    if(r)
        *r = tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

static tjs_error MotionPlayer_getEmptyArray(tTJSVariant *r, tjs_int,
                                            tTJSVariant **, iTJSDispatch2 *) {
    if(r) {
        if(iTJSDispatch2 *arr = TJSCreateArrayObject()) {
            *r = tTJSVariant(arr);
            arr->Release();
        } else {
            r->Clear();
        }
    }
    return TJS_S_OK;
}

static motion::EmotePlayer *GetEmotePlayerInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::EmotePlayer>::GetNativeInstance(objthis);
}

static tjs_error Player_getMaskMode(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(player ? player->getMaskMode()
                                : static_cast<tjs_int>(MaskModeAlpha));
    return TJS_S_OK;
}

static tjs_error Player_setMaskMode(tTJSVariant *, tjs_int count,
                                    tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1)
        return TJS_E_INVALIDPARAM;
    player->setMaskMode(static_cast<tjs_int>(p[0]->AsInteger()));
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getHairScale(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getHairScale() : 1.0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setHairScale(tTJSVariant *, tjs_int count,
                                          tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setHairScale(static_cast<double>(p[0]->AsReal()));
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getPartsScale(tTJSVariant *r, tjs_int,
                                           tTJSVariant **,
                                           iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getPartsScale() : 1.0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setPartsScale(tTJSVariant *, tjs_int count,
                                           tTJSVariant **p,
                                           iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setPartsScale(static_cast<double>(p[0]->AsReal()));
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getBustScale(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getBustScale() : 1.0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setBustScale(tTJSVariant *, tjs_int count,
                                          tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setBustScale(static_cast<double>(p[0]->AsReal()));
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getBodyScale(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getBodyScale() : 1.0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setBodyScale(tTJSVariant *, tjs_int count,
                                          tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setBodyScale(static_cast<double>(p[0]->AsReal()));
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getVisible(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getVisible() : true);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setVisible(tTJSVariant *, tjs_int count,
                                        tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setVisible(p[0]->AsInteger() != 0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getSmoothing(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getSmoothing() : false);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setSmoothing(tTJSVariant *, tjs_int count,
                                          tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setSmoothing(p[0]->AsInteger() != 0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getQueing(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getQueing() : false);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setQueing(tTJSVariant *, tjs_int count,
                                       tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setQueing(p[0]->AsInteger() != 0);
    return TJS_S_OK;
}

static tjs_error EmotePlayer_getMotionKey(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(r)
        *r = tTJSVariant(e ? e->getMotionKey() : ttstr());
    return TJS_S_OK;
}

static tjs_error EmotePlayer_setMotionKey(tTJSVariant *, tjs_int count,
                                          tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *e = GetEmotePlayerInstance(objthis);
    if(e && count >= 1)
        e->setMotionKey(ttstr(*p[0]));
    return TJS_S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion.Player 与 Motion.EmotePlayer 共用的成员表
//
// Motion.EmotePlayer 是"持有 Player"的壳层，GetPlayerInstance() 会把壳层内部的
// Player 交出来，因此同一份回调、同一份成员表对两种实例都成立。用宏而不是复制
// 两遍，避免两边漂移（NEKOPARA 4 的动态立绘走 EmotePlayer，千恋万花走 Player，
// 两边必须同时具备 play/progress/draw/setVariable 这套接口）。
// ─────────────────────────────────────────────────────────────────────────────
#define MOTION_PLAYER_COMMON_MEMBERS()                                        \
    NCB_PROPERTY_RAW_CALLBACK(useD3D, Player_getUseD3D, Player_setUseD3D,     \
                              TJS_STATICMEMBER);                              \
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Player_getEnableD3D,                 \
                              Player_setEnableD3D, TJS_STATICMEMBER);         \
    NCB_PROPERTY_RAW_CALLBACK_RO(playing, Player_getPlaying, 0);              \
    NCB_PROPERTY_RAW_CALLBACK_RO(allplaying, Player_getAllplaying, 0);        \
    NCB_PROPERTY_RAW_CALLBACK(motion, Player_getMotion, Player_setMotion, 0); \
    NCB_PROPERTY_RAW_CALLBACK(chara, Player_getChara, Player_setChara, 0);    \
    NCB_PROPERTY_RAW_CALLBACK(tickCount, Player_getTickCount,                 \
                              Player_setTickCount, 0);                        \
    NCB_PROPERTY_RAW_CALLBACK(lastTime, Player_getLastTime, Player_setLastTime, \
                              0);                                             \
    NCB_PROPERTY_RAW_CALLBACK(speed, Player_getSpeed, Player_setSpeed, 0);    \
    NCB_PROPERTY_RAW_CALLBACK(completionType, Player_getCompletionType,       \
                              Player_setCompletionType, 0);                   \
    /* loopTime/animating: Yuzusoft canSync() 的同步查询属性，见 Player.h */   \
    /* loopTime/animating: sync-query properties read by Yuzusoft canSync(). */\
    NCB_PROPERTY_RAW_CALLBACK(loopTime, Player_getLoopTime, Player_setLoopTime, \
                              0);                                             \
    NCB_PROPERTY_RAW_CALLBACK(animating, Player_getAnimating,                 \
                              Player_setAnimating, 0);                        \
    /* outline/zpos: getOptions() 遍历的成员，缺失会抛错卡白屏。 */            \
    /* outline/zpos: enumerated by getOptions(); missing members throw. */    \
    NCB_PROPERTY_RAW_CALLBACK(outline, Player_getOutline, Player_setOutline, 0); \
    NCB_PROPERTY_RAW_CALLBACK(zpos, Player_getZpos, Player_setZpos, 0);       \
    NCB_PROPERTY_RAW_CALLBACK_RO(variableKeys, Player_getVariableKeys, 0);    \
    /* emote 脚本会读的遮挡/同步/描边透传成员（宽容实现）。 */                 \
    /* maskMode/sync/outline pass-through members used by emote scripts. */   \
    NCB_PROPERTY_RAW_CALLBACK(maskMode, Player_getMaskMode, Player_setMaskMode, \
                              0);                                             \
    NCB_PROPERTY_RAW_CALLBACK_RO(syncWaiting, MotionPlayer_getFalse, 0);      \
    NCB_PROPERTY_RAW_CALLBACK_RO(syncActive, MotionPlayer_getFalse, 0);       \
    NCB_METHOD_RAW_CALLBACK(play, Player_play, 0);                            \
    NCB_METHOD_RAW_CALLBACK(stop, Player_stop, 0);                            \
    NCB_METHOD_RAW_CALLBACK(progress, Player_progress, 0);                    \
    NCB_METHOD_RAW_CALLBACK(skip, MotionPlayer_ignoreArgs, 0);                \
    NCB_METHOD_RAW_CALLBACK(skipToSync, Player_skipToSync, 0);                \
    NCB_METHOD_RAW_CALLBACK(pass, MotionPlayer_ignoreArgs, 0);                \
    NCB_METHOD_RAW_CALLBACK(releaseSyncWait, MotionPlayer_ignoreArgs, 0);     \
    NCB_METHOD_RAW_CALLBACK(setDrawAffineTranslateMatrix,                     \
                            Player_setDrawAffineTranslateMatrix, 0);          \
    NCB_METHOD_RAW_CALLBACK(setCoord, Player_setCoord, 0);                    \
    NCB_METHOD_RAW_CALLBACK(contains, Player_contains, 0);                    \
    NCB_METHOD_RAW_CALLBACK(getCommandList, Player_getCommandList, 0);        \
    NCB_METHOD_RAW_CALLBACK(getLayerMotion, Player_getLayerMotion, 0);        \
    NCB_METHOD_RAW_CALLBACK(getLayerGetter, Player_getLayerGetter, 0);        \
    NCB_METHOD_RAW_CALLBACK(clear, Player_clear, 0);                          \
    NCB_METHOD_RAW_CALLBACK(draw, Player_draw, 0);                            \
    NCB_METHOD_RAW_CALLBACK(setVariable, Player_setVariable, 0);              \
    NCB_METHOD_RAW_CALLBACK(getVariable, Player_getVariable, 0);

NCB_REGISTER_SUBCLASS_DELAY(Player) {
    NCB_CONSTRUCTOR(());
    MOTION_PLAYER_COMMON_MEMBERS();
}

NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    NCB_CONSTRUCTOR((ResourceManager));
    // 与 Motion.Player 同一套播放/绘制/变量接口（见 GetPlayerInstance）。
    MOTION_PLAYER_COMMON_MEMBERS();
    // ── emote 壳层专有属性（本引擎只存储）──
    NCB_PROPERTY_RAW_CALLBACK(hairScale, EmotePlayer_getHairScale,
                              EmotePlayer_setHairScale, 0);
    NCB_PROPERTY_RAW_CALLBACK(partsScale, EmotePlayer_getPartsScale,
                              EmotePlayer_setPartsScale, 0);
    NCB_PROPERTY_RAW_CALLBACK(bustScale, EmotePlayer_getBustScale,
                              EmotePlayer_setBustScale, 0);
    NCB_PROPERTY_RAW_CALLBACK(bodyScale, EmotePlayer_getBodyScale,
                              EmotePlayer_setBodyScale, 0);
    NCB_PROPERTY_RAW_CALLBACK(visible, EmotePlayer_getVisible,
                              EmotePlayer_setVisible, 0);
    NCB_PROPERTY_RAW_CALLBACK(smoothing, EmotePlayer_getSmoothing,
                              EmotePlayer_setSmoothing, 0);
    NCB_PROPERTY_RAW_CALLBACK(queing, EmotePlayer_getQueing,
                              EmotePlayer_setQueing, 0);
    NCB_PROPERTY_RAW_CALLBACK(motionKey, EmotePlayer_getMotionKey,
                              EmotePlayer_setMotionKey, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(module, MotionPlayer_getVoid, 0);
    // ── emote 壳层专有方法：本引擎无 Timeline/物理/序列化，全部宽容实现 ──
    // 说明：这些成员脚本会**无条件**调用，缺失即 `Member "x" does not exist`
    // 异常并终止播放器/读档（NEKOPARA 4 实证），因此宁可返回空值也不缺成员。
    NCB_METHOD_RAW_CALLBACK(create, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(initPhysics, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(assignState, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(show, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(hide, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(load, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(loadResource, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(unloadResource, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(unloadUnusedTextures, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(loadImages, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(loadSource, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(findSource, MotionPlayer_getVoid, 0);
    NCB_METHOD_RAW_CALLBACK(setRot, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setRotate, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setMirror, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setColor, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(moveVariable, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(startWind, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(stopWind, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setOuterForce, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(getOuterForce, MotionPlayer_getVoid, 0);
    NCB_METHOD_RAW_CALLBACK(serialize, MotionPlayer_getVoid, 0);
    NCB_METHOD_RAW_CALLBACK(unserialize, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(clone, MotionPlayer_getVoid, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableFrameList, MotionPlayer_getEmptyArray, 0);
    NCB_METHOD_RAW_CALLBACK(countVariables, MotionPlayer_getZero, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableLabelAt, MotionPlayer_getVoid, 0);
    // Timeline（差分表情）：一律"没在播"，脚本据此跳过差分绘制而不抛错。
    NCB_METHOD_RAW_CALLBACK(playTimeline, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(stopTimeline, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(fadeInTimeline, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(fadeOutTimeline, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setTimelineBlendRatio, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(setTimeline, MotionPlayer_ignoreArgs, 0);
    NCB_METHOD_RAW_CALLBACK(getMainTimelineLabelList, MotionPlayer_getEmptyArray,
                            0);
    NCB_METHOD_RAW_CALLBACK(getDiffTimelineLabelList, MotionPlayer_getEmptyArray,
                            0);
    NCB_METHOD_RAW_CALLBACK(getPlayingTimelineInfoList,
                            MotionPlayer_getEmptyArray, 0);
    NCB_METHOD_RAW_CALLBACK(getLoopTimeline, MotionPlayer_getFalse, 0);
    NCB_METHOD_RAW_CALLBACK(isLoopTimeline, MotionPlayer_getFalse, 0);
    NCB_METHOD_RAW_CALLBACK(getTimelinePlaying, MotionPlayer_getFalse, 0);
    NCB_METHOD_RAW_CALLBACK(isTimelinePlaying, MotionPlayer_getFalse, 0);
    NCB_METHOD_RAW_CALLBACK(getTimelineTotalFrameCount, MotionPlayer_getZero, 0);
}

static tjs_error ResourceManager_unload(tTJSVariant *, tjs_int count,
                                        tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *manager =
        ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(objthis);
    if(!manager || count < 1)
        return TJS_E_INVALIDPARAM;
    manager->unload(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error ResourceManager_clearCache(tTJSVariant *, tjs_int,
                                            tTJSVariant **,
                                            iTJSDispatch2 *objthis) {
    auto *manager =
        ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(objthis);
    if(!manager)
        return TJS_E_INVALIDPARAM;
    manager->clearCache();
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD_RAW_CALLBACK(unload, ResourceManager_unload, 0);
    NCB_METHOD_RAW_CALLBACK(clearCache, ResourceManager_clearCache, 0);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptSeed,
                            &ResourceManager::setEmotePSBDecryptSeed,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptFunc,
                            &ResourceManager::setEmotePSBDecryptFunc,
                            TJS_STATICMEMBER);
}

// D3DAdaptor —— 千恋万花等 Yuzusoft 作品的 D3D affine layer 适配器。
// D3DAdaptor — D3D affine layer adapter for Senren Clinic etc. Yuzusoft titles.
// mainwindow.tjs 的 motionD3DAdaptor getter 会**无条件** `new
// Motion.D3DAdaptor(...)` （VM ip45 实证：`new %1, %9(%-1, %2, %3, %4,
// %6)`，异常 "Called method is not implemented" = ncb 空类无构造函数，new
// 失败——不能用 `class D3DAdaptor{}`+NCB 注册）。 也不能用 classic tjsNative 的
// TJS_BEGIN_NATIVE_MEMBERS 放在自由函数里（该宏用 `this`，
// 只能在类构造/成员函数内展开 → Android 编译报 invalid use of 'this'）。
// mainwindow.tjs unconditionally does `new Motion.D3DAdaptor(...)`; an empty
// NCB class has no constructor so `new` fails, and TJS_BEGIN_NATIVE_MEMBERS
// cannot be used in a free function (it uses `this`, so Android fails to
// compile).
//
// 此处用公开的 TJSCreateNativeClassMethod + TJSNativeClassRegisterNCM
// 在创建类对象后 动态注册 captureCanvas/unloadUnusedTextures 方法与
// canvasCaptureEnabled 属性， 使 `new Motion.D3DAdaptor(...)`
// 生成的实例带有这些成员（对齐 Kirikiroid2 APK： 其 D3DAdaptor
// 类自带同款成员，实证）。affinesourcemotion.tjs 的 drawAffine 会
// `_window.motionWorkLayer.captureCanvas()`——若 D3DAdaptor 实例无该方法即闪退。
// We register captureCanvas/unloadUnusedTextures/canvasCaptureEnabled on the
// class object via the public TJSCreateNativeClassMethod +
// TJSNativeClassRegisterNCM, so instances created by `new
// Motion.D3DAdaptor(...)` carry these members — matching the D3DAdaptor class
// in the official Kirikiroid2 APK. drawAffine calls
// `_window.motionWorkLayer.captureCanvas()`, which otherwise crashes.
//
// ⚠️ 空壳性质说明（重要，防误判）：
//   - captureCanvas / unloadUnusedTextures / canvasCaptureEnabled 是 **D3D
//   canvas 捕获**
//     能力的占位（把已绘制的 motion 画面抓进另一块 D3D canvas）。
//   - 移动端无 D3D9，motion 走 CPU/GL **直接把内容画进目标
//   layer、即时呈现**，因此
//     "再抓一份"本身是无意义操作——返回空 + 保留已渲染内容 = **语义正确的
//     no-op**， 不是"没写完的 stub"。Kirikiroid2 移动端同定位。
//   - **承载画面的真渲染链路不在这些空方法里**：Motion.Player::draw 做 PSB
//   图缓存→
//     合成到 layer，Motion.ResourceManager 做真 PSB 解码/缓存/解密
//     seed。空方法只负责 "让脚本调用不抛 Member does not exist"，画面靠 Player
//     这套真链路。
//   - **何时必须从 no-op 升级为真实现**：仅当某个游戏把 captureCanvas
//   的捕获结果当
//     后续图像源使用（读取返回值 / 绘制到指定
//     layer）时。当前千恋万花反汇编证明它只是 调用、不取返回值，故 no-op
//     足够。若未来遇依赖捕获结果的游戏再做真实现。
//   Empty-shell note (important, do not misjudge as unfinished stub):
//   - These members are placeholders for D3D canvas capture.
//   - Mobile has no D3D9; motion renders directly into the target layer via
//     CPU/GL and is shown immediately, so "capturing another copy" is pointless
//     — returning empty while keeping the rendered content is a semantically
//     correct no-op, not a half-written stub (Kirikiroid2 mobile is the same).
//   - The real rendering that carries the picture is NOT in these empty
//   methods:
//     Motion.Player::draw caches PSB images and composites them to the layer,
//     and Motion.ResourceManager does real PSB decode/cache/decrypt-seed. The
//     empty methods only prevent "Member does not exist".
//   - Upgrade to a real implementation ONLY when a game actually uses the
//     captured result as a later image source (reads the return value or draws
//     to a target layer). Current Senren Clinic disassembly shows it calls
//     but ignores the return, so no-op is sufficient.
static tjs_error D3DAdaptor_captureCanvas(tTJSVariant *r, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
    auto l = spdlog::get("plugin");
    if(l && MotionCaptureCallDue(/*slot=*/1, "D3DAdaptor")) {
        // TEMP DIAGNOSTIC: observe the real captureCanvas call contract (param
        // count/types) from the running game, since the game script is
        // bytecode- encrypted. Removed once the contract is known.
        // 临时诊断：从运行中的游戏观察 captureCanvas
        // 的真实调用契约（入参个数/类型），
        // 因为游戏脚本是加密字节码。确认契约后移除。
        // 这是逐帧路径 ⇒ 只记前 3 次 + 每 300 次心跳，见 MotionCaptureCallDue。
        ttstr sig;
        sig += TJS_W("objthis=");
        sig += VariantTypeName(objthis);
        sig += TJS_W(" count=");
        sig += ttstr((tjs_int)numparams);
        for(tjs_int i = 0; i < numparams && i < 16; i++) {
            sig += TJS_W(" p");
            sig += ttstr(i);
            sig += TJS_W("=");
            if(!param[i]) {
                sig += TJS_W("null");
                continue;
            }
            sig += VariantTypeName(*param[i]);
            if((*param[i]).Type() == tvtInteger) {
                sig += TJS_W("(");
                sig += ttstr((tjs_int)*param[i]);
                sig += TJS_W(")");
            } else if((*param[i]).Type() == tvtReal) {
                sig += TJS_W("(");
                sig += ttstr(tTJSVariant((tjs_real)*param[i]));
                sig += TJS_W(")");
            }
        }
        l->info("MCP D3DAdaptor.captureCanvas: {}", sig.AsStdString());
    }
    // REAL integration: the game calls captureCanvas(destLayer) on every frame
    // to hand the motion picture to a layer it controls. param[0] is that
    // destination layer. We composite the current motion frame onto it so the
    // content lands in the z-order the game script manages (e.g. under the
    // title menu) instead of a free-floating child layer above everything.
    // 真实现：游戏每帧调 captureCanvas(destLayer)，把 motion
    // 画面交给它控制的层； param[0] 即该目标层。我们把当前 motion
    // 帧合成到它上面，让内容落在游戏脚本管理的
    // 层级序中（例如标题菜单之下），而不再是压在最上层的自由子层。
    auto *player = motion::Player::getLastDrawSource();
    if(player && numparams >= 1 && param[0] &&
       (*param[0]).Type() == tvtObject) {
        iTJSDispatch2 *dest = (*param[0]).AsObjectNoAddRef();
        if(dest)
            player->captureDrawTo(dest);
    }
    // 返回 void 让脚本 continue；不 clear，保留已渲染内容。
    // Return void so the script can continue; do not clear, keep rendered
    // content.
    if(r)
        r->Clear();
    return TJS_S_OK;
}

static tjs_error D3DAdaptor_unloadUnusedTextures(tTJSVariant *r, tjs_int,
                                                 tTJSVariant **,
                                                 iTJSDispatch2 *) {
    if(r)
        r->Clear();
    return TJS_S_OK;
}

static tjs_error D3DAdaptor_getCanvasCaptureEnabledProp(tTJSVariant *r,
                                                        iTJSDispatch2 *) {
    // "是否可用 D3D canvas 捕获"：移动端无 D3D，但返回 true 让游戏走
    // captureCanvas no-op
    // 路径，避免误判为不支持而走另一条更复杂/未实现的路径。与 Kirikiroid2
    // 一致。
    if(r)
        *r = tTJSVariant(true);
    return TJS_S_OK;
}

static tjs_error D3DAdaptor_setCanvasCaptureEnabledProp(const tTJSVariant *,
                                                        iTJSDispatch2 *) {
    // no-op setter：只读属性本无需 setter，但若传 nullptr，游戏脚本对
    // canvasCaptureEnabled 赋值时会经 tTJSNativeClassProperty::PropSet 直接调用
    // 空函数指针 → SIGSEGV（千恋万花 yuzulogo 动画实证：脚本写该属性即崩）。
    // 提供一个 no-op setter（纯吞掉值），与 getter 返回 true
    // 保持一致，避免空调用。 No-op setter: this property is logically
    // read-only, but passing nullptr makes tTJSNativeClassProperty::PropSet
    // call a null function pointer when the game script ASSIGNS
    // canvasCaptureEnabled -> SIGSEGV (verified on the Senren Banka yuzulogo
    // animation, which writes this property). Absorb the value and return OK.
    return TJS_S_OK;
}

// clearEnabled：游戏脚本 `AffineSourceMotion.tjs`（字符串表实证含 clearEnabled）
// 会对 D3DAdaptor 写这个属性；参考实现的 D3DAdaptor 有完整语义。壳层先提供
// 可写属性（避免缺成员），并一次性记录游戏写入的值以便定位。
// clearEnabled: the game script writes this property on D3DAdaptor; the
// reference D3DAdaptor implements it. Provide a writable property here and
// log the written value once.
static bool s_d3dClearEnabled = false;

static tjs_error D3DAdaptor_getClearEnabledProp(tTJSVariant *r,
                                                iTJSDispatch2 *) {
    if(r)
        *r = tTJSVariant(s_d3dClearEnabled);
    return TJS_S_OK;
}

static tjs_error D3DAdaptor_setClearEnabledProp(const tTJSVariant *v,
                                                iTJSDispatch2 *) {
    s_d3dClearEnabled = v && static_cast<bool>(*v);
    static std::atomic<int> s_logged{0};
    if(s_logged.fetch_add(1) < 4) {
        if(auto l = spdlog::get("plugin"))
            l->info("probe: D3DAdaptor.clearEnabled = {}",
                    s_d3dClearEnabled ? 1 : 0);
    }
    return TJS_S_OK;
}

static iTJSDispatch2 *Create_NC_D3DAdaptor() {
    auto *cls = new tTJSNativeClass(TJS_W("D3DAdaptor"));
    if(cls) {
        // captureCanvas / unloadUnusedTextures 方法
        TJSNativeClassRegisterNCM(
            cls, TJS_W("captureCanvas"),
            TJSCreateNativeClassMethod(D3DAdaptor_captureCanvas),
            TJS_W("D3DAdaptor"), nitMethod);
        TJSNativeClassRegisterNCM(
            cls, TJS_W("unloadUnusedTextures"),
            TJSCreateNativeClassMethod(D3DAdaptor_unloadUnusedTextures),
            TJS_W("D3DAdaptor"), nitMethod);
        // canvasCaptureEnabled 属性（只读语义，但提供 no-op setter
        // 防空调用崩溃）。 注意：RegisterNCM 内部会 `dsp->Release()`
        // 接管传入对象的所有权（tjsNative.cpp RegisterNCM 末尾
        // dsp->Release()），此处**不得**再手动 Release，否则 use-after-free。
        // Note: RegisterNCM internally does `dsp->Release()` to take ownership
        // of the passed object (tjsNative.cpp, end of RegisterNCM); do NOT
        // Release again here. Setter must NOT be nullptr or PropSet on
        // assignment calls a null function pointer (see
        // D3DAdaptor_setCanvasCaptureEnabledProp).
        iTJSDispatch2 *cProp = TJSCreateNativeClassProperty(
            D3DAdaptor_getCanvasCaptureEnabledProp,
            D3DAdaptor_setCanvasCaptureEnabledProp);
        TJSNativeClassRegisterNCM(cls, TJS_W("canvasCaptureEnabled"), cProp,
                                  TJS_W("D3DAdaptor"), nitProperty);
        iTJSDispatch2 *clrProp = TJSCreateNativeClassProperty(
            D3DAdaptor_getClearEnabledProp,
            D3DAdaptor_setClearEnabledProp);
        TJSNativeClassRegisterNCM(cls, TJS_W("clearEnabled"), clrProp,
                                  TJS_W("D3DAdaptor"), nitProperty);
    }
    return cls;
}

class Motion {
public:
    // 枚举常量统一走这个小宏：脚本把它们当**类上的静态成员**读
    // （NEKOPARA 4 的 createPlayer 第 63 字节就是 `Motion.MaskModeAlpha`），缺失
    // 即抛 `Member "x" does not exist` 并中断整段脚本。
    // Enum constants are read as static members of the Motion class object by
    // game scripts; a missing one throws and aborts the calling function.
#define MOTION_INT_CONST(name, value)                                        \
    static tjs_error name(tTJSVariant *r, tjs_int, tTJSVariant **,           \
                          iTJSDispatch2 *) {                                 \
        if(r)                                                                \
            *r = tTJSVariant(static_cast<tjs_int>(value));                    \
        return TJS_S_OK;                                                     \
    }

    static tjs_error getPlayFlagForce(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(1));
        return TJS_S_OK;
    }

    MOTION_INT_CONST(getPlayFlagChain, 2)
    MOTION_INT_CONST(getPlayFlagAsCan, 4)
    MOTION_INT_CONST(getPlayFlagJoin, 8)
    MOTION_INT_CONST(getPlayFlagStealth, 16)

    MOTION_INT_CONST(getLayerTypeObj, 0)
    MOTION_INT_CONST(getLayerTypeShape, 1)
    MOTION_INT_CONST(getLayerTypeLayout, 2)
    MOTION_INT_CONST(getLayerTypeMotion, 3)
    MOTION_INT_CONST(getLayerTypeParticle, 4)
    MOTION_INT_CONST(getLayerTypeCamera, 5)

    // MaskModeStencil/Alpha：遮罩模式。NEKOPARA 4 的 affinesourcemotion.tjs
    // 读 Motion.MaskModeAlpha 决定 maskMode（真机实证的致命缺失项）。
    MOTION_INT_CONST(getMaskModeStencil, 0)
    MOTION_INT_CONST(getMaskModeAlpha, 1)

    MOTION_INT_CONST(getTimelinePlayFlagParallel, 1)
    MOTION_INT_CONST(getTimelinePlayFlagSequential, 2)

    MOTION_INT_CONST(getTransformOrderFlip, 0)
    MOTION_INT_CONST(getTransformOrderAngle, 1)
    MOTION_INT_CONST(getTransformOrderZoom, 2)
    MOTION_INT_CONST(getTransformOrderSlant, 3)

    MOTION_INT_CONST(getCoordinateRecutangularXY, 0)
    MOTION_INT_CONST(getCoordinateRecutangularXZ, 1)

    static tjs_error getShapeTypePoint(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypeCircle(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(1));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypeRect(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(2));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypeQuad(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
        if(r)
            *r = tTJSVariant(static_cast<tjs_int>(3));
        return TJS_S_OK;
    }

    static tjs_error setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *) {
        if(count == 1 && (*p)->Type() == tvtInteger) {
            _enableD3D = static_cast<bool>(**p);
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    // Z（KIRIKIRI Z）游戏脚本 `Motion.D3DAdaptor`
    // 会被**无条件访问**：mainwindow.tjs 的 `motionD3DAdaptor` 属性 getter 先算
    // scWidth/2、pxHeight/2 后直接取 `Motion.D3DAdaptor` 并 `new` 之（engine(5)
    // 实证：保持 undefined → `Member "D3DAdaptor" does not exist`
    // 致命崩溃；engine(7) 实证：空 ncb 类无构造函数 → `new` 报 "Called method
    // is not implemented"）。affinesourcemotion.tjs 也在 D3D capture 路径 `new`
    // 它。移动端无 D3D： classic tjsNative 模式定义可 new 类（构造收任意参）+
    // 类上 captureCanvas/ unloadUnusedTextures no-op（Layer 原生另有同款 no-op
    // 兜底，见 LayerIntf.cpp）。
    static tjs_error getD3DAdaptor(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *) {
        iTJSDispatch2 *cls = Create_NC_D3DAdaptor();
        if(cls) {
            *r = tTJSVariant(cls);
            cls->Release();
        } else {
            *r = tTJSVariant();
        }
        return TJS_S_OK;
    }

    static tjs_error getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
        iTJSDispatch2 *obj = TJSCreateDictionaryObject();
        if(obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
        return TJS_S_OK;
    }

private:
    inline static bool _enableD3D;
};

#undef MOTION_INT_CONST

NCB_REGISTER_CLASS(Motion) {
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Motion::getEnableD3D,
                              Motion::setEnableD3D, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(D3DAdaptor, Motion::getD3DAdaptor,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagForce, Motion::getPlayFlagForce,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagChain, Motion::getPlayFlagChain,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagAsCan, Motion::getPlayFlagAsCan,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagJoin, Motion::getPlayFlagJoin,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagStealth, Motion::getPlayFlagStealth,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeObj, Motion::getLayerTypeObj,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeShape, Motion::getLayerTypeShape,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeLayout, Motion::getLayerTypeLayout,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeMotion, Motion::getLayerTypeMotion,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeParticle, Motion::getLayerTypeParticle,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(LayerTypeCamera, Motion::getLayerTypeCamera,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MaskModeStencil, Motion::getMaskModeStencil,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(MaskModeAlpha, Motion::getMaskModeAlpha,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TimelinePlayFlagParallel,
                                 Motion::getTimelinePlayFlagParallel,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TimelinePlayFlagSequential,
                                 Motion::getTimelinePlayFlagSequential,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderFlip, Motion::getTransformOrderFlip,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderAngle,
                                 Motion::getTransformOrderAngle,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderZoom, Motion::getTransformOrderZoom,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(TransformOrderSlant,
                                 Motion::getTransformOrderSlant,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(CoordinateRecutangularXY,
                                 Motion::getCoordinateRecutangularXY,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(CoordinateRecutangularXZ,
                                 Motion::getCoordinateRecutangularXZ,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypePoint, Motion::getShapeTypePoint,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeCircle, Motion::getShapeTypeCircle,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeRect, Motion::getShapeTypeRect,
                                 TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeQuad, Motion::getShapeTypeQuad,
                                 TJS_STATICMEMBER);
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
}

static void PreRegistCallback() {}

static void PostUnregistCallback() { AutoDriveClearAll(); }

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
