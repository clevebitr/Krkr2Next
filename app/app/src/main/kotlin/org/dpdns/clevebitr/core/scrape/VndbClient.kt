package org.dpdns.clevebitr.core.scrape

import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.dpdns.clevebitr.core.AppLog
import org.json.JSONObject

/** 一条 VNDB 视觉小说记录（只取壳用得到的字段）。 */
data class VndbVn(
    val id: String,
    val title: String,
    /** 原文标题（VNDB 的 `alttitle`），日文条目通常是日文名。可为空。 */
    val altTitle: String = "",
    val released: String = "",
    val description: String = "",
    /** 原图 URL，通常 800px 级；封面展示用。 */
    val imageUrl: String = "",
    /** 缩略图 URL，图片挂了时兜底。 */
    val thumbnailUrl: String = "",
    val developers: List<String> = emptyList(),
    val tags: List<String> = emptyList(),
    /** VNDB 评分（0–100），用来在候选并列时做次级排序。 */
    val rating: Double = 0.0,
) {
    /** 标题展示口径：主标题 + （原文标题），两者相同或后者的次要性不明显时不重复。 */
    fun displayTitle(): String =
        if (altTitle.isNotBlank() && !altTitle.equals(title, ignoreCase = true)) {
            "$title（$altTitle）"
        } else {
            title
        }
}

/** 刮削失败的原因。**不再像 PocketKrKr 那样吞成空列表**——UI 需要说出到底怎么了。 */
class ScrapeException(message: String, cause: Throwable? = null) : Exception(message, cause)

/**
 * VNDB 的 Kana API 客户端（`POST https://api.vndb.org/kana/vn`）。
 *
 * ## 与 PocketKrKr 的差别
 *
 * 它只发 `filters` + `fields` + `results`，异常一律吞成 `[]`，导致"数据源不可用"
 * 的文案是死代码（规格 §6 里有据）。这里三点都改：
 * 1. 带 `User-Agent`：VNDB 明确要求能识别调用方，缺 UA 有被限流的风险；
 * 2. 取更多字段（发售日/简介/标签/评分），标题匹配需要它们做候选排序；
 * 3. **异常向上抛** [ScrapeException]，由 UI 决定怎么显示。
 *
 * 网络调用一律 `Dispatchers.IO`；[client] 是进程内共享的，连接池与线程复用。
 */
object VndbClient {

    private const val ENDPOINT = "https://api.vndb.org/kana/vn"
    private const val TAG = "KrKr2Next/Vndb"

    /** VNDB 要求可识别的 UA；顺手把 API 版本写进去，方便以后排查。 */
    private const val USER_AGENT = "KiriNext/0.1 (Android; +https://github.com/clevebitr/Krkr2Next)"

    /**
     * 请求字段。`tags.rating` 用于过滤掉权重极低的标签（见 [applyTags]），
     * `image.thumbnail` 是原图 404 时的兜底——两个 URL 都存下来，下载时再决定用哪个。
     */
    private const val FIELDS =
        "id,title,alttitle,released,description,rating," +
            "image.url,image.thumbnail,developers.name,tags.name,tags.rating"

