package com.romcloud.rcnative

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * Jeu PC installé : ses fichiers d'origine (dans [dir]), son exécutable, et le code machine ARM64
 * traduit sur l'appareil ([code]).
 */
data class Port(
    val id: String,
    val name: String,
    val dir: File,
    val exe: File,
    val code: File,
    /** Version du recompilateur qui a produit [code] : un autre recompilateur doit le refaire. */
    val recompiler: String,
)

enum class ImportError { INVALID_ARCHIVE, NO_EXECUTABLE, TRANSLATION_FAILED, STORAGE }

class PortImportException(val error: ImportError, val detail: String = "", cause: Throwable? = null) :
    Exception(error.name + if (detail.isEmpty()) "" else " : $detail", cause)

/** Jeu en cours d'import : fichiers copiés, exécutable encore à choisir. */
class StagedGame(val dir: File, val name: String, val executables: List<String>)

/**
 * Jeux PC de l'appareil, un dossier chacun dans les fichiers privés de l'application :
 *  - les fichiers du jeu, tels quels (copiés d'un dossier ou d'une archive .zip) ;
 *  - rcn.json : { "format": 2, "name", "exe", "code", "recompiler" } ;
 *  - game.rcnx : code machine traduit par le recompilateur embarqué (PortRunner.recompile).
 */
object PortStore {
    const val MANIFEST = "rcn.json"
    const val CODE = "game.rcnx"
    const val FORMAT = 2

    fun portsDir(context: Context): File = File(context.filesDir, "ports")

