# 上游参照物（已从工作树删除，可随时从 git 取回）

本项目在 `pocketkrkr-base` 标签处保留了 PocketKrKr 的**完整历史**。被删除的 Flutter 壳
代码仍可随时取回，作为行为参照——它是这些功能**唯一已被证明可用的实现**。
不要凭记忆重写，先取回对照。

```bash
git show pocketkrkr-base:<路径>                      # 查看单个文件
git checkout pocketkrkr-base -- <路径>               # 取回到工作树（用完记得删）
git ls-tree -r --name-only pocketkrkr-base apps/     # 列出全部被删文件
```

## 值得取回的实现

| 主题 | 路径（相对 `pocketkrkr-base`） | 价值 |
|---|---|---|
| 输入事件与坐标映射 | `apps/flutter_app/lib/widgets/engine_surface.dart` | 已在 [input-contract.md](input-contract.md) 完整提炼，取回用于交叉核对 |
| VNDB 刮削客户端 | `apps/flutter_app/lib/services/vndb_client.dart` | Kana API 的请求字段、限流、超时、空结果处理 |
| 刮削门面 | `apps/flutter_app/lib/services/game_metadata_scraper.dart` | 搜索 + 封面下载的组合方式（**故意不做持久化**，持久化在别处） |
| 封面下载 | `apps/flutter_app/lib/services/cover_downloader.dart` | **缩略图优先**、浏览器式请求头/Referer、从 URI 或 content-type 推断扩展名、下载到 `<documents>/covers/<source>_<id>.<ext>` |
| 游戏库模型 | `apps/flutter_app/lib/models/game_info.dart` | 字段集：path / title / developer / lastPlayed / coverPath / playDurationSeconds |
| 游戏库持久化 | `apps/flutter_app/lib/services/game_manager.dart` | SharedPreferences 键 `krkr2_game_list`、游戏时长会话与崩溃恢复（`pendingPlaySession`、`settledPlaySessionIds`） |
| XP3 纯实现工具 | `apps/flutter_app/lib/utils/xp3_utils.dart` | `xp3Extract`/`xp3Pack`/`xp3List`，自包含不依赖 native，可作算法参照 |
| 刮削候选选择页 | `apps/flutter_app/lib/pages/scrape_select_page.dart` | 交互流程 |

## 注意

- 这些是 **Dart/Flutter** 实现，Kotlin 侧重写时**只借鉴行为与数据结构**，不要照搬 Flutter 特有
  概念（如 `LogicalKeyboardKey`、`devicePixelRatio` 相关的坐标处理——见 input-contract.md 的说明）。
- `game_metadata_scraper.dart` **不做持久化**是刻意设计，不要"顺手补上"。
- 取回文件仅作参照，**不要** `git add` 回仓库（它们是已删代码）。
