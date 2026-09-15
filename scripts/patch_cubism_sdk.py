#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# 给还原出来的官方 Cubism SDK for Native 打上"逐 drawable 强制隐藏"扩展。
#
# 为什么需要它：cpp/plugins/krkrlive2d.cpp 用了两个**官方 SDK 没有**的方法
#    void CubismModel::SetDrawableForceHidden(csmInt32, csmBool)
#    void CubismModel::ClearDrawableForceHiddenFlags()
# 它们来自 KiriKiri-LauncherC 的**修补版** Framework（KrKr2-Next / NextScene 等
# 能用的 fork 都是直接 vendor 那份修补版）。本仓库的 scripts/restore_cubism_sdk.sh
# 还原的是**官方原版**，因此这两个调用在 CI 上必然编不过——这正是 2026-09-15
# 那轮 CI 失败的原因（此前 CI 一直拿不到 SDK，krkrlive2d.cpp 从未参与编译，
# 所以问题一直藏着，头一次拿到 SDK 就炸）。
#
# 为什么不用别的办法：
#   * SDK 是专有构件、不入库（README 硬约束 6），本机 cpp/plugins/cubism/ 是
#     gitignore 的，直接改那份**既不会被共享也不会进 CI**；
#   * 官方 Core 的 csmGetDrawableOpacities() 返回 const float*，不能写；
#     CubismModel 也没有 SetDrawableOpacity，所以插件侧没有公开 API 能替代
#     "让某个 drawable 不可见"。
# 于是补丁落在"还原 SDK"这一步（该脚本是 SDK 来源的唯一入口），改完本机与 CI 同时生效。
#
# 幂等：已打过直接返回 0。锚点对不上（官方 SDK 换版本了）就**硬失败**，
# 绝不放行一个"看起来能编、实际没有扩展"的 SDK。
#
# 用法：python3 scripts/patch_cubism_sdk.py [cpp/plugins/cubism]
# ---------------------------------------------------------------------------
import sys
from pathlib import Path

# ── 锚点与插入内容：逐字对齐修补版 Framework（来源见文件头）────────────────
HPP_ANCHOR_VISIBLE = """    csmBool GetDrawableDynamicFlagIsVisible(csmInt32 drawableIndex) const;
"""
HPP_INSERT_VISIBLE = """    csmBool GetDrawableDynamicFlagIsVisible(csmInt32 drawableIndex) const;

    /**
     * Sets an engine-side visibility override for a drawable.
     */
    void SetDrawableForceHidden(csmInt32 drawableIndex, csmBool hidden);

    /**
     * Clears all engine-side drawable visibility overrides.
     */
    void ClearDrawableForceHiddenFlags();
"""

HPP_ANCHOR_MEMBER = """    csmVector<CubismIdHandle> _drawableIds;
"""
HPP_INSERT_MEMBER = """    csmVector<CubismIdHandle> _drawableIds;
    csmVector<csmBool> _drawableForceHidden;
"""

# GetDrawableDynamicFlagIsVisible() 要先看强制隐藏标志，再看 Core 的动态标志。
CPP_ANCHOR_VISIBLE = """csmBool CubismModel::GetDrawableDynamicFlagIsVisible(csmInt32 drawableIndex) const
{
    const Core::csmFlags* dynamicFlags = Core::csmGetDrawableDynamicFlags(_model);
"""
CPP_INSERT_VISIBLE = """csmBool CubismModel::GetDrawableDynamicFlagIsVisible(csmInt32 drawableIndex) const
{
    if (drawableIndex >= 0 &&
        drawableIndex < static_cast<csmInt32>(_drawableForceHidden.GetSize()) &&
        _drawableForceHidden[drawableIndex])
    {
        return false;
    }
    const Core::csmFlags* dynamicFlags = Core::csmGetDrawableDynamicFlags(_model);
"""

