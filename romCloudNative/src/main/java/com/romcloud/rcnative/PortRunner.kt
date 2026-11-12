package com.romcloud.rcnative

/** Reçoit la sortie et le résultat d'un jeu transposé. Appelé depuis le code natif (rcn_jni.c). */
interface PortSink {
    /** Texte écrit par le jeu ; stream : 1 sortie standard, 2 sortie d'erreur. */
    fun onOutput(stream: Int, data: ByteArray)

    /** Le jeu affiche des images de cette taille (première image, ou changement de mode d'affichage). */
    fun onVideo(width: Int, height: Int)

    /** Fin du jeu ; status : [PortRunner.OK] ou une erreur du runtime (message dans error). */
    fun onResult(status: Int, exitCode: Int, error: String)
}

object PortRunner {
    const val OK = 0

    init {
        System.loadLibrary("rcnhost")
    }

    /** Exécute le jeu sur le thread appelant, jusqu'à sa fin. */
    fun run(port: Port, sink: PortSink) = nativeRun(port.lib.absolutePath, port.exe.absolutePath, sink)

    /** Surface où le jeu est affiché, ou null quand elle disparaît. */
    fun setSurface(surface: android.view.Surface?) = nativeSetSurface(surface)

    /** Boutons de la manette enfoncés (bits de [Pad]). */
    fun setPad(buttons: Int) = nativeSetPad(buttons)

    @JvmStatic
    private external fun nativeRun(libPath: String, exePath: String, sink: PortSink)

    @JvmStatic
    private external fun nativeSetSurface(surface: android.view.Surface?)

    @JvmStatic
    private external fun nativeSetPad(buttons: Int)
}
