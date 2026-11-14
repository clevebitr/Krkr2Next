//
// 模块存在性查询的注入点（把插件注册表从 io 的依赖里摘出去）。
//
// 背景：`TVPIsExistentStorage("xxx.dll")` 要回答"引擎是否内置了这个插件模块" —— 这是
// **插件系统**的知识，不是 IO 的知识。历史上 io 直接调 `ncbAutoRegister::HasModule`，
// 于是 IO 组件硬依赖 core_plugin_module；两套兼容层要各自决定"哪些模块存在"时，这个
// 直接依赖就成了障碍（见 compat/README.md §2 与 IO 搬迁顺序第 6 步）。
//
// 现在改成注入式：`core/plugin` 在启动期把查询函数注入进来，io 只认函数指针。
// 未注入时返回 false —— 与"注册表还没填充时 HasModule 返回 false"语义一致，
// 因此注入点放在 `TVPLoadInternalPlugins()` 的 `AllRegist()` 之前即可保持行为不变。
#pragma once

#include "tjs.h"

namespace krkr::io {

    // 查询"该模块名是否存在于内置插件表"。
    using ModuleQueryFn = bool (*)(const ttstr &name);

    // 注入查询实现（core/plugin 在启动期调用一次）。
    void SetModuleLocator(ModuleQueryFn fn);

    // 未注入时返回 false。
    bool HasModule(const ttstr &name);

} // namespace krkr::io
