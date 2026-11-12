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
     * Appelé hors du thread principal ; `progress(fait, total)` suit la traduction (exécutable puis DLL).
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

    /**
     * Traduit l'exécutable, puis les DLL du jeu dont il a besoin (« fmod64.dll » → « fmod64.dll.rcnx »
     * à côté de son code, où le runtime les cherche). La progression couvre le tout.
     */
    private fun translate(exe: File, code: File, progress: (Int, Int) -> Unit): String {
        val dlls = gameDlls(exe)
        val steps = 1 + dlls.size
        val (ok, message) = PortRunner.recompile(exe, code, scaled(progress, 0, steps))
        if (!ok) throw PortImportException(ImportError.TRANSLATION_FAILED, message)
        // Code de DLL d'une traduction précédente qui n'est plus retenue : supprimé (le runtime la chargerait).
        val wanted = dlls.map { it.name.lowercase() + ".rcnx" }.toSet() + code.name
        code.parentFile?.listFiles { f -> f.name.endsWith(".rcnx") && f.name !in wanted }?.forEach { it.delete() }
        dlls.forEachIndexed { i, dll ->
            val out = File(code.parentFile, dll.name.lowercase() + ".rcnx")
            val (dllOk, dllMessage) = PortRunner.recompile(dll, out, scaled(progress, i + 1, steps))
            if (!dllOk) throw PortImportException(ImportError.TRANSLATION_FAILED, "${dll.name} : $dllMessage")
        }
        return PortRunner.recompilerVersion() ?: ""
    }

    /** Progression de l'étape `step` sur `steps`, ramenée à 0 … 1000. */
    private fun scaled(progress: (Int, Int) -> Unit, step: Int, steps: Int): (Int, Int) -> Unit = { done, total ->
        val part = if (total > 0) done.toLong() * 1000 / total else 0
        progress(((step * 1000L + part) / steps).toInt(), 1000)
    }

    /**
     * Pas de traduction : bibliothèques que le runtime fournit lui-même (SDL2), et kits des plateformes
     * en ligne (GOG Galaxy, Steam, Epic, Discord) : absents, le jeu tourne hors ligne comme sans leur client.
     */
    private val providedDlls = setOf(
        "sdl2.dll", "galaxy.dll", "galaxy64.dll", "galaxynative.dll", "galaxynative64.dll", "steam_api.dll",
        "steam_api64.dll", "eossdk-win32-shipping.dll", "eossdk-win64-shipping.dll", "discord_game_sdk.dll",
    )

    /**
     * DLL natives du dossier de l'exécutable qu'il faut traduire : même processeur que lui, pas .NET,
     * nommées dans l'exécutable (P/Invoke, LoadLibrary) ou importées par une DLL déjà retenue.
     */
    private fun gameDlls(exe: File): List<File> {
        val exeMachine = peMachine(exe) ?: return emptyList()
        val candidates = exe.parentFile?.listFiles { f -> f.isFile && f.name.lowercase().endsWith(".dll") }
            ?.filter { it.name.lowercase() !in providedDlls && peMachine(it) == exeMachine && !isManaged(it) }
            ?: return emptyList()
        if (candidates.isEmpty()) return emptyList()
        val chosen = LinkedHashMap<String, File>()
        val pending = ArrayDeque<File>()
        val exeText = searchable(exe)
        for (dll in candidates) {
            if (mentions(exeText, dll.name)) pending.add(dll)
        }
        while (pending.isNotEmpty()) {
            val dll = pending.removeFirst()
            if (chosen.put(dll.name.lowercase(), dll) != null) continue
            val text = searchable(dll)
            for (other in candidates) {
                if (other.name.lowercase() !in chosen && mentions(text, other.name)) pending.add(other)
            }
        }
        return chosen.values.toList()
    }

    /** Octets du fichier en minuscules ASCII (UTF-16 ramené à un octet sur deux) pour chercher des noms. */
    private class Searchable(val ascii: String, val wide: String)
    private fun searchable(file: File): Searchable {
        val bytes = file.readBytes()
        val ascii = StringBuilder(bytes.size)
        for (b in bytes) ascii.append(lowerAscii(b))
        val wide = StringBuilder(bytes.size / 2)
        var i = 0
        while (i + 1 < bytes.size) {
            wide.append(if (bytes[i + 1].toInt() == 0) lowerAscii(bytes[i]) else '\u0000')
            i += 2
        }
        // Les noms UTF-16 peuvent commencer à une adresse impaire : deuxième lecture décalée d'un octet.
        val wideOdd = StringBuilder(bytes.size / 2)
        i = 1
        while (i + 1 < bytes.size) {
            wideOdd.append(if (bytes[i + 1].toInt() == 0) lowerAscii(bytes[i]) else '\u0000')
            i += 2
        }
        return Searchable(ascii.toString(), wide.toString() + "\u0000" + wideOdd.toString())
    }
    private fun lowerAscii(b: Byte): Char {
        val c = b.toInt() and 0xFF
        return if (c in 'A'.code..'Z'.code) (c + 32).toChar() else c.toChar()
    }
    /** Nom de la DLL, avec ou sans « .dll », présent dans le texte. */
    private fun mentions(text: Searchable, dllName: String): Boolean {
        val full = dllName.lowercase()
        val bare = full.removeSuffix(".dll")
        return text.ascii.contains(full) || text.wide.contains(full) || text.ascii.contains("$bare\u0000") || text.wide.contains("$bare\u0000")
    }

    /** Machine d'un exécutable PE (0x8664 x86-64, 0x14C x86), ou null. */
    private fun peMachine(file: File): Int? = try {
        java.io.RandomAccessFile(file, "r").use { f ->
            if (f.length() < 0x40) return null
            val header = ByteArray(0x40)
            f.readFully(header)
            if (header[0] != 'M'.code.toByte() || header[1] != 'Z'.code.toByte()) return null
            val nt = le32(header, 0x3C)
            if (nt < 0 || nt + 6 > f.length()) return null
            f.seek(nt.toLong())
            val sig = ByteArray(6)
            f.readFully(sig)
            if (le32(sig, 0) != 0x4550) null else (sig[4].toInt() and 0xFF) or ((sig[5].toInt() and 0xFF) shl 8)
        }
    } catch (e: Exception) {
        null
    }

    /** Assemblage .NET (répertoire CLR présent) : son code n'est pas du x86. */
    private fun isManaged(file: File): Boolean = try {
        java.io.RandomAccessFile(file, "r").use { f ->
            val header = ByteArray(0x40)
            f.readFully(header)
            val nt = le32(header, 0x3C)
            f.seek(nt.toLong() + 24)
            val opt = ByteArray(240)
            f.readFully(opt)
            val magic = (opt[0].toInt() and 0xFF) or ((opt[1].toInt() and 0xFF) shl 8)
            val dirs = if (magic == 0x20B) 112 else 96
            le32(opt, dirs + 14 * 8) != 0
        }
    } catch (e: Exception) {
        false
    }

    private fun le32(b: ByteArray, at: Int): Int =
        (b[at].toInt() and 0xFF) or ((b[at + 1].toInt() and 0xFF) shl 8) or ((b[at + 2].toInt() and 0xFF) shl 16) or ((b[at + 3].toInt() and 0xFF) shl 24)

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
