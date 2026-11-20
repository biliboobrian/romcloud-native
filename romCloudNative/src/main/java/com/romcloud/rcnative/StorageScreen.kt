package com.romcloud.rcnative

import android.text.format.Formatter
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private val OtherColor = Color(0xFF5B6170)

@Composable
private fun size(bytes: Long): String = Formatter.formatShortFileSize(LocalContext.current, bytes)

/** Espace de la partition de l'application (sous le titre) ; un clic ouvre la page du stockage. */
@Composable
fun DiskBar(onClick: () -> Unit) {
    val context = LocalContext.current
    val stat = remember { runCatching { android.os.StatFs(context.filesDir.absolutePath) }.getOrNull() } ?: return
    val free = stat.availableBytes
    val total = stat.totalBytes.takeIf { it > 0 } ?: return
    val low = free < total / 10
    Row(
        Modifier.clip(RoundedCornerShape(6.dp)).clickable(onClickLabel = stringResource(R.string.storage_open), onClick = onClick).padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        LinearProgressIndicator(
            progress = { (1f - free.toFloat() / total).coerceIn(0f, 1f) },
            color = if (low) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
            trackColor = MaterialTheme.colorScheme.surfaceVariant,
            drawStopIndicator = {},
            modifier = Modifier.width(72.dp).height(6.dp),
        )
        Text(
            stringResource(R.string.storage_free, size(free)),
            style = MaterialTheme.typography.labelSmall,
            color = if (low) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/**
 * Page du stockage : barre de la partition (données de l'application par catégorie, autres fichiers,
 * espace libre), une barre multicolore par jeu avec sa corbeille, légende des couleurs en bas.
 * [onChanged] : jeux effacés (liste à relire).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StorageScreen(onBack: () -> Unit, onChanged: () -> Unit) {
    BackHandler(onBack = onBack)
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbar = remember { SnackbarHostState() }
    var details by remember { mutableStateOf<StorageDetails?>(null) }
    var reload by remember { mutableIntStateOf(0) }
    var clearing by remember { mutableStateOf<StorageRow?>(null) }
    LaunchedEffect(reload) { details = withContext(Dispatchers.IO) { PortStorage.details(context) } }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.storage_title)) },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back)) } },
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        Column(
            Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            val d = details
            if (d == null) {
                CircularProgressIndicator(Modifier.align(Alignment.CenterHorizontally))
                return@Column
            }
            if (d.known) {
                val app = StorageKind.entries.sumOf { d.sum(it) }
                val other = (d.total - d.free - app).coerceAtLeast(0)
                StorageBar(StorageKind.entries.map { d.sum(it) to Color(it.color) } + (other to OtherColor), d.total, 20.dp)
                Text(
                    stringResource(R.string.storage_summary, size(app), size(d.free), size(d.total)),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (d.rows.isEmpty()) Text(stringResource(R.string.storage_empty), color = MaterialTheme.colorScheme.onSurfaceVariant)
            val max = d.rows.maxOfOrNull { it.total } ?: 0
            for (row in d.rows) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Column(Modifier.width(140.dp)) {
                        Text(rowName(row), fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(size(row.total), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    StorageBar(StorageKind.entries.map { row.size(it) to Color(it.color) }, max, 12.dp, Modifier.weight(1f))
                    IconButton(onClick = { clearing = row }) {
                        Icon(Icons.Filled.Delete, stringResource(R.string.storage_clear), tint = MaterialTheme.colorScheme.error)
                    }
                }
            }
            HorizontalDivider()
            StorageLegend(d)
        }
    }
    clearing?.let { row ->
        val name = rowName(row)
        val freedText = { freed: Long -> context.getString(R.string.storage_cleared, Formatter.formatShortFileSize(context, freed), name) }
        ClearDialog(row, name, onDismiss = { clearing = null }) { kinds ->
            clearing = null
            scope.launch {
                val freed = withContext(Dispatchers.IO) { PortStorage.clear(context, row, kinds) }
                reload++
                onChanged()
                snackbar.showSnackbar(freedText(freed))
            }
        }
    }
}

@Composable
private fun rowName(row: StorageRow): String = row.port?.name ?: stringResource(R.string.storage_drivers_row)

@Composable
private fun StorageBar(segments: List<Pair<Long, Color>>, scale: Long, height: Dp, modifier: Modifier = Modifier) {
    Row(modifier.fillMaxWidth().height(height).clip(RoundedCornerShape(height / 2)).background(MaterialTheme.colorScheme.surfaceVariant)) {
        if (scale <= 0) return@Row
        for ((bytes, color) in segments) {
            if (bytes <= 0) continue
            Box(Modifier.fillMaxHeight().weight((bytes.toDouble() / scale).toFloat().coerceIn(0.002f, 1f)).background(color))
        }
        val rest = 1f - (segments.sumOf { it.first.coerceAtLeast(0) }.toDouble() / scale).toFloat()
        if (rest > 0.0005f) Spacer(Modifier.weight(rest))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun StorageLegend(d: StorageDetails) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        for (kind in StorageKind.entries) LegendItem(Color(kind.color), stringResource(kind.label), size(d.sum(kind)))
        if (d.known) {
            LegendItem(OtherColor, stringResource(R.string.storage_other), null)
            LegendItem(MaterialTheme.colorScheme.surfaceVariant, stringResource(R.string.storage_free_label), size(d.free))
        }
    }
}

@Composable
private fun LegendItem(color: Color, label: String, value: String?) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(Modifier.size(12.dp).clip(RoundedCornerShape(3.dp)).background(color))
        Text(label, style = MaterialTheme.typography.bodySmall)
        if (value != null) Text(value, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** Catégories à effacer ; effacer le jeu emporte tout son dossier, sauvegardes décochées par défaut sinon. */
@Composable
private fun ClearDialog(row: StorageRow, name: String, onDismiss: () -> Unit, onConfirm: (Set<StorageKind>) -> Unit) {
    val kinds = StorageKind.entries.filter { row.size(it) > 0 }
    var chosen by remember { mutableStateOf(kinds.filter { it != StorageKind.SAVES && it != StorageKind.GAME }.toSet().ifEmpty { kinds.toSet() }) }
    val whole = StorageKind.GAME in chosen
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Filled.Delete, null) },
        title = { Text(stringResource(R.string.storage_clear_title, name)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(stringResource(R.string.storage_clear_text))
                for (kind in kinds) {
                    // Jeu effacé : tout son dossier part, les autres cases suivent.
                    val implied = whole && kind != StorageKind.GAME
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(
                            checked = kind in chosen || implied,
                            enabled = !implied,
                            onCheckedChange = { on -> chosen = if (on) chosen + kind else chosen - kind },
                        )
                        Box(Modifier.size(12.dp).clip(RoundedCornerShape(3.dp)).background(Color(kind.color)))
                        Text("  ${stringResource(kind.label)}", Modifier.weight(1f))
                        Text(size(row.size(kind)), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
                Text(
                    stringResource(if (row.port == null) R.string.storage_clear_drivers_hint else R.string.storage_clear_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 6.dp),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(chosen) }, enabled = chosen.isNotEmpty()) {
                Text(stringResource(R.string.storage_clear_button), color = MaterialTheme.colorScheme.error)
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(android.R.string.cancel)) } },
    )
}