    /** Dossier où déposer des archives de jeux depuis un PC (adb push) : importées à l'ouverture de l'application. */
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
            throw PortImportException(ImportError.STORAGE, cause = e)
        } catch (e: JSONException) {
            throw PortImportException(ImportError.INVALID_ARCHIVE, cause = e)
        }
        if (json.optInt("format") != FORMAT) throw PortImportException(ImportError.INVALID_ARCHIVE)
        val exe = entryFile(dir, json.optString("exe"))
        if (exe == null || !exe.isFile) throw PortImportException(ImportError.NO_EXECUTABLE)
        val name = json.optString("name").ifBlank { exe.nameWithoutExtension }
        return Port(dir.name, name, dir, exe, File(dir, json.optString("code", CODE)), json.optString("recompiler"))
    }

    private fun stagingDir(context: Context): File =
        File(portsDir(context), ".import-${System.nanoTime()}").apply { mkdirs() }

    /** Copie le contenu d'une archive .zip du jeu. */
    fun stageZip(context: Context, input: InputStream, name: String): StagedGame {
        val dir = stagingDir(context)
        try {
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    val file = entryFile(dir, entry.name) ?: throw PortImportException(ImportError.INVALID_ARCHIVE)
                    if (entry.isDirectory) {
                        file.mkdirs()
                    } else {
                        file.parentFile?.mkdirs()
                        file.outputStream().use { zip.copyTo(it) }
                    }
                }
            }
            return staged(dir, unwrap(dir, name))
        } catch (e: IOException) {
            dir.deleteRecursively()
            throw PortImportException(ImportError.INVALID_ARCHIVE, cause = e)
        } catch (e: PortImportException) {
            dir.deleteRecursively()
            throw e
        }
    }

    /** Copie le contenu d'un dossier choisi dans le sélecteur de fichiers d'Android. */
    fun stageFolder(context: Context, tree: Uri): StagedGame {
        val root = DocumentFile.fromTreeUri(context, tree) ?: throw PortImportException(ImportError.STORAGE)
        val dir = stagingDir(context)
        try {
            copyTree(context, root, dir)
            return staged(dir, root.name ?: "Jeu")
        } catch (e: IOException) {
            dir.deleteRecursively()
            throw PortImportException(ImportError.STORAGE, cause = e)
        } catch (e: SecurityException) {
            dir.deleteRecursively()
            throw PortImportException(ImportError.STORAGE, cause = e)
        }
    }

    private fun copyTree(context: Context, source: DocumentFile, target: File) {
        for (child in source.listFiles()) {
            val name = child.name ?: continue
            if (name == "." || name == ".." || name.contains('/')) continue
            val out = File(target, name)
            if (child.isDirectory) {
                out.mkdirs()
                copyTree(context, child, out)
            } else {
                context.contentResolver.openInputStream(child.uri)?.use { input -> out.outputStream().use { input.copyTo(it) } }
                    ?: throw IOException("lecture impossible : $name")
            }
        }
    }

    /** Archive dont tout le contenu est dans un seul dossier : ce dossier donne son nom au jeu. */
    private fun unwrap(dir: File, name: String): String {
        val children = dir.listFiles().orEmpty()
        return if (children.size == 1 && children[0].isDirectory) children[0].name else name
    }

    private fun staged(dir: File, name: String): StagedGame {
        val executables = executables(dir)
        if (executables.isEmpty()) {
            dir.deleteRecursively()
            throw PortImportException(ImportError.NO_EXECUTABLE)
        }
        return StagedGame(dir, name, executables)
    }

    /**
     * Exécutables du jeu (chemins relatifs), le plus probable en premier : hors programmes d'installation,
     * de désinstallation et de configuration, puis le plus gros.
     */
    fun executables(dir: File): List<String> {
        val unlikely = Regex("^(unins.*|setup.*|install.*|vcredist.*|dxsetup.*|dxwebsetup.*|.*crash.*|config.*|launcher.*)\\.exe$", RegexOption.IGNORE_CASE)
        return dir.walkTopDown()
            .filter { it.isFile && it.name.endsWith(".exe", ignoreCase = true) }
            .sortedWith(compareBy<File>({ unlikely.matches(it.name) }, { -it.length() }))
            .map { it.relativeTo(dir).invariantSeparatorsPath }
            .toList()
    }

    /**
     * Traduit l'exécutable choisi et installe le jeu (un jeu du même nom est remplacé).
     * Appelé hors du thread principal ; `progress(fait, total)` suit les fonctions traduites.
     */
    fun install(context: Context, staged: StagedGame, exe: String, progress: (Int, Int) -> Unit): Port {
        try {
            val exeFile = entryFile(staged.dir, exe) ?: throw PortImportException(ImportError.NO_EXECUTABLE)
            val version = translate(exeFile, File(staged.dir, CODE), progress)
            writeManifest(staged.dir, staged.name, exe, version)
            val target = File(portsDir(context), portId(staged.name))
            target.deleteRecursively()
            if (!staged.dir.renameTo(target)) throw PortImportException(ImportError.STORAGE)
            return read(target)
        } finally {
            staged.dir.deleteRecursively()
        }
    }

    /** Refait le code d'un jeu traduit par une autre version du recompilateur. */
    fun retranslate(port: Port, progress: (Int, Int) -> Unit): Port {
        val version = translate(port.exe, port.code, progress)
        writeManifest(port.dir, port.name, port.exe.relativeTo(port.dir).invariantSeparatorsPath, version)
        return read(port.dir)
    }

    /** Le code du jeu est-il à refaire (autre version du recompilateur, ou absent) ? */
    fun needsTranslation(port: Port): Boolean = !port.code.isFile || port.recompiler != PortRunner.recompilerVersion()

    private fun translate(exe: File, code: File, progress: (Int, Int) -> Unit): String {
        val (ok, message) = PortRunner.recompile(exe, code, progress)
        if (!ok) throw PortImportException(ImportError.TRANSLATION_FAILED, message)
        return PortRunner.recompilerVersion() ?: ""
    }

    private fun writeManifest(dir: File, name: String, exe: String, recompiler: String) {
        val json = JSONObject()
            .put("format", FORMAT)
            .put("name", name)
            .put("exe", exe)
            .put("code", CODE)
            .put("recompiler", recompiler)
        File(dir, MANIFEST).writeText(json.toString(2))
    }

    /** Importe puis supprime les archives déposées dans [inboxDir] (exécutable le plus probable). Renvoie les erreurs. */
    fun importInbox(context: Context, progress: (String, Int, Int) -> Unit): List<String> {
        val errors = mutableListOf<String>()
        for (file in inboxDir(context)?.listFiles().orEmpty()) {
            if (!file.isFile || !file.name.endsWith(".zip", ignoreCase = true)) continue
            try {
                val staged = file.inputStream().use { stageZip(context, it, file.nameWithoutExtension) }
                install(context, staged, staged.executables.first()) { done, total -> progress(staged.name, done, total) }
            } catch (e: PortImportException) {
                errors += "${file.name} : ${e.message}"
            } catch (e: IOException) {
                errors += "${file.name} : ${e.message}"
            }
            file.delete()
        }
        return errors
    }

    /** Abandonne un import (fichiers copiés supprimés). */
    fun discard(staged: StagedGame) {
        staged.dir.deleteRecursively()
    }

    fun delete(port: Port) {
        port.dir.deleteRecursively()
    }
}
