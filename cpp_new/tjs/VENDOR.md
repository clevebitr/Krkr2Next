# tjs/ 相对上游的差异

本目录是 krkrz 的 tjs2 源码拷贝。除下表登记的补丁外不得修改；新增补丁必须附
"上游代码在目标工具链下无法编译或行为错误"的证据（ARCHITECTURE.md §4.1）。

| # | 文件 | 修改 | 原因 / 证据 |
|---:|---|---|---|
| 1 | `tjsArray.cpp` | `stream>Write(` → `stream.Write(` | 上游拼写错误，只存在于非 CRLF 分支；任何工具链都编不过 |
| 2 | `tjsConfig.cpp` | `TJSRestoreFPUE()` 里 `if(!TJSFPUInit)` 补 `#if` 守卫 | 该变量只在 MSVC 分支声明；GCC/Clang 下未定义 |
| 3 | `tjsHashSearch.h` | `tTJSHashCache` 内 6 处基类调用改为 `this->GetCount()` / `this->ChopLast()` | 依赖基类是 dependent base，未限定名查找在 Clang 下报 `use of undeclared identifier 'GetCount'`；MSVC/GCC 容忍两阶段查找的宽松实现。证据：NDK r27d clang 18 编译 `tjsMessage.cpp` 失败，GCC 15 通过 |

不修改、只靠编译参数兼容的差异（不属于补丁）：

| 现象 | 处理 |
|---|---|
| `register` 存储类（C++17 起非法） | Clang：`-Wno-register` |
| 取临时 `tTJSVariantClosure` 的地址 | Clang：`-Wno-address-of-temporary` |
| `std::binary_function`（C++17 移除） | `shim/tjs_host/tjs_prelude.h` 在 libc++ 下补回同名模板 |

同步流程：重新拷贝 `tjs/` → 重放上表补丁 → 重新生成 `shim/tjs_host/generated/` 下的日期词表与消息表 → 跑 `tools/run_probes.sh`（桌面）与 `cmake --preset android-debug`（Android）。
