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
    /** Arguments de la ligne de commande du jeu (réglages du moteur, voir GameSettings). */
    val args: String = "",
    /** Turnip sans rendu par tuiles (TU_DEBUG=sysmem) : réglage du jeu, pour les jeux qui dessinent beaucoup. */
    val sysmem: Boolean = false,
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
        return Port(dir.name, name, dir, exe, File(dir, json.optString("code", CODE)), json.optString("recompiler"), json.optString("args"),
            json.optBoolean("sysmem", false))
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
     * Pas de traduction : bibliothèques que le runtime fournit lui-même (SDL2), et kits de Steam, Epic et
     * Discord : absents, le jeu tourne hors ligne comme sans leur client. GOG Galaxy (sans DRM) est traduit :
     * ses jeux l'importent directement (Streets of Rage 4 s'arrête sans lui), et il échoue proprement hors ligne.
     */
    private val providedDlls = setOf(
        "sdl2.dll", "steam_api.dll",
        "steam_api64.dll", "eossdk-win32-shipping.dll", "eossdk-win64-shipping.dll", "discord_game_sdk.dll",
    )

    /**
     * DLL natives du dossier de l'exécutable qu'il faut traduire : même processeur que lui, pas .NET,
     * nommées dans l'exécutable (P/Invoke, LoadLibrary) ou importées par une DLL déjà retenue.
     */
    private fun gameDlls(exe: File): List<File> {
        val exeMachine = peMachine(exe) ?: return emptyList()
        val folder = exe.parentFile ?: return emptyList()
        // Dossier de l'exécutable, et greffons natifs de Unity (« <jeu>_Data/Plugins[/x86_64] »), où le runtime
        // les cherche aussi.
        val dirs = listOf(folder) + folder.listFiles { f -> f.isDirectory && f.name.endsWith("_Data") }.orEmpty()
            .flatMap { listOf(File(it, "Plugins/x86_64"), File(it, "Plugins")) }
        val candidates = dirs.flatMap { d -> d.listFiles { f -> f.isFile && f.name.lowercase().endsWith(".dll") }.orEmpty().toList() }
            .filter { it.name.lowercase() !in providedDlls && peMachine(it) == exeMachine && !isManaged(it) }
            .distinctBy { it.name.lowercase() }
        if (candidates.isEmpty()) return emptyList()
        val chosen = LinkedHashMap<String, File>()
        val pending = ArrayDeque<File>()
        for (dll in mentioned(exe, candidates)) pending.add(dll)
        while (pending.isNotEmpty()) {
            val dll = pending.removeFirst()
            if (chosen.put(dll.name.lowercase(), dll) != null) continue
            for (other in mentioned(dll, candidates.filter { it.name.lowercase() !in chosen })) pending.add(other)
        }
        return chosen.values.toList()
    }

    /**
     * DLL de `candidates` dont le nom (avec ou sans « .dll », sans la casse, en ASCII ou en UTF-16) figure
     * dans `file`. Lecture par morceaux : les DLL de plusieurs dizaines de Mo (Unity) ne tiennent pas en
     * mémoire Java sous forme de texte.
     */
    private fun mentioned(file: File, candidates: List<File>): List<File> {
        if (candidates.isEmpty()) return emptyList()
        // Motifs par candidat : « nom.dll », « nom\0 », en ASCII puis en UTF-16 (octets nuls intercalés).
        val patterns = candidates.map { dll ->
            val full = dll.name.lowercase()
            val bare = full.removeSuffix(".dll") + "\u0000"
            listOf(full, bare).flatMap { s -> listOf(ascii(s), wide(s)) }
        }
        val longest = patterns.flatten().maxOf { it.size }
        val found = BooleanArray(candidates.size)
        val chunk = ByteArray(1 shl 20)
        var carry = 0 // octets gardés de la fin du morceau précédent (motif à cheval)
        file.inputStream().buffered().use { input ->
            while (true) {
                val n = input.read(chunk, carry, chunk.size - carry)
                if (n <= 0) break
                val end = carry + n
                for (i in carry until end) chunk[i] = lower(chunk[i])
                for (c in candidates.indices) {
                    if (!found[c] && patterns[c].any { contains(chunk, end, it) }) found[c] = true
                }
                carry = minOf(longest - 1, end)
                System.arraycopy(chunk, end - carry, chunk, 0, carry)
            }
        }
        return candidates.filterIndexed { i, _ -> found[i] }
    }
    private fun ascii(s: String) = ByteArray(s.length) { s[it].code.toByte() }
    private fun wide(s: String) = ByteArray(s.length * 2) { if (it % 2 == 0) s[it / 2].code.toByte() else 0 }
    private fun lower(b: Byte): Byte = if (b in 'A'.code.toByte()..'Z'.code.toByte()) (b + 32).toByte() else b
    private fun contains(data: ByteArray, size: Int, pattern: ByteArray): Boolean {
        val first = pattern[0]
        var i = 0
        while (i + pattern.size <= size) {
            if (data[i] == first) {
                var k = 1
                while (k < pattern.size && data[i + k] == pattern[k]) k++
                if (k == pattern.size) return true
            }
            i++
        }
        return false
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
        // Réglages choisis pour le jeu (arguments, rendu) : gardés d'une traduction à l'autre.
        val old = runCatching { JSONObject(File(dir, MANIFEST).readText()) }.getOrNull()
        val args = old?.optString("args").orEmpty()
        val json = JSONObject()
            .put("format", FORMAT)
            .put("name", name)
            .put("exe", exe)
            .put("code", CODE)
            .put("recompiler", recompiler)
        if (args.isNotEmpty()) json.put("args", args)
        if (old?.optBoolean("sysmem", false) == true) json.put("sysmem", true)
        File(dir, MANIFEST).writeText(json.toString(2))
    }

    /** Rendu sans tuiles de Turnip pour ce jeu, écrit dans son manifeste. */
    fun setSysmem(port: Port, on: Boolean) {
        val file = File(port.dir, MANIFEST)
        val json = JSONObject(file.readText())
        if (on) json.put("sysmem", true) else json.remove("sysmem")
        file.writeText(json.toString(2))
    }

    /** Arguments de la ligne de commande du jeu, écrits dans son manifeste. */
    fun setArgs(port: Port, args: String) {
        val file = File(port.dir, MANIFEST)
        val json = JSONObject(file.readText())
        if (args.isBlank()) json.remove("args") else json.put("args", args.trim())
        file.writeText(json.toString(2))
    }

    /**
     * Importe puis supprime les archives .zip et les dossiers déposés dans [inboxDir] (exécutable le plus
     * probable). Un dossier dont le nom commence par « . » est encore en cours d'envoi : ignoré.
     * Renvoie les erreurs.
     */
    fun importInbox(context: Context, progress: (String, Int, Int) -> Unit): List<String> {
        val errors = mutableListOf<String>()
        // Copies d'un import interrompu (application arrêtée en cours de route) : supprimées.
        portsDir(context).listFiles { f -> f.isDirectory && f.name.startsWith(".import-") }?.forEach { it.deleteRecursively() }
        for (file in inboxDir(context)?.listFiles().orEmpty()) {
            val folder = file.isDirectory && !file.name.startsWith(".")
            if (!folder && (!file.isFile || !file.name.endsWith(".zip", ignoreCase = true))) continue
            try {
                val staged = if (folder) stageDir(context, file) else file.inputStream().use { stageZip(context, it, file.nameWithoutExtension) }
                install(context, staged, staged.executables.first()) { done, total -> progress(staged.name, done, total) }
            } catch (e: PortImportException) {
                errors += "${file.name} : ${e.message}"
            } catch (e: IOException) {
                errors += "${file.name} : ${e.message}"
            }
            if (folder) file.deleteRecursively() else file.delete()
        }
        return errors
    }

    /** Copie un dossier de jeu déposé dans la boîte d'import. */
    private fun stageDir(context: Context, source: File): StagedGame {
        val dir = stagingDir(context)
        try {
            if (!source.copyRecursively(dir, overwrite = true)) throw IOException("copie de ${source.name}")
            return staged(dir, source.name)
        } catch (e: IOException) {
            dir.deleteRecursively()
            throw PortImportException(ImportError.STORAGE, cause = e)
        }
    }

    /** Abandonne un import (fichiers copiés supprimés). */
    fun discard(staged: StagedGame) {
        staged.dir.deleteRecursively()
    }

    fun delete(port: Port) {
        port.dir.deleteRecursively()
    }
}
