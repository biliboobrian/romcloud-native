package com.romcloud.rcnative

import android.app.Activity
import android.content.Context
import android.os.Build
import android.os.PerformanceHintManager
import java.io.File

/** Mode du processeur pendant le jeu (réglage global). */
enum class CpuMode { SYSTEM, SUSTAINED, BOOST, MAX }

/**
 * Processeur pendant le jeu (réglage global de l'application, menu « Processeur »).
 *
 * Sans accès root, une application ne peut pas relever les fréquences maximales du processeur (vrai
 * surcadençage) : elle peut seulement demander au système de les garder hautes.
 * - [CpuMode.SUSTAINED] : mode de performances soutenues de la fenêtre (fréquences stables, pas de pics
 *   suivis d'un bridage thermique), si l'appareil le propose.
 * - [CpuMode.BOOST] : session de « performance hint » (ADPF, Android 12+) sur les threads du jeu, qui
 *   annonce des images en retard sur leur objectif : le gouverneur monte la fréquence des cœurs qui les
 *   portent.
 * - [CpuMode.MAX] : les deux, avec un objectif plus serré (fréquences au plus haut ; plus de chaleur et de
 *   consommation).
 */
object CpuBoost {
    private const val PREFS = "settings"
    private const val KEY = "cpu_mode"

    fun mode(context: Context): CpuMode =
        runCatching { CpuMode.valueOf(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null) ?: "") }
            .getOrDefault(CpuMode.SYSTEM)

    fun setMode(context: Context, mode: CpuMode) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, mode.name).apply()
    }

    /** Le mode de performances soutenues est-il proposé par l'appareil ? */
    fun sustainedSupported(context: Context): Boolean =
        (context.getSystemService(Context.POWER_SERVICE) as android.os.PowerManager).isSustainedPerformanceModeSupported

    /** Les sessions de « performance hint » sont-elles disponibles ? */
    fun hintSupported(context: Context): Boolean =
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            (context.getSystemService(Context.PERFORMANCE_HINT_SERVICE) as? PerformanceHintManager)?.preferredUpdateRateNanos?.let { it > 0 } == true

    /**
     * Mode de jeu appliqué par le système à l'application (Android 12+ : 1 standard, 2 performance,
     * 3 économie, 4 personnalisé ; 0 inconnu). Choisi par l'utilisateur dans les réglages de jeu du système
     * (Game Turbo de MIUI, tableau de bord de jeu…), pas par l'application.
     */
    fun gameMode(context: Context): Int =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            (context.getSystemService(Context.GAME_SERVICE) as? android.app.GameManager)?.gameMode ?: 0
        else 0

    /** Ouvre les réglages de jeu du système : Game Turbo de MIUI s'il est là, sinon la fiche de l'application. */
    fun openSystemGameSettings(context: Context) {
        val intents = listOf(
            android.content.Intent().setClassName("com.miui.securitycenter", "com.miui.gamebooster.ui.GameBoosterMainActivity"),
            android.content.Intent().setClassName("com.miui.securitycenter", "com.miui.gamebooster.ui.GameBoosterRichWebActivity"),
            android.content.Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, android.net.Uri.parse("package:${context.packageName}")),
        )
        for (intent in intents) {
            intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
            if (runCatching { context.startActivity(intent) }.isSuccess) return
        }
    }

    /** Fréquences actuelles et maximales des groupes de cœurs (MHz), pour l'affichage. */
    fun clusters(): List<Triple<Int, Int, Int>> {
        val seen = mutableSetOf<String>()
        val out = mutableListOf<Triple<Int, Int, Int>>()
        for (cpu in 0 until 16) {
            val dir = File("/sys/devices/system/cpu/cpu$cpu/cpufreq")
            if (!dir.isDirectory) continue
            val related = runCatching { File(dir, "related_cpus").readText().trim() }.getOrDefault("$cpu")
            if (!seen.add(related)) continue
            fun read(name: String) = runCatching { File(dir, name).readText().trim().toInt() / 1000 }.getOrDefault(0)
            out += Triple(cpu, read("scaling_cur_freq"), read("cpuinfo_max_freq"))
        }
        return out
    }

    @Volatile private var running = false
    private var thread: Thread? = null

    /** Au lancement du jeu (activité du jeu) : applique le mode choisi jusqu'à [stop]. */
    fun start(activity: Activity) {
        val mode = mode(activity)
        if (mode == CpuMode.SUSTAINED || mode == CpuMode.MAX) {
            if (sustainedSupported(activity)) activity.window.setSustainedPerformanceMode(true)
        }
        if ((mode == CpuMode.BOOST || mode == CpuMode.MAX) && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val manager = activity.getSystemService(Context.PERFORMANCE_HINT_SERVICE) as? PerformanceHintManager ?: return
            running = true
            // Objectif par image : 1/60 s en BOOST, 1/120 s en MAX ; le travail annoncé le dépasse toujours.
            val target = if (mode == CpuMode.MAX) 8_333_333L else 16_666_666L
            thread = Thread({ hintLoop(manager, target) }, "rcn-cpu-boost").apply {
                isDaemon = true
                start()
            }
        }
    }

    fun stop() {
        running = false
        thread?.interrupt()
        thread = null
    }

    /** Threads du processus (le jeu crée les siens au fil du temps), sans celui-ci. */
    private fun threadIds(): IntArray {
        val self = android.os.Process.myTid()
        return File("/proc/self/task").list().orEmpty().mapNotNull { it.toIntOrNull() }.filter { it != self }.sorted().toIntArray()
    }

    @androidx.annotation.RequiresApi(Build.VERSION_CODES.S)
    private fun hintLoop(manager: PerformanceHintManager, target: Long) {
        var session: PerformanceHintManager.Session? = null
        var tids = IntArray(0)
        var refreshed = 0L
        try {
            while (running) {
                val now = System.nanoTime()
                // Liste des threads relue toutes les 2 s : la session suit les nouveaux threads du jeu.
                if (session == null || now - refreshed > 2_000_000_000L) {
                    refreshed = now
                    val current = threadIds()
                    if (!current.contentEquals(tids) || session == null) {
                        tids = current
                        val next = runCatching {
                            if (Build.VERSION.SDK_INT >= 34 && session != null) {
                                session!!.setThreads(tids)
                                session
                            } else {
                                session?.close()
                                manager.createHintSession(tids, target)
                            }
                        }.getOrNull()
                        session = next
                    }
                }
                // Travail annoncé : 1,5 fois l'objectif, l'image est « en retard » : fréquence relevée.
                runCatching { session?.reportActualWorkDuration(target * 3 / 2) }
                Thread.sleep(target / 1_000_000L)
            }
        } catch (_: InterruptedException) {
        } finally {
            runCatching { session?.close() }
        }
    }
}
