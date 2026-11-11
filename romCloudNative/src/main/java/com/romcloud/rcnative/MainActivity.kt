package com.romcloud.rcnative

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import java.io.IOException

/** Liste des jeux transposés installés, import d'un paquet. */
class MainActivity : ComponentActivity() {
    private var ports by mutableStateOf(emptyList<Port>())
    private var message by mutableStateOf<String?>(null)

    private val picker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importPackage(uri)
    }

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            RcnTheme {
                val snackbar = remember { SnackbarHostState() }
                var toDelete by remember { mutableStateOf<Port?>(null) }
                LaunchedEffect(message) {
                    message?.let {
                        snackbar.showSnackbar(it)
                        message = null
                    }
                }
                Scaffold(
                    topBar = { TopAppBar(title = { Text(stringResource(R.string.app_name)) }) },
                    snackbarHost = { SnackbarHost(snackbar) },
                    floatingActionButton = {
                        ExtendedFloatingActionButton(
                            onClick = { picker.launch(arrayOf("application/zip", "application/octet-stream")) },
                            icon = { Icon(Icons.Default.Add, contentDescription = null) },
                            text = { Text(stringResource(R.string.import_port)) },
                        )
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
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // Paquets déposés depuis un PC (adb push) : importés hors du thread principal.
        Thread {
            val failed = PortStore.importInbox(this)
            runOnUiThread {
                if (failed > 0) message = getString(R.string.import_failed_invalid)
                refresh()
            }
        }.start()
    }

    private fun refresh() {
        ports = PortStore.list(this)
    }

    private fun play(port: Port) {
        startActivity(Intent(this, PortActivity::class.java).putExtra(PortActivity.EXTRA_PORT, port.id))
    }

    private fun importPackage(uri: Uri) {
        Thread {
            val result = try {
                val port = contentResolver.openInputStream(uri)?.use { PortStore.import(this, it) }
                    ?: throw IOException("openInputStream")
                getString(R.string.import_done, port.name)
            } catch (e: PortImportException) {
                getString(
                    when (e.error) {
                        ImportError.INVALID_ARCHIVE -> R.string.import_failed_invalid
                        ImportError.UNSUPPORTED_FORMAT -> R.string.import_failed_format
                        ImportError.MISSING_FILES -> R.string.import_failed_missing
                    }
                )
            } catch (e: IOException) {
                getString(R.string.import_failed_invalid)
            } catch (e: SecurityException) {
                getString(R.string.import_failed_invalid)
            }
            runOnUiThread {
                message = result
                refresh()
            }
        }.start()
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
