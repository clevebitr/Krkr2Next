# core/io —— 四层，目录名就是依赖方向

`io` 曾经是 20 个文件的扁平目录 + 一个 STATIC target。那个 target 的 PUBLIC
include 面有 15 条 `-I`，于是"io 依赖谁"这个问题只能靠读 20 个文件回答。
现在按依赖层次切成四个目录，目录名 = target 名后缀：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `policy/` | `core_io_policy` | 兼容层注入的 IO 口径：`StoragePolicy.h` / `IoPolicy.{h,cpp}` | 只依赖 base 的存储声明 |
| `path/` | `core_io_path` | 路径规范化、扩展名提取、模块存在性查询（`IoPath.cpp` / `IoModuleLocator`） | 只依赖 base |
| `archive/` | `core_io_archive` | 归档后端：XP3 / XP3-Cx / ZIP / 7z / TAR（+ `IoArchiveInternal.h`、`tar.h`） | `policy` + base + zlib/zstd/7zip/minizip |
| `storage/` | `core_io_storage` | 存储门面、本地文件、虚拟文件（`IoStorage` / `IoStorageLocal` / `IoVirtualFile`） | `policy` + `path` + `archive` + base |

分层依据是**实测 include 图**，不是文件名：

```
storage  →  archive   IoStorage.cpp:26 XP3Archive.h / IoStorageLocal.cpp:34-35
storage  →  path      IoStorage.cpp:28 IoModuleLocator.h
storage  →  policy    IoStorage.cpp:18 / IoStorageLocal.cpp:28 IoPolicy.h
archive  →  policy    XP3Archive.cpp:15 IoPolicy.h
path     →  -         IoPath.cpp 只 include base 的 StorageIntf.h
policy   →  -         只 include StoragePolicy.h
```

反向一条都没有：没有任何 `archive/`、`path/`、`policy/` 里的文件 include
`storage/` 的头。所以这四层是真的 DAG，可以按目录逐个理解。

`core_io_module` 是一个 **INTERFACE 聚合层**，把四个簇串起来。`base` / `environ` /
`plugins` / `tools/xp3` 里写的 `core_io_module` 一个字都没改。

## 两条规矩

**1. 簇只声明头面，链接行集中在聚合层。**
`base` 把链接行写在各簇上，代价是簇之间已经出现声明式的环
（`script ↔ runtime`）。`io` 只有 20 个文件，聚合层足够表达完整链接行，
所以这里四个簇只 `PUBLIC tjs2`，真正的 `core_base_module` / visual / plugin /
environ / 第三方库全部挂在 `core_io_module` 上 —— 和搬迁前的链接行逐条对应，
且不新增任何 target 环。

**2. `io` 不认识"层"。**
`krkr2-classic` / `aetherkiri` 这些名字只出现在 `compat/`，io 只认
`ActiveStoragePolicy()` 的取值。这条约束在 `IoPolicy.h` 和
`compat/README.md` §2 里各写了一遍，不要在哪一边破口子。

## 加一个新文件放哪

1. 只是给实现读的开关/取值 → `policy/`
2. 路径字符串处理、模块查找 → `path/`
3. 新归档格式（如 `.bin` / `.pak`）→ `archive/`，并在 `IoArchiveInternal.h` 里
   加前置声明，不要 include 别的后端
4. 挂载表、流、本地文件系统、虚拟文件 provider → `storage/`
5. 媒体注册表（`tTVPStorageMediaManager`）现在还留在
   `cpp/core/base/core/StorageIntf.cpp`，将来属于 `io/media/`，
   **不要**在新文件里另起一份

## 已知的环（不在本轮解决）

- **`io ↔ base`（声明级）**：`io` 的头 include `base/core/StorageIntf.h`
  （`ttstr` / `tTJSBinaryStream` / `tTVPArchive`），而 base 的
  `StorageIntf.h` 里有 52 条 `using krkr::io::…` 门面转发、`StorageIntf.cpp`
  又要 include io 的实现头。这是整条 `Storage` 门面还没拆完的残留：
  `base` 保留门面、`io` 提供声明，双向依赖在 `core_io_module` /
  `core_base_module` 两个聚合层上闭合。拆法见 `cpp/DECISIONS.md` #9 的同类讨论。
- **`policy` 半接线**：`StoragePolicy` 的取值已经接了一部分（M1.3：注入点 +
  段缓存预算），`auto path` tie-break / 挂载排序等仍是"登记了取值但实现还没读"。
  读 `policy/StoragePolicy.h` 时注意它自己写着这句话。
- **`archive` 的段缓存**：`TVPSegmentCacheLimit*` 的声明在
  `base/core/StorageIntf.h`，实现与覆盖标记在 `policy/IoPolicy.cpp`，
  两边是一对"分离的意图"，改动时两边一起看。

## 公共头的现状（技术债）

和 base 一样：`include` 语句写的还是裸文件名（`#include "XP3Archive.h"`），
靠四条 `-I io/{policy,path,archive,storage}` 解析 —— 也就是 io 仍然是一张扁平的
公共头面。`IO_LEGACY_INCLUDE_DIRS` 里那 15 条 `-I` 是搬迁前 io 对全引擎暴露的
include 面（包含 base / visual / environ / utils 的目录），**没有编译验证之前
按原样保留**：收窄它会同时改动 environ、plugins、tools/xp3 的编译结果。

显式路径已经在 4 处生效，改目录时要一起改：

| 调用点 | include |
|---|---|
| `compat/AetherKiriCompanions.cpp` | `io/storage/IoVirtualFile.h` |
| `compat/CompatLayer.cpp` | `io/policy/IoPolicy.h` |
| `compat/CompatLayer.h` | `io/policy/StoragePolicy.h` |
| `plugin/PluginImpl.cpp` | `io/path/IoModuleLocator.h` |

`storage/IoVirtualFile.h` 自己用的是 `base/core/StorageIntf.h` —— 这是仓库里
第一条"带模块前缀"的 include，新代码照此写。

## 验收（有编译器之后）

```bash
scripts/check_tu.sh --module io
bash scripts/check_static.sh
llvm-nm -C --defined-only libengine_api.so | grep 'krkr::io::'
```

行为对齐点：XP3 挂载顺序 / `patch.xp3` 优先级 / 段缓存命中 /
`TVPIsExistentStorage` 的模块查询 —— 四个簇的搬迁不改变任何一条，
出现差异说明是 include 路径问题，不是逻辑问题。
