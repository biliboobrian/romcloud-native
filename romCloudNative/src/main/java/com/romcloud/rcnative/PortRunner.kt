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

    /** Le jeu attend du texte (true) ou n'en attend plus : clavier de l'appareil montré ou caché. */
    fun onTextInput(active: Boolean) {}
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

    /**
     * Exécute le jeu sur le thread appelant, jusqu'à sa fin. `vulkanDriver` : bibliothèque du pilote Vulkan
     * choisi par l'utilisateur (Turnip…), ou null pour celui du système (variable RCN_VULKAN_DRIVER du runtime).
     */
    fun run(port: Port, sink: PortSink, vulkanDriver: java.io.File? = null, dxvk: Boolean = false, perfOverlay: Boolean = false) {
        android.system.Os.setenv("RCN_VULKAN_DRIVER", vulkanDriver?.absolutePath ?: "", true)
        android.system.Os.setenv("RCN_D3D", if (dxvk) "dxvk" else "", true)
        // Indicateurs de performance : affichage de DXVK (images/s, temps par image, charge et mémoire du GPU).
        android.system.Os.setenv("DXVK_HUD", if (perfOverlay) PerfOverlay.DXVK_HUD else "", true)
        // Turnip sans rendu par tuiles (réglage du jeu) : chaque dessin n'est plus rejoué par tuile de l'écran,
        // utile quand le jeu fait des milliers de dessins par image (Castlevania : GPU 90 % → 71 %).
        android.system.Os.setenv("TU_DEBUG", if (port.sysmem) "sysmem" else "", true)
        // AVX annoncé au jeu (réglage du jeu ; le code AVX est traduit dans tous les cas).
        android.system.Os.setenv("RCN_AVX", if (port.avx) "1" else "", true)
        // Plafond d'images par seconde (réglage du jeu) : appliqué à chaque présentation par le runtime.
        android.system.Os.setenv("RCN_FPS_MAX", if (port.fpsMax > 0) port.fpsMax.toString() else "", true)
        // Sans plateforme en ligne (réglage du jeu) : Steam et GOG Galaxy refusés au chargement.
        android.system.Os.setenv("RCN_HIDE_DLLS", if (port.offline) "steam_api,galaxy" else "", true)
        android.system.Os.setenv("RCN_NATIVE_LIB_DIR", nativeLibraryDir ?: "", true)
        // Arguments du jeu (réglages de son moteur) : ajoutés à sa ligne de commande par le runtime.
        android.system.Os.setenv("RCN_ARGS", port.args, true)
        // Résolution choisie (réglages du jeu) : aussi la taille de l'écran annoncé au jeu, qui ne peut alors pas
        // en reprendre une plus grande (Unity applique ensuite celle de ses propres options).
        android.system.Os.setenv("RCN_SCREEN", screenSize(port.args) ?: port.screen, true)
        // Réduction des grandes textures (réglage du jeu « compression des textures ») : par le pont DXVK.
        android.system.Os.setenv("RCN_TEX_REDUCE", UnrealOverrides.textureReduce(port)?.let { "$it:1" } ?: "", true)
        // Réglages du moteur d'un jeu Unreal (mémoire…) : remis dans son Engine.ini, que le jeu réécrit en quittant.
        try {
            UnrealOverrides.apply(port)
        } catch (_: java.io.IOException) {
        }
        nativeRun(port.code.absolutePath, port.exe.absolutePath, sink)
    }

    /** « largeur x hauteur » des arguments -screen-width / -screen-height, ou null. */
    private fun screenSize(args: String): String? {
        val a = args.split(' ').filter { it.isNotEmpty() }
        fun value(name: String) = a.indexOf(name).takeIf { it >= 0 && it + 1 < a.size }?.let { a[it + 1].toIntOrNull() }
        val w = value("-screen-width") ?: return null
        val h = value("-screen-height") ?: return null
        return "${w}x$h"
    }

    /** Surface où le jeu est affiché, ou null quand elle disparaît. */
    fun setSurface(surface: android.view.Surface?) = nativeSetSurface(surface)

    /** Boutons de la manette enfoncés (bits de [Pad]). */
    fun setPad(buttons: Int) = nativeSetPad(buttons)

    /** Axes de la manette : sticks -1 … 1 (y vers le bas), gâchettes 0 … 1. */
    fun setAxes(lx: Float, ly: Float, rx: Float, ry: Float, lt: Float, rt: Float) = nativeSetAxes(lx, ly, rx, ry, lt, rt)

    /** Résumé du pilote Vulkan (`driver` : bibliothèque d'un pilote importé, ou null pour celui du système). */
    fun vulkanInfo(driver: File?): String {
        android.system.Os.setenv("RCN_NATIVE_LIB_DIR", nativeLibraryDir ?: "", true)
        return nativeVulkanInfo(driver?.absolutePath)
    }

    /** Essai de DXVK (Direct3D 11 par Vulkan) avec le pilote `driver` (null : celui du système). */
    fun dxvkTest(driver: File?): String {
        android.system.Os.setenv("RCN_NATIVE_LIB_DIR", nativeLibraryDir ?: "", true)
        android.system.Os.setenv("RCN_VULKAN_DRIVER", driver?.absolutePath ?: "", true)
        return nativeDxvkTest()
    }

    /** Dossier des bibliothèques natives extraites de l'APK (relais de libadrenotools), posé par l'activité. */
    var nativeLibraryDir: String? = null

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

    /** Texte tapé au clavier de l'appareil, remis au jeu en frappes. */
    fun keyText(text: String) = nativeKeyText(text)

    /** Touche spéciale ([Keys] : touches virtuelles de Windows), enfoncée ou relâchée. */
    fun key(vk: Int, down: Boolean) = nativeKey(vk, down)

    /** Souris : pointeur placé en (x, y), en 1/65535 de l'image du jeu (0 gauche, 1 droit, 2 milieu). */
    fun mouseMove(x: Int, y: Int) = nativeMouseMove(x, y)
    fun mouseButton(button: Int, down: Boolean) = nativeMouseButton(button, down)
    private external fun nativeKeyText(text: String)
    private external fun nativeKey(vk: Int, down: Boolean)
    private external fun nativeMouseMove(x: Int, y: Int)
    private external fun nativeMouseButton(button: Int, down: Boolean)

    @JvmStatic
    private external fun nativeSetAxes(lx: Float, ly: Float, rx: Float, ry: Float, lt: Float, rt: Float)

    @JvmStatic
    private external fun nativeVulkanInfo(driverPath: String?): String

    @JvmStatic
    private external fun nativeDxvkTest(): String

    @JvmStatic
    private external fun nativeRecompilerVersion(): String?

    @JvmStatic
    private external fun nativeRecompile(exePath: String, codePath: String, listener: RecompileListener, status: IntArray): String
}
