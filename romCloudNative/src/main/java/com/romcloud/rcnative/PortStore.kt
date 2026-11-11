package com.romcloud.rcnative

import android.content.Context
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipInputStream

/** Jeu transposé installé : bibliothèque ARM64 du code traduit et exécutable d'origine (ses données). */
data class Port(
    val id: String,
    val name: String,
    val dir: File,
    val exe: File,
    val lib: File,
    val recompiler: String,
)

enum class ImportError { INVALID_ARCHIVE, UNSUPPORTED_FORMAT, MISSING_FILES }

class PortImportException(val error: ImportError, cause: Throwable? = null) : Exception(error.name, cause)

/**
 * Jeux transposés de l'appareil, un dossier chacun dans les fichiers privés de l'application.
 * Un paquet (.rcn.zip, produit sur PC par scripts/transpose.ps1) contient :
 *  - rcn.json : { "format": 1, "name", "exe", "lib", "abi": "arm64-v8a", "recompiler" } ;
 *  - la bibliothèque du jeu (code traduit + runtime) ;
 *  - l'exécutable d'origine et ses fichiers.
 */
object PortStore {
    const val MANIFEST = "rcn.json"
    const val FORMAT = 1
    const val ABI = "arm64-v8a"

    fun portsDir(context: Context): File = File(context.filesDir, "ports")

    /** Dossier où déposer des paquets depuis un PC (adb push) : importés à l'ouverture de l'application. */
    fun inboxDir(context: Context): File? = context.getExternalFilesDir("import")

    fun list(context: Context): List<Port> =
        portsDir(context).listFiles().orEmpty()
            .filter { it.isDirectory && !it.name.startsWith(".") }
            .mapNotNull { runCatching { read(it) }.getOrNull() }
            .sortedBy { it.name.lowercase() }

    fun find(context: Context, id: String): Port? =
        File(portsDir(context), id).takeIf { portId(id) == id && it.isDirectory }?.let { runCatching { read(it) }.getOrNull() }

    /** Identifiant (nom de dossier) tiré du nom du jeu : lettres, chiffres, « - » et « _ ». */
    fun portId(name: String): String =
        name.trim().map { if (it.isLetterOrDigit() || it == '-' || it == '_') it else '_' }.joinToString("").take(64)
            .ifEmpty { "port" }

    /** Fichier d'une entrée d'archive sous `root`, ou null si son chemin en sort (« ../ », chemin absolu). */
    fun entryFile(root: File, entryName: String): File? {
        val file = File(root, entryName)
        val base = root.canonicalPath + File.separator
        return file.takeIf { it.canonicalPath.startsWith(base) }
    }

    private fun read(dir: File): Port {
        val json = try {
            JSONObject(File(dir, MANIFEST).readText())
        } catch (e: IOException) {
            throw PortImportException(ImportError.MISSING_FILES, e)
        } catch (e: JSONException) {
            throw PortImportException(ImportError.INVALID_ARCHIVE, e)
        }
        if (json.optInt("format") != FORMAT || json.optString("abi") != ABI) {
            throw PortImportException(ImportError.UNSUPPORTED_FORMAT)
        }
        val exe = entryFile(dir, json.optString("exe"))
        val lib = entryFile(dir, json.optString("lib"))
        if (exe == null || lib == null || !exe.isFile || !lib.isFile) throw PortImportException(ImportError.MISSING_FILES)
        val name = json.optString("name").ifBlank { exe.nameWithoutExtension }
        return Port(dir.name, name, dir, exe, lib, json.optString("recompiler"))
    }

    /** Installe un paquet ; un jeu du même nom est remplacé. */
    fun import(context: Context, input: InputStream): Port {
        val ports = portsDir(context).apply { mkdirs() }
        val temp = File(ports, ".import-${System.nanoTime()}")
        try {
            temp.mkdirs()
            try {
                ZipInputStream(input.buffered()).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        val file = entryFile(temp, entry.name) ?: throw PortImportException(ImportError.INVALID_ARCHIVE)
                        if (entry.isDirectory) {
                            file.mkdirs()
                        } else {
                            file.parentFile?.mkdirs()
                            file.outputStream().use { zip.copyTo(it) }
                        }
                    }
                }
            } catch (e: IOException) {
                throw PortImportException(ImportError.INVALID_ARCHIVE, e)
            }
            if (!File(temp, MANIFEST).isFile) throw PortImportException(ImportError.INVALID_ARCHIVE)
            val port = read(temp)
            // Le code du jeu n'a pas à être modifiable une fois installé.
            port.lib.setReadOnly()
            val target = File(ports, portId(port.name))
            target.deleteRecursively()
            if (!temp.renameTo(target)) throw PortImportException(ImportError.INVALID_ARCHIVE)
            return read(target)
        } finally {
            temp.deleteRecursively()
        }
    }

    /** Importe puis supprime les paquets déposés dans [inboxDir]. Renvoie le nombre de paquets refusés. */
    fun importInbox(context: Context): Int {
        var failed = 0
        for (file in inboxDir(context)?.listFiles().orEmpty()) {
            if (!file.isFile || !file.name.endsWith(".zip", ignoreCase = true)) continue
            try {
                file.inputStream().use { import(context, it) }
            } catch (e: PortImportException) {
                failed++
            } catch (e: IOException) {
                failed++
            }
            file.delete()
        }
        return failed
    }

    fun delete(port: Port) {
        port.dir.deleteRecursively()
    }
}
