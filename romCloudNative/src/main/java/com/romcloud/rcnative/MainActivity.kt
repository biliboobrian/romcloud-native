package com.romcloud.rcnative

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import java.io.IOException

/** Travail en cours, affiché dans une fenêtre qui bloque la liste. */
data class Busy(val title: String, val done: Int = 0, val total: Int = 0)

/**
 * Liste des jeux PC, import d'un jeu (dossier ou archive .zip) : ses fichiers sont copiés, puis son
 * exécutable est traduit en ARM64 sur l'appareil par le recompilateur embarqué.
 */
class MainActivity : ComponentActivity() {
    private var ports by mutableStateOf(emptyList<Port>())
    private var message by mutableStateOf<String?>(null)
    private var busy by mutableStateOf<Busy?>(null)
    /** Jeu copié dont l'exécutable reste à choisir (plusieurs candidats). */
    private var choosing by mutableStateOf<StagedGame?>(null)

    private val pickZip = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) stage { PortStore.stageZip(this, openInput(uri), displayName(uri)) }
    }
    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) stage { PortStore.stageFolder(this, uri) }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            RcnTheme {
                val snackbar = remember { SnackbarHostState() }
                var toDelete by remember { mutableStateOf<Port?>(null) }
                var menu by remember { mutableStateOf(false) }
                var padSettings by remember { mutableStateOf(false) }
                LaunchedEffect(message) {
                    message?.let {
                        snackbar.showSnackbar(it)
                        message = null
                    }
                }
                Scaffold(
                    topBar = {
                        TopAppBar(
                            title = { Text(stringResource(R.string.app_name)) },
                            actions = {
                                IconButton(onClick = { padSettings = true }) {
                                    Icon(Icons.Default.SportsEsports, contentDescription = stringResource(R.string.virtual_pad))
                                }
                            },
                        )
                    },
                    snackbarHost = { SnackbarHost(snackbar) },
                    floatingActionButton = {
                        Column(horizontalAlignment = Alignment.End) {
                            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                                DropdownMenuItem(text = { Text(stringResource(R.string.import_folder)) }, onClick = {
                                    menu = false
                                    pickFolder.launch(null)
                                })
                                DropdownMenuItem(text = { Text(stringResource(R.string.import_zip)) }, onClick = {
                                    menu = false
                                    pickZip.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
                                })
                            }
                            ExtendedFloatingActionButton(
                                onClick = { menu = true },
                                icon = { Icon(Icons.Default.Add, contentDescription = null) },
                                text = { Text(stringResource(R.string.import_port)) },
                            )
                        }
                    },
                ) { padding ->
                    if (ports.isEmpty()) {
                        Column(
                            Modifier.fillMaxSize().padding(padding).padding(24.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterVertically),
                        ) {
                            Text(stringResource(R.string.empty_title), style = MaterialTheme.typography.titleLarge)
                            Text(stringResource(R.string.empty_text), style = MaterialTheme.typography.bodyMedium)
                        }
                    } else {
                        LazyColumn(
                            Modifier.fillMaxSize().padding(padding),
                            contentPadding = PaddingValues(start = 16.dp, top = 8.dp, end = 16.dp, bottom = 96.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            items(ports, key = { it.id }) { port ->
                                PortCard(port, onPlay = { play(port) }, onDelete = { toDelete = port })
                            }
                        }
                    }
                }
                toDelete?.let { port ->
                    AlertDialog(
                        onDismissRequest = { toDelete = null },
                        title = { Text(stringResource(R.string.delete_title, port.name)) },
                        text = { Text(stringResource(R.string.delete_text)) },
                        confirmButton = {
                            TextButton(onClick = {
                                PortStore.delete(port)
                                toDelete = null
                                refresh()
                            }) { Text(stringResource(R.string.delete)) }
                        },
                        dismissButton = {
                            TextButton(onClick = { toDelete = null }) { Text(stringResource(android.R.string.cancel)) }
                        },
                    )
                }
                if (padSettings) {
                    var mode by remember { mutableStateOf(VirtualPadSettings.mode(this@MainActivity)) }
                    AlertDialog(
                        onDismissRequest = { padSettings = false },
                        title = { Text(stringResource(R.string.virtual_pad)) },
                        text = {
                            Column {
                                listOf(
                                    VirtualPadMode.AUTO to R.string.virtual_pad_auto,
                                    VirtualPadMode.ALWAYS to R.string.virtual_pad_always,
                                    VirtualPadMode.NEVER to R.string.virtual_pad_never,
                                ).forEach { (value, label) ->
                                    Row(
                                        Modifier.fillMaxWidth().clickable {
                                            mode = value
                                            VirtualPadSettings.setMode(this@MainActivity, value)
                                        }.padding(vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                    ) {
                                        RadioButton(selected = mode == value, onClick = null)
                                        Text(stringResource(label), Modifier.padding(start = 12.dp))
                                    }
                                }
                            }
                        },
                        confirmButton = { TextButton(onClick = { padSettings = false }) { Text(stringResource(android.R.string.ok)) } },
                    )
                }
                choosing?.let { staged ->
                    AlertDialog(
                        onDismissRequest = {
                            choosing = null
                            PortStore.discard(staged)
                        },
                        title = { Text(stringResource(R.string.choose_exe)) },
                        text = {
                            Column(Modifier.verticalScroll(rememberScrollState())) {
                                staged.executables.forEach { exe ->
                                    Text(
                                        exe,
                                        Modifier.fillMaxWidth().clickable {
                                            choosing = null
                                            install(staged, exe)
                                        }.padding(vertical = 12.dp),
                                        style = MaterialTheme.typography.bodyLarge,
                                    )
                                }
                            }
                        },
                        confirmButton = {},
                        dismissButton = {
                            TextButton(onClick = {
                                choosing = null
                                PortStore.discard(staged)
                            }) { Text(stringResource(android.R.string.cancel)) }
                        },
                    )
                }
                busy?.let { work ->
                    AlertDialog(
                        onDismissRequest = {},
                        title = { Text(work.title) },
                        text = {
                            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                if (work.total > 0) {
                                    LinearProgressIndicator(progress = { work.done.toFloat() / work.total }, Modifier.fillMaxWidth())
                                    Text(stringResource(R.string.translating_functions, if (work.total > 0) work.done * 100 / work.total else 0))
                                } else {
                                    LinearProgressIndicator(Modifier.fillMaxWidth())
                                }
                            }
                        },
                        confirmButton = {},
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // Archives déposées depuis un PC (adb push) : importées et traduites hors du thread principal.
        if (busy != null) return
        background {
            val errors = PortStore.importInbox(this) { name, done, total ->
                runOnUiThread { busy = Busy(getString(R.string.translating, name), done, total) }
            }
            runOnUiThread {
                busy = null
                if (errors.isNotEmpty()) message = errors.joinToString("\n")
                refresh()
            }
        }
    }

    private fun refresh() {
        ports = PortStore.list(this)
    }

    private fun background(work: () -> Unit) = Thread(work, "rcn-import").start()

    private fun openInput(uri: Uri) = contentResolver.openInputStream(uri) ?: throw IOException("openInputStream")

    private fun displayName(uri: Uri): String {
        val name = contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: uri.lastPathSegment ?: "Jeu"
        return name.substringAfterLast('/').removeSuffix(".zip").removeSuffix(".ZIP")
    }

    /** Copie des fichiers du jeu, puis choix de l'exécutable (s'il y en a plusieurs) et traduction. */
    private fun stage(copy: () -> StagedGame) {
        busy = Busy(getString(R.string.copying))
        background {
            try {
                val staged = copy()
                runOnUiThread {
                    busy = null
                    if (staged.executables.size == 1) install(staged, staged.executables[0]) else choosing = staged
                }
            } catch (e: Exception) {
                runOnUiThread {
                    busy = null
                    message = describe(e)
                }
            }
        }
    }

    private fun install(staged: StagedGame, exe: String) {
        busy = Busy(getString(R.string.translating, staged.name))
        background {
            val result = try {
                val port = PortStore.install(this, staged, exe) { done, total ->
                    runOnUiThread { busy = Busy(getString(R.string.translating, staged.name), done, total) }
                }
                getString(R.string.import_done, port.name)
            } catch (e: Exception) {
                describe(e)
            }
            runOnUiThread {
                busy = null
                message = result
                refresh()
            }
        }
    }

    /** Lance le jeu ; son code est d'abord refait s'il vient d'une autre version du recompilateur. */
    private fun play(port: Port) {
        if (!PortStore.needsTranslation(port)) {
            startActivity(Intent(this, PortActivity::class.java).putExtra(PortActivity.EXTRA_PORT, port.id))
            return
        }
        busy = Busy(getString(R.string.translating, port.name))
        background {
            try {
                val updated = PortStore.retranslate(port) { done, total ->
                    runOnUiThread { busy = Busy(getString(R.string.translating, port.name), done, total) }
                }
                runOnUiThread {
                    busy = null
                    refresh()
                    startActivity(Intent(this, PortActivity::class.java).putExtra(PortActivity.EXTRA_PORT, updated.id))
                }
            } catch (e: Exception) {
                runOnUiThread {
                    busy = null
                    message = describe(e)
                }
            }
        }
    }

    private fun describe(e: Exception): String = when (e) {
        is PortImportException -> when (e.error) {
            ImportError.INVALID_ARCHIVE -> getString(R.string.import_failed_invalid)
            ImportError.NO_EXECUTABLE -> getString(R.string.import_failed_no_exe)
            ImportError.TRANSLATION_FAILED -> getString(R.string.import_failed_translation, e.detail)
            ImportError.STORAGE -> getString(R.string.import_failed_storage)
        }
        else -> getString(R.string.import_failed_storage)
    }
}

@Composable
private fun PortCard(port: Port, onPlay: () -> Unit, onDelete: () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(start = 16.dp, top = 12.dp, end = 8.dp, bottom = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(port.name, style = MaterialTheme.typography.titleMedium)
                Text(
                    stringResource(R.string.port_details, port.exe.name, port.recompiler.ifBlank { "?" }),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Delete, contentDescription = stringResource(R.string.delete))
            }
            Button(onClick = onPlay) {
                Icon(Icons.Default.PlayArrow, contentDescription = null)
                Text(stringResource(R.string.play))
            }
        }
    }
}
