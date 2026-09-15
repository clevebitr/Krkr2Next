#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 把 Live2D Cubism SDK for Native 还原到 cpp/plugins/cubism/。
#
# 为什么要这个脚本：SDK 是专有构件，README「硬约束 6」要求它**不入库**
# （cpp/plugins/cubism/{Framework,Core/lib} 已 gitignore）。于是 CI 机器上永远没有它，
# 而拿不到 SDK 的后果在真机上只表现为一句 "krkrlive2d.dll Failed"，很难反查到构建配置。
# 这个脚本把"怎么拿到"收敛成一条路径，并且区分两种情况：
#
#   * 没配置来源   -> 打印警告后**成功退出**（硬约束 6：缺 SDK 不得阻断核心构建）；
#   * 配了却失败   -> **硬失败**，不允许静默降级成一个没有 Live2D 的 APK。
#
# 用法（CI 由 .github/workflows/android_build.yml 调用，也可本地跑）：
#
#   环境变量（二选一）：
#     CUBISM_SDK_GH      私有仓库 owner/repo，从它的 release asset 取（推荐）
#     CUBISM_SDK_URL     直链（自建主机 / 其它对象存储）
#   可选：
#     CUBISM_SDK_TAG     release tag，默认 latest
#     CUBISM_SDK_ASSET   asset 文件名匹配，默认 cubism*.zip
#     CUBISM_SDK_TOKEN   GitHub token（私有仓库必需）/ Bearer token（直链方式可选）
#     CUBISM_SDK_SHA256  下载后校验；给了就必须匹配
#     CUBISM_SDK_DEST    目标目录，默认 cpp/plugins/cubism
#
#   bash scripts/restore_cubism_sdk.sh
#
# 期望的归档内容（官方 SDK 解出来的样子即可，脚本自己找，不要求固定顶层目录名）：
#   <任意前缀>/Core/include/Live2DCubismCore.h
#   <任意前缀>/Core/lib/android/arm64-v8a/libLive2DCubismCore.a
#   <任意前缀>/Framework/src/CubismFramework.hpp
# ---------------------------------------------------------------------------
set -euo pipefail

DEST="${CUBISM_SDK_DEST:-cpp/plugins/cubism}"
GH_REPO="${CUBISM_SDK_GH:-}"
URL="${CUBISM_SDK_URL:-}"
TAG="${CUBISM_SDK_TAG:-latest}"
ASSET="${CUBISM_SDK_ASSET:-cubism*.zip}"
TOKEN="${CUBISM_SDK_TOKEN:-}"
WANT_SHA="${CUBISM_SDK_SHA256:-}"

warn_ci() { # GitHub Actions 的 ::warning:: 注解；非 CI 环境当普通输出
    echo "::warning::$*" 2>/dev/null || true
    echo "警告：$*"
}

# ── 0. 没配置来源：警告后成功退出 ──────────────────────────────────────────
if [[ -z "$GH_REPO" && -z "$URL" ]]; then
    warn_ci "未配置 CUBISM_SDK_GH / CUBISM_SDK_URL —— 本轮不还原 Live2D SDK，产出的 APK 不含 Live2D（G2 那类全动画作品会黑屏）。"
    echo "  取 SDK：https://www.live2d.com/sdk/download/native/ （需接受其许可）"
    echo "  放 CI ：建一个私有仓库，把 SDK 压成 zip 传成 release asset，然后设置"
    echo "          CUBISM_SDK_GH=owner/repo 与 CUBISM_SDK_TOKEN（只读 contents 的 fine-grained PAT）。"
    echo "  放哪 ：Settings -> Secrets and variables -> Actions 里 **Variables 与 Secrets 两个页签都认**，"
    echo "          但 token 只能放 Secrets（仓库公开时变量是明文）。"
    exit 0
fi

