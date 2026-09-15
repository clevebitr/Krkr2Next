#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# 把 Cubism SDK 的 GLES2 着色器源码嵌成 C++ 表，供 krkrlive2d.cpp 注册的
# csmLoadFileFunction 使用。
#
# 为什么必须这么做：Cubism 的 GLES2 渲染器**不内背着色器**——它通过
# CubismFramework::Option::LoadFileFunction 回调向宿主索取，例如
# GenerateShaders() 里调用
#     LoadShaderProgramFromFile("VertShaderSrc.vert", "FragShaderSrc.frag")
# 而 LoadShaderProgramFromFile 里只是
#     csmLoadFileFunction fileLoader = CubismFramework::GetLoadFileFunction();
#     ... fileLoader(vertShaderPath, &size) ...
# 本插件此前只设了 LogFunction / LoggingLevel，**没设 LoadFileFunction**，
# 于是 StartUp() 里那句 `s_option = option;` 存下的是未初始化的栈内容，
# LoadFileFunction 是野指针 → 真机一创建立绘模型就 SIGSEGV，栈顶正是
#     CubismShader_OpenGLES2::LoadShaderProgramFromFile
#     ← GenerateShaders ← GetInstance ← CubismRenderer_OpenGLES2::Initialize
#     ← CubismUserModel::CreateRenderer ← CubismLive2DModel::LoadFromL2D
#
# 为什么用嵌入而不是运行时读文件：着色器要在**任意 GL 线程/堆栈**上被取到，
# 且游戏档案里并不随包提供这些文件（AetherKiri 那份同样没有）。嵌入后与文件
# 系统解耦，也不依赖引擎当前目录。
#
# 取哪一份：`StandardES`（_ES2 变体），与 CMake 定义的 CSM_TARGET_ANDROID_ES2
# 对应。实测 GenerateShaders() 用到的 36 个文件名在 StandardES 下**逐一命中**。
#
# 用法：python3 gen_embedded_shaders.py <Shaders/StandardES 目录> <输出 .cpp>
# ---------------------------------------------------------------------------
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("用法: gen_embedded_shaders.py <shader 目录> <输出 .cpp>", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    out = Path(sys.argv[2])

    if not src.is_dir():
        print("✗ 找不到着色器目录：%s" % src, file=sys.stderr)
        return 1

    files = sorted(p for p in src.iterdir() if p.is_file())
    if not files:
        print("✗ 着色器目录是空的：%s" % src, file=sys.stderr)
        return 1

    # ⚠️ 绝不能给每个文件追加 \0 结尾（曾经加过，正是真机黑屏的根因）。
    #
    # CubismShader_OpenGLES2::LoadShaderProgramFromFile() 会把**多个文件读进同一个
    # csmString 再拼接**：
    #     fragString  = <fragShaderPath> 的内容
    #     fragString += "#define CSM_COLOR_BLEND_MODE n"
    #     fragString += <FragShaderSrcColorBlend.frag> 的内容   ← 定义转换/混合函数
    #     fragString += "#define CSM_ALPHA_BLEND_MODE m"
    #     fragString += <FragShaderSrcAlphaBlend.frag> 的内容
    # 而 CompileShaderSource() 提交时是 `glShaderSource(s, 1, &src, NULL)`——**NULL 长度
    # 意味着按 C 字符串解释，遇到第一个 NUL 就截断**。多出的终止字节会让 #define 与
    # 后拼的两个文件对编译器完全不可见，于是所有混合模式着色器都报
    #     Shader compile log: 0:29/0:30/0:32/0:36: L0002:
    #         Function 'ConvertPremultipliedToStraight' not defined
    # （真机实测 474 次 Fragment shader compile error，顶点着色器 0 次），ShaderProgram
    # 拿不到就整块立绘不绘制 = 黑屏，而脚本与音频照常 → “有声音没画面”。
    #
    # csmString(c, length) 内部自己会补终止符（Copy() 里 `_ptr[length] = 0x0`），所以
    # 这里只给**文件原始字节**，size 也就是文件的真实长度。
    total = 0
    entries = []
    for p in files:
        data = p.read_bytes()
        total += len(data)
        entries.append((p.name, data))

    L = []
    L.append("// 由 scripts/gen_embedded_shaders.py 自动生成，请勿手改。")
    L.append("// 源：Cubism SDK 的 Rendering/OpenGL/Shaders/StandardES/")
    L.append("// 用途：krkrlive2d.cpp 注册的 csmLoadFileFunction（见该脚本头部说明）。")
    L.append("")
    L.append("#include <cstring>")
    L.append("#include <string>")
    L.append("")
    L.append("namespace {")
    L.append("")
    for name, data in entries:
        ident = "".join(ch if ch.isalnum() else "_" for ch in name)
        L.append("const unsigned char kShader_%s[%d] = {" % (ident, len(data)))
        for i in range(0, len(data), 16):
            row = ",".join(str(b) for b in data[i:i + 16])
            L.append("    " + row + ",")
        L.append("};")
        L.append("")
    L.append("struct EmbeddedShader {")
    L.append("    const char *name;")
    L.append("    const unsigned char *data;")
    L.append("    int size;")
    L.append("};")
    L.append("")
    L.append("const EmbeddedShader kEmbeddedShaders[] = {")
    for name, data in entries:
        ident = "".join(ch if ch.isalnum() else "_" for ch in name)
        L.append('    { "%s", kShader_%s, %d },' % (name, ident, len(data)))
    L.append("};")
    L.append("")
    L.append("const int kEmbeddedShaderCount =")
    L.append("    static_cast<int>(sizeof(kEmbeddedShaders) / sizeof(kEmbeddedShaders[0]));")
    L.append("")
    L.append("} // namespace")
    L.append("")
    L.append("// 供 krkrlive2d.cpp 按**文件名**取材；带目录前缀时只取最后一段。")
    L.append("extern \"C\" const unsigned char *KrkrLive2DEmbeddedShader(const char *path,")
    L.append("                                                      int *outSize) {")
    L.append("    if (!path || !outSize)")
    L.append("        return nullptr;")
    L.append("    const char *name = path;")
    L.append("    for (const char *q = path; *q; ++q) {")
    L.append("        if (*q == '/' || *q == '\\\\')")
    L.append("            name = q + 1;")
    L.append("    }")
    L.append("    for (int i = 0; i < kEmbeddedShaderCount; ++i) {")
    L.append("        if (std::strcmp(kEmbeddedShaders[i].name, name) == 0) {")
    L.append("            *outSize = kEmbeddedShaders[i].size;")
    L.append("            return kEmbeddedShaders[i].data;")
    L.append("        }")
    L.append("    }")
    L.append("    return nullptr;")
    L.append("}")
    L.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(L)
    # 只在内容变化时重写，避免每次 configure 都碰时间戳
    if not out.is_file() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8")

    print("  嵌入着色器 %d 个（%d 字节）-> %s" % (len(entries), total, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
