//
// 模块归属门（按兼容层放行/拒绝插件模块注册）。
//
// 为什么需要：两套兼容层会各自模拟同一批 Windows 插件（`zlib.dll`、`lzfs.dll`…）。本工程
// 的 `ncbAutoRegister` 在启动时把**所有**内置模块注册一遍，同一个模块名被两层各写一份
// registrar 时就会出现"同名 TJS 类注册两次"的类型混淆与卸载互相删除（证据见
// compat/recon/plugin-compat-diff.md §3）。所以：模块名可以**登记归属某一层**，注册时按
// 当前激活层放行或跳过。
//
// 规则（尽量保守，避免影响现状）：
//   - 没有登记归属的模块 ⇒ 一律放行（= 旧层现状，110 多个插件不受影响）；
//   - 登记为某层专属且当前不是该层 ⇒ 跳过注册，并打一条 info 说明原因。
#pragma once

#include "tjs.h"

#include "CompatLayer.h"

namespace krkr::compat {

    // 登记模块归属层。可在静态初始化期调用（本工程所有 ncbAutoRegister 都在静态期收集，
    // 真正注册发生在启动期 tvpLoadPlugins，所以登记一定早于查询）。
    // 同一个模块名重复登记为**不同**层时：记一条 error，并保留先登记的那条（先到先得）。
    void RegisterModuleOwner(const char *moduleName, LayerId layer);

    // 当前激活层是否允许注册该模块。未登记归属 ⇒ true。
    bool AllowModuleLoad(const ttstr &moduleName);

} // namespace krkr::compat
