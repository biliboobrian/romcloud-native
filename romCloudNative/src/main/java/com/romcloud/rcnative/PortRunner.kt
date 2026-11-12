package com.romcloud.rcnative

import java.io.File

/** Reçoit la sortie et le résultat d'un jeu. Appelé depuis le code natif (rcn_jni.c). */
interface PortSink {
    /** Texte écrit par le jeu ; stream : 1 sortie standard, 2 sortie d'erreur. */
    fun onOutput(stream: Int, data: ByteArray)

    /** Le jeu affiche des images de cette taille (première image, ou changement de mode d'affichage). */
    fun onVideo(width: Int, height: Int)

    /** Fin du jeu ; status : [PortRunner.OK] ou une erreur du runtime (message dans error). */
    fun onResult(status: Int, exitCode: Int, error: String)
}

/** Suivi d'une traduction, appelé depuis le code natif. */
fun interface RecompileListener {
    fun onProgress(done: Int, total: Int)
}

object PortRunner {
    const val OK = 0

    init {
        System.loadLibrary("rcnhost")
    }

    /** Exécute le jeu sur le thread appelant, jusqu'à sa fin. */
    fun run(port: Port, sink: PortSink) = nativeRun(port.code.absolutePath, port.exe.absolutePath, sink)

    /** Surface où le jeu est affiché, ou null quand elle disparaît. */
    fun setSurface(surface: android.view.Surface?) = nativeSetSurface(surface)

    /** Boutons de la manette enfoncés (bits de [Pad]). */
    fun setPad(buttons: Int) = nativeSetPad(buttons)

    /** Axes de la manette : sticks -1 … 1 (y vers le bas), gâchettes 0 … 1. */
    fun setAxes(lx: Float, ly: Float, rx: Float, ry: Float, lt: Float, rt: Float) = nativeSetAxes(lx, ly, rx, ry, lt, rt)

    /** Version du recompilateur embarqué (null s'il manque). */
    fun recompilerVersion(): String? = nativeRecompilerVersion()

    /**
     * Traduit l'exécutable `exe` en code machine ARM64 (`code`), sur le thread appelant (quelques
     * secondes). Renvoie le succès et le bilan (ou l'erreur).
     */
    fun recompile(exe: File, code: File, progress: (Int, Int) -> Unit): Pair<Boolean, String> {
        val status = IntArray(1)
        val message = nativeRecompile(exe.absolutePath, code.absolutePath, RecompileListener(progress), status)
        return (status[0] == 0) to message
    }

    @JvmStatic
    private external fun nativeRun(codePath: String, exePath: String, sink: PortSink)

    @JvmStatic
    private external fun nativeSetSurface(surface: android.view.Surface?)

    @JvmStatic
    private external fun nativeSetPad(buttons: Int)

    @JvmStatic
    private external fun nativeSetAxes(lx: Float, ly: Float, rx: Float, ry: Float, lt: Float, rt: Float)

    @JvmStatic
    private external fun nativeRecompilerVersion(): String?

    @JvmStatic
    private external fun nativeRecompile(exePath: String, codePath: String, listener: RecompileListener, status: IntArray): String
}
