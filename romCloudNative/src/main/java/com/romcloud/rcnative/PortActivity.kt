package com.romcloud.rcnative

import android.os.Bundle
import android.os.Process
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Exécute un jeu transposé et affiche sa sortie. Premier jalon : programmes en mode console ; la
 * fenêtre et le rendu du jeu viendront avec les fonctions graphiques de l'hôte.
 * L'activité a son propre processus (manifeste) : la quitter arrête le jeu.
 */
class PortActivity : ComponentActivity(), PortSink {
    private var title by mutableStateOf("")
    private var output by mutableStateOf("")
    private var status by mutableStateOf<String?>(null)
    private var failed by mutableStateOf(false)

    private val pending = StringBuilder()
    private var refreshPosted = false

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val port = intent.getStringExtra(EXTRA_PORT)?.let { PortStore.find(this, it) }
        if (port == null) {
            finish()
            return
        }
        title = port.name
        setContent {
            RcnTheme {
                Scaffold(
                    topBar = { TopAppBar(title = { Text(title) }) },
                    bottomBar = {
                        Surface(color = MaterialTheme.colorScheme.surfaceVariant) {
                            Text(
                                status ?: stringResource(R.string.running),
                                Modifier.fillMaxWidth().padding(16.dp),
                                color = if (failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        }
                    },
                ) { padding ->
                    val vertical = rememberScrollState()
                    // La console suit la sortie du jeu.
                    LaunchedEffect(output) { vertical.scrollTo(vertical.maxValue) }
                    Column(Modifier.fillMaxSize().padding(padding).verticalScroll(vertical)) {
                        Text(
                            output,
                            Modifier.horizontalScroll(rememberScrollState()).padding(12.dp),
                            fontFamily = FontFamily.Monospace,
                            fontSize = 11.sp,
                            lineHeight = 13.sp,
                            softWrap = false,
                        )
                    }
                }
            }
        }
        // Une seule exécution par processus : l'activité n'est pas recréée (configChanges).
        if (savedInstanceState == null) {
            // Chaque appel du jeu est un appel C de l'hôte : pile bien plus grande que celle d'un thread Java.
            Thread(null, { PortRunner.run(port, this) }, "rcn-game", GAME_STACK_BYTES).start()
        }
    }

    override fun onOutput(stream: Int, data: ByteArray) {
        synchronized(pending) {
            pending.append(String(data, Charsets.UTF_8))
            if (refreshPosted) return
            refreshPosted = true
        }
        runOnUiThread(::flushOutput)
    }

    private fun flushOutput() {
        val text = synchronized(pending) {
            refreshPosted = false
            pending.toString().also { pending.setLength(0) }
        }
        // Seule la fin d'une sortie très longue est gardée à l'écran.
        output = (output + text).takeLast(MAX_OUTPUT)
    }

    override fun onResult(status: Int, exitCode: Int, error: String) {
        runOnUiThread {
            flushOutput()
            failed = status != PortRunner.OK
            this.status = if (failed) getString(R.string.stopped_error, error) else getString(R.string.finished, exitCode)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        // Le jeu tourne peut-être encore sur son thread : la fin du processus l'arrête.
        if (isFinishing) Process.killProcess(Process.myPid())
    }

    companion object {
        const val EXTRA_PORT = "port"
        private const val MAX_OUTPUT = 200_000
        private const val GAME_STACK_BYTES = 64L * 1024 * 1024
    }
}
