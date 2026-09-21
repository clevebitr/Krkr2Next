//
// 虚拟文件提供者：逻辑上"存在"、但不在物理存储上的名字（引擎侧伴生脚本等）。
//
// 为什么放在 io：存储层（`TVPGetPlacedPath` / `TVPIsExistentStorageNoSearchNoNormalize` /
// `TVPCreateStream`）需要在"物理文件缺失"时仍能解析并读出内容；而**提供哪些名字**
// 属于层/发行版知识，不能写进 io（见 compat/README.md §2 的不变量）。所以 io 只留一个
// 泛型注册点，由 compat 层在启动期注册 provider。
//
// provider 只产出**内容字符串**，由 io 负责包成内存流——这样 provider 不需要认识
// 流类型，也不依赖 base 的流实现。
//
// 语义约定（与 AetherKiri 的伴生脚本一致）：
//   - **物理文件优先**：调用方先查真实存储，缺失时才问 provider（io 侧已按此接线）；
//   - `exists` 只判断"名字命中本 provider 的协议"，不查物理存储，也不得抛异常；
//   - `content` 返回 false 表示不处理，由 io 继续按常规路径报"找不到"；
//     返回 true 且内容为空 = 提供**空文件**（与"不处理"不同）。
//
#pragma once

#include <string>

#include "StorageIntf.h" // ttstr / tTJSBinaryStream

namespace krkr::io {

    // name 是否命中该 provider 的虚拟文件协议（不查物理存储）。
    using VirtualFileExistsFn = bool (*)(const ttstr &name);
    // 产出虚拟文件内容（只读）。返回 false 表示不处理。
    using VirtualFileContentFn = bool (*)(const ttstr &name,
                                          std::string &content);

    // 注册 provider。可在静态初始化期调用；同一 exists 重复注册视为更新 content。
    void RegisterVirtualFileProvider(VirtualFileExistsFn exists,
                                     VirtualFileContentFn content);
    // 注销 provider（按 exists 指针匹配）。
    void UnregisterVirtualFileProvider(VirtualFileExistsFn exists);

    // 是否命中任一 provider（带重入保护：provider 内部可安全再进 io 查询）。
    bool IsVirtualFile(const ttstr &name);
    // 打开命中的虚拟文件（只读，io 包成内存流）。未命中返回 nullptr。
    tTJSBinaryStream *OpenVirtualFile(const ttstr &name);

} // namespace krkr::io