    private val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
            .callTimeout(30, TimeUnit.SECONDS)
            .build()
    }

    /**
     * 关键词搜索。[query] 为空直接返回空列表（VNDB 的空搜索会返回全库首页，没意义）。
     *
     * @throws ScrapeException 网络错误、HTTP 非 2xx、响应不是预期 JSON。
     */
    suspend fun search(query: String, results: Int = 20): List<VndbVn> {
        val keyword = query.trim()
        if (keyword.isEmpty()) return emptyList()

        val body = JSONObject().apply {
            put("filters", org.json.JSONArray().put("search").put("=").put(keyword))
            put("fields", FIELDS)
            put("results", results)
            // 有 search 过滤时 searchrank 是 VNDB 允许的排序，比默认的相关性更稳
            put("sort", "searchrank")
        }

        val payload = withContext(Dispatchers.IO) {
            val request = Request.Builder()
                .url(ENDPOINT)
                .header("User-Agent", USER_AGENT)
                .header("Accept", "application/json")
                .post(body.toString().toRequestBody(JSON_MEDIA_TYPE))
                .build()
            try {
                client.newCall(request).execute().use { response ->
                    val text = response.body?.string().orEmpty()
                    if (!response.isSuccessful) {
                        throw ScrapeException("VNDB 返回 HTTP ${response.code}：${text.take(200)}")
                    }
                    text
                }
            } catch (e: ScrapeException) {
                throw e
            } catch (t: Throwable) {
                throw ScrapeException("无法连接 VNDB：${t.message ?: t.javaClass.simpleName}", t)
            }
        }

        return try {
            parseResults(payload)
        } catch (e: ScrapeException) {
            throw e
        } catch (t: Throwable) {
            throw ScrapeException("VNDB 响应解析失败：${t.message ?: t.javaClass.simpleName}", t)
        }
    }

    /** 暴露给封面下载用：同一个 client（连接池共享），但方法自己判空 URL。 */
    suspend fun fetchImage(url: String): ByteArray? {
        if (url.isBlank()) return null
        return withContext(Dispatchers.IO) {
            val request = Request.Builder()
                .url(url)
                .header("User-Agent", USER_AGENT)
                // VNDB 的图床对无 Referer 的请求也放行，但带上更稳妥
                .header("Referer", "https://vndb.org/")
                .get()
                .build()
            try {
                client.newCall(request).execute().use { response ->
                    if (!response.isSuccessful) {
                        AppLog.w(TAG, "封面下载失败 HTTP ${response.code}：$url")
                        null
                    } else {
                        response.body?.bytes()
                    }
                }
            } catch (t: Throwable) {
                AppLog.w(TAG, "封面下载异常：$url / $t")
                null
            }
        }
    }

    private fun parseResults(payload: String): List<VndbVn> {
        val root = JSONObject(payload)
        val arr = root.optJSONArray("results") ?: return emptyList()
        val out = ArrayList<VndbVn>(arr.length())
        for (i in 0 until arr.length()) {
            val obj = arr.optJSONObject(i) ?: continue
            out += parseOne(obj)
        }
        return out
    }

    private fun parseOne(obj: JSONObject): VndbVn {
        val image = obj.optJSONObject("image")
        val developers = obj.optJSONArray("developers")?.let { arr ->
            (0 until arr.length()).mapNotNull {
                arr.optJSONObject(it)?.optString("name", "")?.takeIf { n -> n.isNotEmpty() }
            }
        } ?: emptyList()
        return VndbVn(
            id = obj.optString("id", ""),
            title = obj.optString("title", ""),
            altTitle = obj.optString("alttitle", ""),
            released = obj.optString("released", ""),
            description = cleanDescription(obj.optString("description", "")),
            imageUrl = image?.optString("url", "").orEmpty(),
            thumbnailUrl = image?.optString("thumbnail", "").orEmpty(),
            developers = developers,
            tags = selectTags(obj),
            rating = obj.optDouble("rating", 0.0),
        )
    }

    /**
     * 标签：只留 VNDB 权重 ≥ 1.5 的，按权重降序取前 8 个。
     *
     * 不过滤的话一个热门游戏能返回上百个标签（含剧透向的细枝末节），详情页会变成一堵墙。
     */
    private fun selectTags(obj: JSONObject): List<String> {
        val arr = obj.optJSONArray("tags") ?: return emptyList()
        data class Tagged(val name: String, val rating: Double)
        val list = ArrayList<Tagged>(arr.length())
        for (i in 0 until arr.length()) {
            val tag = arr.optJSONObject(i) ?: continue
            val name = tag.optString("name", "").takeIf { it.isNotBlank() } ?: continue
            list += Tagged(name, tag.optDouble("rating", 0.0))
        }
        return list.filter { it.rating >= 1.5 }
            .sortedByDescending { it.rating }
            .take(8)
            .map { it.name }
    }

    /**
     * VNDB 的简介是带标记的文本（`[url=...]`、`[spoiler]`、`[b]` 等）。
     * 叠加层/详情页只显示纯文本，这里把方括号标记剥掉，避免界面上出现一堆 `[url=`。
     */
    private fun cleanDescription(raw: String): String {
        if (raw.isBlank()) return ""
        return raw.replace(Regex("\\[/?[a-zA-Z][^\\]]*\\]"), "")
            .replace("\r\n", "\n")
            .trim()
    }

    private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()
}
