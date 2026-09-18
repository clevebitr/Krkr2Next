package org.dpdns.clevebitr.core

/**
 * 运行模式：**兼容层与渲染器设置的固定组合**。
 *
 * ## 为什么合成一个选项，而不是给两个独立旋钮
 *
 * 引擎侧其实有两个开关（`game_compat_profile` 与 `ogldrawdevice_compat`），但它们的
 * **合法组合很少**：档位本身就会映射出一个 OGLDrawDevice 模式，两边都手动指定时
 * 谁赢还取决于下发顺序（引擎里是"显式选项优先"）。实测出现过一堆只在纸面上成立的
 * 组合（例如 `krkrz-ogl` 配 `kag`），选出来的效果既不是 A 也不是 B，用户无法描述、
 * 我们也无法复现。
 *
 * 所以壳侧只暴露**几个试过的组合**，把两个值当成一对儿下发。想加新组合就在这里加一条，
 * 而不是让用户在两个下拉框里自由组合。
 *
 * ## 与引擎的关系
 *
 * [compatProfile] / [oglDrawDeviceCompat] 就是网关下发的两个原始值；[AUTO] 让引擎按
 * 游戏目录里的血脉标记自己判（`krkrgles.dll`/`krkrlive2d.dll` → GPU；
 * `motionplayer*.dll` → KAG），这也是默认值。
 */
enum class RunMode(
    val key: String,
    val label: String,
    val summary: String,
    val compatProfile: String,
    val oglDrawDeviceCompat: String,
) {
    AUTO(
        key = "auto",
        label = "逐游戏自动",
        summary = "按游戏目录的插件标记自动判档（krkrgles/Live2D → GPU，motionplayer → KAG）",
        compatProfile = "auto",
        oglDrawDeviceCompat = "off",
    ),

    CLASSIC(
        key = "classic",
        label = "经典（兼容优先）",
        summary = "走 KiriKiri2 老路线，不挂任何 GPU 扩展；画面最保守",
        compatProfile = "kirikiri2-classic",
        oglDrawDeviceCompat = "off",
    ),

    GPU(
        key = "gpu",
        label = "krkrz GPU",
        summary = "挂 Window.OGLDrawDevice + GLESAdaptor（krkrz 系的 GPU 层脚本路径）",
        compatProfile = "krkrz-gpu",
        oglDrawDeviceCompat = "alias",
    ),

    KAG(
        key = "kag",
        label = "KAG 接管",
        summary = "在 GPU 之上接管 KAGWindow 的绘制设备；G2/千恋万花实测可用",
        compatProfile = "krkrz-kag",
        oglDrawDeviceCompat = "kag",
    ),

    OGL(
        key = "ogl",
        label = "仅 OGLDrawDevice",
        summary = "只挂 OGLDrawDevice，不起 GLESAdaptor（GLESAdaptor 会改变部分游戏的绘制路径）",
        compatProfile = "krkrz-ogl",
        oglDrawDeviceCompat = "ogl",
    ),

    /**
     * AetherKiri 兼容层：唯一会切换**兼容层**（而不只是渲染选项）的模式。
     *
     * 缺省（[AUTO] 与其它模式）一律是旧版 krkr2 层；这一档才把整层行为切到
     * AetherKiri 口径（脚本前奏、加载策略、插件注册集合，见 `compat/README.md`）。
     * 属于实验档：旧层已跑通的游戏不要选，需要逐游戏回归。
     */
    AETHERKIRI(
        key = "aetherkiri",
        label = "AetherKiri 兼容层",
        summary = "AetherKiri 口径的脚本/加载/插件行为（实验档，按游戏单独开）",
        compatProfile = "aetherkiri",
        oglDrawDeviceCompat = "kag",
    );

    companion object {
        val DEFAULT: RunMode = AUTO

        fun fromKey(key: String?): RunMode = entries.firstOrNull { it.key == key } ?: DEFAULT

        /**
         * 由两个原始值反推模式。**读旧配置用**：`krkr2next.json` 里可能只写了
         * `compatProfile`/`oglDrawDeviceCompat`（本类型引入之前写的），这时按档名匹配；
         * 匹配不上（用户手改过、或旧版本留下的怪组合）就退回 [AUTO]，并在下次保存时
         * 归一化成一条合法组合。
         */
        fun fromConfig(compatProfile: String?, oglDrawDeviceCompat: String?): RunMode {
            if (compatProfile == null && oglDrawDeviceCompat == null) return DEFAULT
            if (compatProfile == null || compatProfile == "auto") return AUTO
            return entries.firstOrNull { it.compatProfile == compatProfile }
                ?: entries.firstOrNull { it.oglDrawDeviceCompat == oglDrawDeviceCompat }
                ?: DEFAULT
        }
    }
}

/** 运行模式 → 两个引擎选项。壳只通过它下发，避免各处自己拼。 */
fun RunMode.asEngineOverride(): EngineOverride = EngineOverride(
    compatProfile = compatProfile,
    oglDrawDeviceCompat = oglDrawDeviceCompat,
)