# 实现放在 VisibilityDidChange 之前，与修补版的顺序一致。锚点前面多带一个换行，
# 好让新函数与它之间空一行（原文件里 VisibilityDidChange 前本来就有空行）。
CPP_ANCHOR_IMPL = """
csmBool CubismModel::GetDrawableDynamicFlagVisibilityDidChange(csmInt32 drawableIndex) const
"""
CPP_INSERT_IMPL = """void CubismModel::SetDrawableForceHidden(csmInt32 drawableIndex, csmBool hidden)
{
    if (drawableIndex < 0 || drawableIndex >= GetDrawableCount())
    {
        return;
    }
    if (_drawableForceHidden.GetSize() < static_cast<csmUint32>(GetDrawableCount()))
    {
        _drawableForceHidden.UpdateSize(GetDrawableCount(), false, false);
    }
    _drawableForceHidden[drawableIndex] = hidden;
}

void CubismModel::ClearDrawableForceHiddenFlags()
{
    _drawableForceHidden.Clear();
}

csmBool CubismModel::GetDrawableDynamicFlagVisibilityDidChange(csmInt32 drawableIndex) const
"""

PATCHED_MARK = "ClearDrawableForceHiddenFlags"


def splice(path: Path, text: str, anchor: str, insert: str) -> str:
    """把 insert 顶到 anchor 前面；anchor 必须唯一出现。"""
    if text.count(anchor) != 1:
        raise SystemExit(
            "✗ %s 的锚点出现 %d 次（期望 1 次）——SDK 版本与补丁不匹配：\n  %s"
            % (path.name, text.count(anchor), anchor.strip().splitlines()[0])
        )
    return text.replace(anchor, insert)


def precheck(path: Path, text: str, anchors) -> None:
    for anchor in anchors:
        if text.count(anchor) != 1:
            raise SystemExit(
                "✗ %s 的锚点出现 %d 次（期望 1 次）——SDK 版本与补丁不匹配：\n  %s"
                % (path.name, text.count(anchor), anchor.strip().splitlines()[0])
            )


def main() -> int:
    dest = Path(sys.argv[1] if len(sys.argv) > 1 else "cpp/plugins/cubism")
    hpp = dest / "Framework/Model/CubismModel.hpp"
    cpp = dest / "Framework/Model/CubismModel.cpp"

    for p in (hpp, cpp):
        if not p.is_file():
            print(
                "✗ 找不到 %s —— 先跑 scripts/restore_cubism_sdk.sh 还原 SDK" % p,
                file=sys.stderr,
            )
            return 1

    hpp_text = hpp.read_text(encoding="utf-8")
    cpp_text = cpp.read_text(encoding="utf-8")

    # 幂等：声明与实现都在就算打过
    if PATCHED_MARK in hpp_text and "SetDrawableForceHidden" in cpp_text:
        print("  Cubism 扩展补丁已在位（跳过）")
        return 0

    # 写盘前把四处锚点全验一遍，避免改一半留下坏文件
    precheck(hpp, hpp_text, (HPP_ANCHOR_VISIBLE, HPP_ANCHOR_MEMBER))
    precheck(cpp, cpp_text, (CPP_ANCHOR_VISIBLE, CPP_ANCHOR_IMPL))

    hpp_text = splice(hpp, hpp_text, HPP_ANCHOR_VISIBLE, HPP_INSERT_VISIBLE)
    hpp_text = splice(hpp, hpp_text, HPP_ANCHOR_MEMBER, HPP_INSERT_MEMBER)
    cpp_text = splice(cpp, cpp_text, CPP_ANCHOR_VISIBLE, CPP_INSERT_VISIBLE)
    cpp_text = splice(cpp, cpp_text, CPP_ANCHOR_IMPL, CPP_INSERT_IMPL)

    hpp.write_text(hpp_text, encoding="utf-8")
    cpp.write_text(cpp_text, encoding="utf-8")

    print("  Cubism 扩展补丁已应用：SetDrawableForceHidden / ClearDrawableForceHiddenFlags")
    return 0


if __name__ == "__main__":
    sys.exit(main())
