// 事件触发模式枚举。
//
// 为什么单独放在 utils 而不是 base/EventIntf.h：
//   - `tTVPAsyncTriggerMode` 同时被 utils 的 Timer（TimerIntf.h）与 base 的
//     AsyncTrigger（EventIntf.h）使用；
//   - utils 是下层，base 是上层。枚举留在 base 会让 utils 的公共头反向 include
//     base 的中央头（EventIntf.h，50 个包含者），把 utils 从叶子上拽下来。
//   放在下层后，依赖方向变成 base -> utils（正向）。
#pragma once

namespace krkr::utils {

enum tTVPAsyncTriggerMode {
    atmNormal,
    atmExclusive,
    atmAtIdle,
};

} // namespace krkr::utils

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::utils::tTVPAsyncTriggerMode;
// 枚举值不会随类型自动导入（AGENTS.md §9.3）
using krkr::utils::atmNormal;
using krkr::utils::atmExclusive;
using krkr::utils::atmAtIdle;
