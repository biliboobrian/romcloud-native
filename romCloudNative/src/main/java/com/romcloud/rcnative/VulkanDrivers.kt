package com.romcloud.rcnative

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipInputStream

/** Pilote Vulkan importé par l'utilisateur (Turnip…), rangé dans les fichiers privés de l'application. */
data class VulkanDriver(val id: String, val name: String, val version: String, val library: File)

/**
 * Pilotes Vulkan de remplacement : paquets « adpkg » (archive .zip avec meta.json et la bibliothèque du
 * pilote, format des pilotes Turnip pour Adreno utilisés par les émulateurs). Le pilote choisi est donné au
 * runtime (variable RCN_VULKAN_DRIVER), qui le charge directement à la place du pilote du système.
 */
object VulkanDrivers {
    private const val PREFS = "vulkan"
    private const val KEY = "driver"
    private const val MAX_LIBRARY = 256L shl 20

    private fun root(context: Context) = File(context.filesDir, "vulkan-drivers").apply { mkdirs() }

    /** Pilotes importés, par nom. */
    fun list(context: Context): List<VulkanDriver> =
        root(context).listFiles().orEmpty().filter { it.isDirectory }.mapNotNull { read(it) }.sortedBy { it.name }

    private fun read(dir: File): VulkanDriver? = try {
        val meta = JSONObject(File(dir, "meta.json").readText())
        val library = File(dir, meta.getString("libraryName"))
        if (library.isFile) VulkanDriver(dir.name, meta.optString("name", dir.name), meta.optString("driverVersion", ""), library) else null
    } catch (e: Exception) {
        null
    }

    /** Pilote choisi, ou null pour celui du système. */
    fun selected(context: Context): VulkanDriver? {
        val id = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null) ?: return null
        return list(context).firstOrNull { it.id == id }
    }

    fun select(context: Context, driver: VulkanDriver?) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, driver?.id).apply()
    }

    fun delete(context: Context, driver: VulkanDriver) {
        if (selected(context)?.id == driver.id) select(context, null)
        File(root(context), driver.id).deleteRecursively()
    }

    /**
     * Importe un paquet .zip : meta.json (libraryName, name, driverVersion) et la bibliothèque, une
     * bibliothèque ELF ARM64. Renvoie le pilote importé ; IOException si le paquet n'en est pas un.
     */
    fun import(context: Context, input: InputStream): VulkanDriver {
        val staging = File(root(context), ".import-${System.nanoTime()}")
        staging.mkdirs()
        try {
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    // Fichiers à la racine du paquet seulement (pas de chemins).
                    val name = entry.name.substringAfterLast('/')
                    if (entry.isDirectory || name.isEmpty() || name.startsWith(".")) continue
                    val out = File(staging, name)
                    out.outputStream().use { o ->
                        val buffer = ByteArray(1 shl 16)
                        var total = 0L
                        while (true) {
                            val n = zip.read(buffer)
                            if (n < 0) break
                            total += n
                            if (total > MAX_LIBRARY) throw IOException("fichier trop gros dans le paquet : $name")
                            o.write(buffer, 0, n)
                        }
                    }
                }
            }
            val metaFile = File(staging, "meta.json")
            if (!metaFile.isFile) throw IOException("paquet de pilote sans meta.json")
            val meta = JSONObject(metaFile.readText())
            val libraryName = meta.optString("libraryName")
            if (libraryName.isEmpty() || libraryName.contains('/')) throw IOException("meta.json sans libraryName valable")
            val library = File(staging, libraryName)
            if (!library.isFile) throw IOException("bibliothèque $libraryName absente du paquet")
            checkElfArm64(library)
            val id = meta.optString("name", libraryName).replace(Regex("[^A-Za-z0-9._-]+"), "_").take(80)
            val target = File(root(context), id)
            target.deleteRecursively()
            if (!staging.renameTo(target)) throw IOException("impossible de ranger le pilote")
            return read(target) ?: throw IOException("pilote illisible après import")
        } finally {
            staging.deleteRecursively()
        }
    }

    /** Bibliothèque partagée ELF 64 bits pour AArch64 (machine 183). */
    private fun checkElfArm64(file: File) {
        val head = ByteArray(20)
        file.inputStream().use { if (it.read(head) != head.size) throw IOException("bibliothèque tronquée") }
        val elf = head[0] == 0x7f.toByte() && head[1] == 'E'.code.toByte() && head[2] == 'L'.code.toByte() && head[3] == 'F'.code.toByte()
        val machine = (head[18].toInt() and 0xff) or ((head[19].toInt() and 0xff) shl 8)
        if (!elf || head[4].toInt() != 2 || machine != 183) throw IOException("ce n'est pas une bibliothèque ARM64")
    }
}
