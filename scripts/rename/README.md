# scripts/rename —— 契约驱动改名的流程

契约在 [`docs/NAMING.md`](../../docs/NAMING.md)。这里只有"怎么机械地执行它"。

## 一次改名的完整流程（一个模块一个 commit）

```bash
# 1) 起草：从模块里扫出"有定义"的遗留符号，机械去前缀，标出冲突
python3 scripts/rename_symbols.py --draft io --out scripts/rename/maps/io.tsv

# 2) 人工审阅 map（**必做**）
#    · 「冲突」列非空的名字要自己改（例：tTVPRect → Rect 会撞 visual 里已有的 Rect）
#    · 机械去前缀读不通的换成语义名（例：TVPCauseAtInstallExtensionClass →
#      InstallRegisteredClasses，而不是 CauseAtInstallExtensionClass）
#    · 删掉不该改的行（例外见 docs/NAMING.md §4）

# 3) dry-run：看 owner/外部命中数、新名冲突、注释/字符串命中
python3 scripts/rename_symbols.py --map scripts/rename/maps/io.tsv

# 4) 落地
python3 scripts/rename_symbols.py --map scripts/rename/maps/io.tsv --apply

# 5) 验收（三条，见 NAMING §5）
python3 scripts/check_naming.py --module io -v      # 遗留计数 → 0（或只剩例外）
grep -rn "OldName" cpp bridge tools                 # 应为空
git diff --stat                                     # 只该有"确定的文件"

# 6) 基线往下调（只能降）
python3 scripts/check_naming.py --update-baseline
```

## 工具做的与不做的

| 做 | 不做 |
|---|---|
| 按 token 边界替换标识符 | 不做语义改名（名字好不好靠人） |
| owner 内用短名、外部用**全限定名**（`krkr::io::OpenArchive`） | 不推断调用点所在 namespace（全限定总是合法） |
| 删 owner 里的过渡 `using krkr::…::Old;` | 不删别处的桥接（那是调用点迁移任务） |
| 改注释（保留字符串字面量，除非 `--strings`） | 不排序 include、不重排版（`clang-format` 会污染 diff） |
| 幂等：已改过的条目报「✓ 已完成」 | 不自动解决新名冲突 |

## 为什么外部用全限定名

`krkr` 在 `namespace krkr::x` 内部仍然解析到全局的 `::krkr`，所以
`krkr::io::OpenArchive` 在**任何作用域**都合法。这是"没有编译器"时唯一不需要
理解调用点上下文的替换策略；代价是插件侧会出现显式前缀，而这正好是契约 §3
要的效果（旧名字彻底消失，不再靠 `using` 苟活）。

## maps 目录现状

| map | 状态 |
|---|---|
| `extension.tsv` | ✅ 已应用（第 1 轮，试点） |
| `plugin.tsv` `utils.tsv` `io.tsv` `sound.tsv` `environ.tsv` | 🟡 `--draft` 生成，**未审阅**，冲突列需要人工处理 |
| `base.tsv` | ⬜ 未起草（465 个符号，先做非消息部分） |

## 起草模式的统计口径（别误读）

`--draft` 只收**本模块里有定义**的符号；`check_naming.py` 统计的是**模块内出现过的
所有**遗留标识符（含调用别的模块的）。所以 io 的 draft 是 68 个，而基线是 176 ——
差额是 io 里调用 base/visual 的符号，它们会在 base/visual 各自那一轮被改掉。
