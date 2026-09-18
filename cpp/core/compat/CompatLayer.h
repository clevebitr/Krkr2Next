//
// 兼容层注册表与激活状态。
//
// 引擎同时带着两套兼容层的行为口径，但**一个进程只激活一层**：同名 Windows 插件被两层
// 各自模拟、同名 TJS 类被注册两次时，只有激活层的注册有意义（见 compat/README.md §1、§2）。
//
// 层的选择链路：
//   krkr2next.json（每游戏）→ 壳 RunMode → 引擎选项 game_compat_profile
//   → engine_api 的 ApplyCompatProfileLocked() → krkr::compat::SetActiveLayerByName()
// 缺省是旧版 krkr2 层（classic）：AetherKiri 层是新增代码路径，必须显式开启。
//
// 现状：本文件只登记"当前是哪一层"与"该层用哪套 IO 策略"。策略还没有接到
// StorageIntf/StorageImpl 的实现上（那是 M1.2–M1.6 的迁移工作），因此本文件当前对
// 运行时行为没有影响，只提供日志与后续接线的单一入口。
#pragma once

#include "io/StoragePolicy.h"

namespace krkr::compat {

    enum class LayerId {
        // 旧版 krkr2 层：本仓库既有行为（PocketKrKr / KrKr2-Next 血脉），缺省。
        Krkr2Classic,
        // AetherKiri 层：对齐 AetherKiri 的行为集合，按游戏显式开启。
        AetherKiri,
    };

    // 层名（日志与选项取值用）："krkr2-classic" / "aetherkiri"。
    const char *LayerName(LayerId id);

    // 解析层名。认 "krkr2-classic" / "classic" / "kirikiri2-classic" → 旧层，
    // "aetherkiri" / "ak" → AetherKiri 层。无法识别时返回 false（调用方保持当前层）。
    bool LayerFromName(const char *name, LayerId &out);

    // 当前激活层。缺省 Krkr2Classic。
    LayerId ActiveLayer();

    // 设置激活层；幂等（同层不重复打日志）。层切换只在引擎启动期有意义。
    void SetActiveLayer(LayerId id);

    // 按名字设置；返回是否识别成功。
    bool SetActiveLayerByName(const char *name);

    // 各层的 IO/加载策略取值。取值依据见 compat/recon/io-loading-diff.md §2A/2B/2D。
    io::StoragePolicy StoragePolicyFor(LayerId id);

    // 当前激活层的策略。
    const io::StoragePolicy &ActiveStoragePolicy();

} // namespace krkr::compat
