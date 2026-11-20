package com.romcloud.rcnative

import android.content.Context
import android.os.StatFs
import androidx.annotation.StringRes
import java.io.File

/** Catégories de la page du stockage, dans l'ordre des barres et de la légende (couleurs de RomCloud). */
enum class StorageKind(@StringRes val label: Int, val color: Long) {
    GAME(R.string.storage_kind_game, 0xFF9D95FF),
    CODE(R.string.storage_kind_code, 0xFF3FB6E8),
    SAVES(R.string.storage_kind_saves, 0xFF2EB872),
    DRIVERS(R.string.storage_kind_drivers, 0xFFFF7AB8),
}

/** Place occupée par un jeu ([port]) ou, sans jeu, par les pilotes Vulkan importés. */
data class StorageRow(val port: Port?, val sizes: Map<StorageKind, Long>) {
    val total: Long get() = sizes.values.sum()
    fun size(kind: StorageKind): Long = sizes[kind] ?: 0
}

data class StorageDetails(val rows: List<StorageRow> = emptyList(), val free: Long = -1, val total: Long = -1) {
    val known: Boolean get() = total > 0 && free >= 0
    fun sum(kind: StorageKind): Long = rows.sumOf { it.size(kind) }
}

/**
 * Page du stockage : chaque jeu se décompose en
 *  - code traduit : fichiers .rcnx de son dossier (refaits au prochain lancement s'ils sont effacés) ;
 *  - sauvegardes et réglages : « C:\Users » et « C:\ProgramData » du jeu (son C:\ est le dossier de l'exécutable) ;
 *  - fichiers du jeu : le reste de son dossier.
 * Les pilotes Vulkan importés forment une ligne à part.
 */
object PortStorage {

    private fun codeFiles(port: Port): List<File> =
        port.dir.listFiles { f -> f.isFile && f.name.endsWith(".rcnx") }.orEmpty().toList()

    private fun saveDirs(port: Port): List<File> {
        val drive = port.exe.parentFile ?: return emptyList()
        return listOf(File(drive, "Users"), File(drive, "ProgramData")).filter { it.exists() }
    }

    fun details(context: Context): StorageDetails {
        val rows = PortStore.list(context).map { port ->
            val code = codeFiles(port).sumOf(::sizeOf)
            val saves = saveDirs(port).sumOf(::sizeOf)
            val all = sizeOf(port.dir)
            StorageRow(port, mapOf(StorageKind.GAME to (all - code - saves).coerceAtLeast(0), StorageKind.CODE to code, StorageKind.SAVES to saves))
        }.toMutableList()
        val drivers = VulkanDrivers.list(context).sumOf { d -> d.library.parentFile?.let(::sizeOf) ?: 0 }
        if (drivers > 0) rows += StorageRow(null, mapOf(StorageKind.DRIVERS to drivers))
        val stat = runCatching { StatFs(context.filesDir.absolutePath) }.getOrNull()
        return StorageDetails(rows.filter { it.total > 0 }.sortedByDescending { it.total }, stat?.availableBytes ?: -1, stat?.totalBytes ?: -1)
    }

    /**
     * Efface les catégories [kinds] de la ligne ; renvoie les octets libérés. Effacer le jeu retire tout son
     * dossier (code et sauvegardes compris).
     */
    fun clear(context: Context, row: StorageRow, kinds: Set<StorageKind>): Long {
        val port = row.port
        if (port == null) {
            if (StorageKind.DRIVERS !in kinds) return 0
            VulkanDrivers.list(context).forEach { VulkanDrivers.delete(context, it) }
            return row.total
        }
        if (StorageKind.GAME in kinds) {
            PortStore.delete(port)
            return row.total
        }
        val files = (if (StorageKind.CODE in kinds) codeFiles(port) else emptyList()) +
            (if (StorageKind.SAVES in kinds) saveDirs(port) else emptyList())
        return files.sumOf { file -> sizeOf(file).takeIf { file.deleteRecursively() } ?: 0 }
    }

    /** Taille d'un fichier ou d'un dossier (sous-dossiers compris) ; 0 s'il n'existe pas. */
    fun sizeOf(file: File): Long = when {
        file.isFile -> file.length()
        file.isDirectory -> file.listFiles().orEmpty().sumOf(::sizeOf)
        else -> 0
    }
}
