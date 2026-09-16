package org.dpdns.clevebitr.core.scrape

import java.text.Normalizer

/** 候选 + 匹配度（0–1）。 */
data class ScoredCandidate(val vn: VndbVn, val score: Double) {
    /** UI 显示用的百分比整数。 */
    val percent: Int get() = (score * 100).toInt().coerceIn(0, 100)
}

/**
 * 标题匹配：把"目录名/用户输入"与 VNDB 候选对起来，并给出置信度。
 *
 * ## 为什么要自己算
 *
 * PocketKrKr 直接把 VNDB 的返回顺序当结果顺序（无归一化、无排序、无置信度），
 * 目录名与条目标题差一点（`千恋＊万花` vs `千恋＊万花` 的全角星号、`-汉化版` 后缀、
 * `【KRKR汉化高压】` 前缀）时用户只能自己在 20 条里找。这里做三件事：
 *
 * 1. **归一化**：NFKC（全角→半角，日文标题里 ＊／？／！ 很常见）、去括号段、
 *    去常见版本/汉化标记、去掉所有非字母数字（保留假名与汉字）。
 * 2. **相似度**：字符二元组 Dice 系数。它对语种不敏感——日文、中文、拉丁标题
 *    走同一套逻辑，不需要为 CJK 分词。
 * 3. **多来源取最大**：用户输入、目录名各算一次，候选的 `title` 与 `alttitle` 各算一次，
 *    取最大值；再按"年份命中"和 VNDB 评分做小幅加成。
 *
 * 分数只用于**排序与提示**，不做自动选择：猜错了让用户改一条的成本，远低于自动填错
 * 之后他要在一堆字段里找出哪条是错的。
 */
object TitleMatch {

    /** 参与匹配前先丢掉的词：版本、语言、发行形态。只影响匹配，不影响标题展示。 */
    private val NOISE_WORDS = listOf(
        "汉化版", "汉化", "中文版", "官中", "机翻", "完全版", "通常版", "初回版", "限定版",
        "体験版", "体验版", "trial", "fandisc", "fan disc", "dl版", "dl edition",
        "remaster", "remastered", "hd version", "hd版", "full voice", "フルボイス",
    )

    /** 括号段一律不参与匹配（`【KRKR汉化高压】`、`（2019）`、`[HD]` 等）。 */
    private val BRACKET_SEGMENT = Regex("[（(\\[【〈《][^）)\\]】〉》]*[）)\\]】〉》]")

    /**
     * 归一化。步骤顺序固定，因为每一步都依赖前一步的结果：
     * NFKC 之后全角括号才变成半角，括号正则才吃得干净。
     */
    fun normalize(raw: String): String {
        if (raw.isBlank()) return ""
        var text = Normalizer.normalize(raw, Normalizer.Form.NFKC).lowercase()
        text = BRACKET_SEGMENT.replace(text, " ")
        for (word in NOISE_WORDS) {
            text = text.replace(word, " ")
        }
        return text.filter { it.isLetterOrDigit() }
    }

    /**
     * 相似度，0–1。分级而不是纯 Dice：
     * 完全相等 = 1；一方是另一方的前缀（够长）= 0.9；其余按 Dice 折算到 0–0.85。
     *
     * 前缀单独判是有原因的：VNDB 的 `alttitle` 常带副标题（`千恋＊万花 - 体験版`），
     * 纯 Dice 会把它压到 0.6 以下，与不相关的条目混在一起。
     */
    fun similarity(a: String, b: String): Double {
        if (a.isEmpty() || b.isEmpty()) return 0.0
        if (a == b) return 1.0
        val shorter = if (a.length <= b.length) a else b
        val longer = if (a.length <= b.length) b else a
        if (shorter.length >= 4 && longer.startsWith(shorter)) return 0.9
        if (longer.contains(shorter) && shorter.length >= 5) return 0.85
        return 0.85 * diceBigrams(a, b)
    }

    /** 字符二元组 Dice 系数；长度 1 的串退化成单字符比较。 */
    private fun diceBigrams(a: String, b: String): Double {
        if (a.length < 2 || b.length < 2) return if (a == b) 1.0 else 0.0
        val aGrams = HashMap<String, Int>(a.length)
        for (i in 0 until a.length - 1) {
            val g = a.substring(i, i + 2)
            aGrams[g] = (aGrams[g] ?: 0) + 1
        }
        var hits = 0
        for (i in 0 until b.length - 1) {
            val g = b.substring(i, i + 2)
            val n = aGrams[g] ?: 0
            if (n > 0) {
                hits++
                aGrams[g] = n - 1
            }
        }
        return 2.0 * hits / ((a.length - 1) + (b.length - 1))
    }

    /**
     * 排序候选。
     *
     * @param query 用户输入或推断的查询词（用于搜索的那一个）。
     * @param dirName 游戏目录名；与 [query] 不同时也参与打分（目录名常带汉化组前缀）。
     */
    fun rank(query: String, dirName: String?, candidates: List<VndbVn>): List<ScoredCandidate> {
        val queries = LinkedHashSet<String>()
        normalize(query).takeIf { it.isNotEmpty() }?.let { queries += it }
        dirName?.let { normalize(it).takeIf { n -> n.isNotEmpty() }?.let { n -> queries += n } }
        if (queries.isEmpty()) return candidates.map { ScoredCandidate(it, 0.0) }

        val years = YEAR.findAll("$query ${dirName.orEmpty()}").map { it.value }.toSet()

        return candidates.map { vn ->
            val titles = listOf(normalize(vn.title), normalize(vn.altTitle))
                .filter { it.isNotEmpty() }
            var best = 0.0
            for (q in queries) {
                for (t in titles) {
                    val s = similarity(q, t)
                    if (s > best) best = s
                }
            }
            // 加成封顶在 1.0：排序用途，没必要让 0.97 与 0.99 之间的差别变得更玄
            if (years.isNotEmpty() && vn.released.isNotBlank() && years.any { vn.released.startsWith(it) }) {
                best += 0.03
            }
            if (vn.rating >= 80.0) best += 0.02
            ScoredCandidate(vn, best.coerceAtMost(1.0))
        }.sortedWith(compareByDescending<ScoredCandidate> { it.score }.thenByDescending { it.vn.rating })
    }

    /**
     * 从库记录推断一个搜索词：优先标题，其次目录名；都做一次清洗。
     * 清洗只去版本/汉化标记与括号段，不动真正的标题字符。
     */
    fun guessQuery(title: String?, dirName: String?): String {
        val cleaned = listOfNotNull(title, dirName)
            .map { raw ->
                var text = Normalizer.normalize(raw, Normalizer.Form.NFKC)
                text = BRACKET_SEGMENT.replace(text, " ")
                for (word in NOISE_WORDS) text = text.replace(word, " ", ignoreCase = true)
                text.replace(Regex("\\s+"), " ").trim()
            }
            .firstOrNull { it.length >= 2 }
            ?: ""
        return cleaned
    }

    private val YEAR = Regex("(19|20)\\d{2}")
}