# 常见配错：把仓库地址当成直链填进 CUBISM_SDK_URL。那种 URL 返回的是 HTML 页面，
# 后面只会以"解包失败"收场，看不出真正原因，所以在这里直接点破。
if [[ -z "$GH_REPO" && "$URL" =~ ^https?://(www\.)?github\.com/[^/]+/[^/?#]+/?$ ]]; then
    echo "✗ CUBISM_SDK_URL 看起来是 GitHub 的**仓库地址**，不是文件直链：$URL" >&2
    echo "  要走 GitHub release asset，请改用 CUBISM_SDK_GH=owner/repo（+ CUBISM_SDK_TOKEN）；" >&2
    echo "  或者把 CUBISM_SDK_URL 填成能直接下到 zip 的地址（自建主机 / 对象存储）。" >&2
    exit 1
fi

# 两个都配时 GH 优先（gh 处理私有 release asset 的鉴权与 302 最稳），但要说一声，
# 免得有人以为改 URL 生效了。
if [[ -n "$GH_REPO" && -n "$URL" ]]; then
    echo "提示：CUBISM_SDK_GH 与 CUBISM_SDK_URL 都配置了，本次用 GH=$GH_REPO（URL 被忽略）。"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── 1. 取归档 ──────────────────────────────────────────────────────────────
ZIP="$WORK/sdk.zip"
if [[ -n "$GH_REPO" ]]; then
    echo "从 GitHub release asset 取 SDK：$GH_REPO ($TAG, 匹配 $ASSET)"
    if ! command -v gh >/dev/null 2>&1; then
        echo "✗ 找不到 gh CLI；GitHub 托管 runner 自带，本地请先安装或改用 CUBISM_SDK_URL。" >&2
        exit 1
    fi
    # gh 自己处理 release asset 的鉴权与 302 跳转——用 curl 直连 asset URL 时
    # Authorization 头会被带到 S3 重定向目标上，S3 见到多余的头会直接 400。
    if [[ -n "$TOKEN" ]]; then
        export GH_TOKEN="$TOKEN"
    fi
    # tag 留空或写成 latest 时**不能**把 "latest" 当 tag 传进去——`gh release
    # download` 只认真实 tag，不带 tag 参数才是"取最新 release"。
    TAG_ARGS=()
    if [[ -n "$TAG" && "$TAG" != "latest" ]]; then
        TAG_ARGS=("$TAG")
    fi
    if ! gh release download "${TAG_ARGS[@]}" -R "$GH_REPO" -p "$ASSET" -O "$ZIP" --clobber; then
        echo "✗ 下载 release asset 失败：$GH_REPO tag=${TAG:-(最新)} pattern=$ASSET" >&2
        echo "  核对：仓库/tag 是否存在、asset 名是否匹配、PAT 是否有该私有仓库的 contents:read。" >&2
        exit 1
    fi
else
    echo "从直链取 SDK：$URL"
    # 两段式：先用带 Authorization 的请求拿 302 的 Location，再不带凭据去取。
    # 这样既能过私有对象的鉴权，又不会把 Authorization 转给存储后端。
    HDRS="$WORK/hdr.txt"
    AUTH=()
    [[ -n "$TOKEN" ]] && AUTH=(-H "Authorization: Bearer $TOKEN")
    if ! curl -fsSL --retry 3 --retry-delay 2 -D "$HDRS" -o /dev/null "${AUTH[@]}" "$URL"; then
        echo "✗ 直链请求失败：$URL" >&2
        exit 1
    fi
    LOC="$(awk 'BEGIN{IGNORECASE=1} /^location:/{sub(/^location:[ \t]*/,""); sub(/\r$/,""); print; exit}' "$HDRS")"
    if [[ -n "$LOC" ]]; then
        curl -fsSL --retry 3 --retry-delay 2 -o "$ZIP" "$LOC"
    else
        curl -fsSL --retry 3 --retry-delay 2 -o "$ZIP" "${AUTH[@]}" "$URL"
    fi
fi

[[ -s "$ZIP" ]] || { echo "✗ 下载到的归档是空的" >&2; exit 1; }
echo "  归档 $(stat -c%s "$ZIP") 字节"

if [[ -n "$WANT_SHA" ]]; then
    GOT="$(sha256sum "$ZIP" | cut -d' ' -f1)"
    if [[ "$GOT" != "$WANT_SHA" ]]; then
        echo "✗ SHA256 不匹配：期望 $WANT_SHA，实际 $GOT" >&2
        exit 1
    fi
    echo "  SHA256 校验通过"
fi

# ── 2. 解包 ────────────────────────────────────────────────────────────────
if ! unzip -q "$ZIP" -d "$WORK/x"; then
    echo "✗ 解包失败：只支持 zip。请把 SDK 压成 zip 再上传。" >&2
    exit 1
fi

# ── 3. 定位三项关键内容（不假设固定的顶层目录名）────────────────────────────
FW_HPP="$(find "$WORK/x" -name CubismFramework.hpp -print -quit)"
CORE_H="$(find "$WORK/x" -name Live2DCubismCore.h -print -quit)"
CORE_A="$(find "$WORK/x" -name 'libLive2DCubismCore.a' -print -quit)"

for pair in "CubismFramework.hpp:$FW_HPP" "Live2DCubismCore.h:$CORE_H" "libLive2DCubismCore.a:$CORE_A"; do
    if [[ -z "${pair#*:}" ]]; then
        echo "✗ 归档里找不到 ${pair%%:*} —— 不像是 Cubism SDK for Native 的完整解包目录。" >&2
        echo "  归档顶层内容：" >&2
        find "$WORK/x" -maxdepth 2 -mindepth 1 | head -20 >&2
        exit 1
    fi
done

# Framework/src/* 整体落到 Framework/ 下：krkrlive2d.cpp 用 #include "CubismFramework.hpp"
# 这种相对 src 的写法，而 CMake 把 ${CUBISM_FW_DIR} 直接加进 include 路径，
# 所以 Framework/ 必须**等于** SDK 的 Framework/src 内容。
FW_SRC_DIR="$(dirname "$FW_HPP")"
CORE_INC_DIR="$(dirname "$CORE_H")"
# Core/lib 取 libLive2DCubismCore.a 往上找到名为 lib 的那一层，保留 <abi>/ 子目录结构
CORE_LIB_DIR=""
d="$(dirname "$CORE_A")"
while [[ "$d" != "/" ]]; do
    if [[ "$(basename "$d")" == "lib" ]]; then CORE_LIB_DIR="$d"; break; fi
    d="$(dirname "$d")"
done
[[ -n "$CORE_LIB_DIR" ]] || { echo "✗ 找不到 Core/lib 层级（在 $CORE_A 之上）" >&2; exit 1; }

echo "  Framework/src -> $FW_SRC_DIR"
echo "  Core/include  -> $CORE_INC_DIR"
echo "  Core/lib      -> $CORE_LIB_DIR"

# ── 4. 落盘 ────────────────────────────────────────────────────────────────
# Core/include 也要从 SDK 覆盖一遍：仓库里跟踪的 Core/include/Live2DCubismCore.h 是
# 早期 SDK 版本的头（实测与 5-r.5 差 1300 多行），而**头必须和静态库同版本**，
# 否则只是一堆看不懂的链接期/运行期不符。所以这里刻意覆盖它——本步骤跑完后
# git status 会显示该文件被修改，那是预期的，不要 commit 回去。
mkdir -p "$DEST/Core/include" "$DEST/Core/lib" "$DEST/Framework"
rm -rf "$DEST/Framework"/* "$DEST/Core/lib"/*
cp -a "$CORE_INC_DIR"/. "$DEST/Core/include"/
cp -a "$CORE_LIB_DIR"/. "$DEST/Core/lib"/
cp -a "$FW_SRC_DIR"/. "$DEST/Framework"/

# ── 5. 校验落到"CMake 真能用"的形状 ────────────────────────────────────────
fail=0
if [[ ! -f "$DEST/Framework/CubismFramework.hpp" ]]; then
    echo "✗ $DEST/Framework/CubismFramework.hpp 不在位（include 路径会解析不到）" >&2; fail=1
fi
FW_CPP="$(find "$DEST/Framework" -name '*.cpp' | wc -l)"
if (( FW_CPP < 10 )); then
    echo "✗ $DEST/Framework 下只找到 $FW_CPP 个 .cpp —— Framework 没放全" >&2; fail=1
fi
if [[ -z "$(find "$DEST/Core/lib" -name 'libLive2DCubismCore.a' -print -quit)" ]]; then
    echo "✗ $DEST/Core/lib 下没有 libLive2DCubismCore.a" >&2; fail=1
fi
# 本项目只出 arm64-v8a（见 app/app/build.gradle.kts 的 abiFilters），CMake 也只按
# ANDROID_ABI 找这一份。没有它就会静默跳过 Live2D，所以这里提前说清楚。
if [[ -z "$(find "$DEST/Core/lib" -path '*arm64-v8a*' -name 'libLive2DCubismCore.a' -print -quit)" ]]; then
    echo "✗ Core/lib 下没有 arm64-v8a 的 libLive2DCubismCore.a（本工程只出 arm64-v8a）" >&2
    echo "  现有：" >&2
    find "$DEST/Core/lib" -name 'libLive2DCubismCore.a' >&2
    fail=1
fi
(( fail == 0 )) || exit 1

echo "✓ Live2D SDK 已还原到 $DEST"
echo "  Framework 源文件 $FW_CPP 个，Core 静态库："
find "$DEST/Core/lib" -name 'libLive2DCubismCore.a' -exec ls -lh {} \; | sed 's/^/    /'
