package org.dpdns.clevebitr.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil3.compose.AsyncImage
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.scrape.ScoredCandidate
import org.dpdns.clevebitr.core.scrape.ScrapeException
import org.dpdns.clevebitr.core.scrape.ScrapeService
import org.dpdns.clevebitr.core.scrape.TitleMatch

/**
 * 刮削候选选择页。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ScrapeScreen(
    game: LibraryGame,
    onApply: (ScoredCandidate) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var query by remember(game.id) {
        mutableStateOf(TitleMatch.guessQuery(game.title, game.dir.name))
    }
    var loading by remember(game.id) { mutableStateOf(false) }
    var error by remember(game.id) { mutableStateOf<String?>(null) }
    var candidates by remember(game.id) { mutableStateOf<List<ScoredCandidate>>(emptyList()) }
    var searchedQuery by remember(game.id) { mutableStateOf<String?>(null) }

    // 查询由一个自增的 tick 驱动：进入页面自动搜一次（tick=0），之后"搜索"与
    // "重试"都只是 tick+1。这样只有一条请求路径，超时/错误处理不会分叉。
    // 不直接依赖 query：否则边打字边发请求。
    var searchTick by remember(game.id) { mutableStateOf(0) }

    LaunchedEffect(game.id, searchTick) {
        if (query.isBlank()) return@LaunchedEffect
        loading = true
        error = null
        try {
            candidates = ScrapeService.search(query, game.dir.name)
            searchedQuery = query
        } catch (e: ScrapeException) {
            error = e.message ?: "未知错误"
        } catch (t: Throwable) {
            error = t.message ?: t.javaClass.simpleName
        } finally {
            loading = false
        }
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("刮削信息") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
            )
        },
    ) { padding ->
        Column(modifier = Modifier.padding(padding).fillMaxSize()) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    label = { Text("搜索关键词（VNDB）") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                Button(
                    onClick = { if (query.isNotBlank()) searchTick += 1 },
                    enabled = !loading && query.isNotBlank(),
                    modifier = Modifier.padding(start = 8.dp),
                ) {
                    Icon(Icons.Filled.Search, contentDescription = null)
                    Text("搜索", modifier = Modifier.padding(start = 4.dp))
                }
            }

            // 关键词改了但还没重搜：把按钮点亮这件事交给用户，这里只负责提示
            if (searchedQuery != null && searchedQuery != query && !loading) {
                Text(
                    text = "关键词已改，点「搜索」重新查询。",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 16.dp, top = 4.dp),
                )
            }

            if (!loading && error == null && searchedQuery == null && query.isBlank()) {
                Text(
                    text = "请输入关键词。目录名里带汉化组前缀时，直接填游戏本名通常更准。",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(16.dp),
                )
            }

            when {
                loading -> Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center,
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        CircularProgressIndicator()
                        Text(
                            text = "正在查询 VNDB…",
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(top = 12.dp),
                        )
                    }
                }

                error != null -> Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        text = "刮削失败",
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.error,
                    )
                    Text(text = error.orEmpty(), style = MaterialTheme.typography.bodySmall)
                    OutlinedButton(
                        onClick = { searchTick += 1 },
                        modifier = Modifier.padding(top = 12.dp),
                    ) {
                        Text("重试")
                    }
                }

                candidates.isEmpty() && searchedQuery != null -> Text(
                    text = "没有找到候选。试试只填标题里最独特的那几个字。",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(16.dp),
                )

                else -> LazyColumn(modifier = Modifier.fillMaxSize()) {
                    items(candidates, key = { it.vn.id }) { candidate ->
                        CandidateRow(candidate = candidate, onPick = { onApply(candidate) })
                    }
                }
            }
        }
    }
}

@Composable
private fun CandidateRow(candidate: ScoredCandidate, onPick: () -> Unit) {
    val vn = candidate.vn
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onPick)
            .padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val thumb = vn.thumbnailUrl.ifBlank { vn.imageUrl }
        if (thumb.isNotBlank()) {
            AsyncImage(
                model = thumb,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier
                    .size(width = 64.dp, height = 88.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant),
            )
        } else {
            Box(
                modifier = Modifier
                    .size(width = 64.dp, height = 88.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(MaterialTheme.colorScheme.surfaceVariant),
            ) {
                Image(
                    imageVector = Icons.Filled.Search,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp).align(Alignment.Center),
                )
            }
        }
        Column(modifier = Modifier.padding(start = 12.dp)) {
            Text(
                text = vn.displayTitle(),
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            val meta = listOfNotNull(
                vn.developers.joinToString("、").takeIf { it.isNotBlank() },
                vn.released.takeIf { it.isNotBlank() },
            ).joinToString(" · ")
            if (meta.isNotBlank()) {
                Text(
                    text = meta,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Text(
                text = "匹配度 ${candidate.percent}%" +
                    if (candidate.score >= 0.9) "（很可能是它）" else "",
                style = MaterialTheme.typography.labelSmall,
                color = if (candidate.score >= 0.9) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
        }
    }
}
