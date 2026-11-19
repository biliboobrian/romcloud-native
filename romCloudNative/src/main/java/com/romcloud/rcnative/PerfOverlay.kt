package com.romcloud.rcnative

import android.app.ActivityManager
import android.content.Context
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Indicateurs de performance pendant le jeu (réglage global de l'application) : panneau de l'application
 * (processeur du jeu et son graphique, mémoire du jeu et de l'appareil) et, pour les jeux servis par DXVK,
 * l'affichage de DXVK (images par seconde, temps par image, charge et mémoire du processeur graphique).
 */
object PerfOverlay {
    private const val PREFS = "settings"
    private const val KEY = "perf_overlay"

    /** Éléments de l'affichage de DXVK (variable DXVK_HUD), dans cet ordre ; le graphique est toujours en bas à gauche. */
    const val DXVK_HUD = "fps,gpuload,memory,frametimes"

    fun enabled(context: Context): Boolean = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY, false)

    fun setEnabled(context: Context, on: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY, on).apply()
    }
}

/** Mesure : processeur du processus (en % d'un cœur), mémoire du jeu et mémoire disponible de l'appareil. */
private class PerfSample(val cpu: Float, val cores: Int, val rssBytes: Long, val availBytes: Long, val totalBytes: Long)

/** Temps processeur du processus (utilisateur + système), en tops d'horloge (100 par seconde). */
private fun cpuTicks(): Long? = try {
    val stat = File("/proc/self/stat").readText()
    // Champs après le nom entre parenthèses : utime et stime sont les 14e et 15e du fichier.
    val fields = stat.substring(stat.lastIndexOf(')') + 2).split(' ')
    fields[11].toLong() + fields[12].toLong()
} catch (e: Exception) {
    null
}

private fun rssBytes(): Long = try {
    File("/proc/self/status").readLines().firstOrNull { it.startsWith("VmRSS:") }
        ?.split(Regex("\\s+"))?.getOrNull(1)?.toLong()?.times(1024) ?: 0L
} catch (e: Exception) {
    0L
}

/** Panneau des indicateurs, rafraîchi deux fois par seconde ; graphique du processeur sur 30 s. */
@Composable
fun PerfPanel(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    var sample by remember { mutableStateOf<PerfSample?>(null) }
    val history = remember { ArrayDeque<Float>() }
    LaunchedEffect(Unit) {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val cores = Runtime.getRuntime().availableProcessors()
        var lastTicks = cpuTicks()
        var lastTime = System.nanoTime()
        while (true) {
            delay(500)
            val s = withContext(Dispatchers.IO) {
                val ticks = cpuTicks()
                val now = System.nanoTime()
                val seconds = (now - lastTime) / 1e9f
                val cpu = if (ticks != null && lastTicks != null && seconds > 0) (ticks - lastTicks!!) / (seconds * 100f) * 100f else 0f
                lastTicks = ticks
                lastTime = now
                val info = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
                PerfSample(cpu, cores, rssBytes(), info.availMem, info.totalMem)
            }
            history.addLast(s.cpu / s.cores)
            while (history.size > 60) history.removeFirst()
            sample = s
        }
    }
    val s = sample ?: return
    Column(modifier.clip(RoundedCornerShape(6.dp)).background(Color(0x99000000)).padding(horizontal = 8.dp, vertical = 4.dp)) {
        val style = androidx.compose.ui.text.TextStyle(color = Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        Text(stringResource(R.string.perf_cpu, s.cpu / s.cores, s.cpu / 100f, s.cores), style = style)
        Text(stringResource(R.string.perf_ram, s.rssBytes / 1e9, s.availBytes / 1e9, s.totalBytes / 1e9), style = style)
        val points = history.toList()
        Canvas(Modifier.width(180.dp).height(32.dp).padding(top = 2.dp)) {
            drawRect(Color(0x33FFFFFF))
            if (points.size >= 2) {
                val step = size.width / 59f
                val path = Path()
                points.forEachIndexed { i, v ->
                    val x = size.width - (points.size - 1 - i) * step
                    val y = size.height * (1f - (v / 100f).coerceIn(0f, 1f))
                    if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
                }
                drawPath(path, Color(0xFF4ADE80), style = Stroke(width = 2f))
            }
            // Repère à 50 %.
            drawLine(Color(0x55FFFFFF), Offset(0f, size.height / 2), Offset(size.width, size.height / 2), strokeWidth = 1f)
        }
    }
}
