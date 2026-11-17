package com.romcloud.rcnative

import android.content.Context
import java.io.File

/**
 * Réglages propres à un jeu, selon son moteur : ceux que le jeu lit dans ses fichiers (Creation Engine :
 * « <jeu>Prefs.ini ») ou sur sa ligne de commande (Unity). Un jeu dont le moteur n'est pas reconnu n'en a pas.
 */
sealed interface GameOption {
    val id: String
    val label: String
    val help: String?
}

/** Choix parmi des valeurs (valeur enregistrée, texte affiché). */
data class ChoiceOption(
    override val id: String,
    override val label: String,
    val choices: List<Pair<String, String>>,
    val value: String,
    override val help: String? = null,
) : GameOption

/** Interrupteur. */
data class ToggleOption(
    override val id: String,
    override val label: String,
    val value: Boolean,
    override val help: String? = null,
) : GameOption

interface GameConfig {
    /** Nom du moteur, affiché en tête des réglages. */
    val engine: String

    /** Réglages avec leurs valeurs actuelles. */
    fun options(): List<GameOption>

    /** Enregistre les valeurs (identifiant → valeur ; interrupteurs : « 1 » ou « 0 »). */
    fun save(values: Map<String, String>)

    /** Préréglages d'ensemble (identifiant, texte), appliqués d'un coup. */
    val presets: List<Pair<String, String>> get() = emptyList()
    fun applyPreset(id: String) {}
}

object GameSettings {
    /** Réglages du jeu, ou null si son moteur n'est pas reconnu (pas de bouton de réglages). */
    fun detect(context: Context, port: Port): GameConfig? =
        CreationEngine.detect(context, port) ?: UnityEngine.detect(context, port) ?: UnrealEngine.detect(context, port)
}

// --- Fichiers .ini (sections, « clé=valeur ») : lignes gardées telles quelles, fins de ligne de Windows ---

internal class IniFile(private val file: File) {
    private val lines: MutableList<String> =
        if (file.isFile) file.readText().split(Regex("\r?\n")).toMutableList() else mutableListOf()

    private fun sectionOf(line: String): String? {
        val t = line.trim()
        return if (t.startsWith("[") && t.endsWith("]")) t.substring(1, t.length - 1).trim() else null
    }

    private fun keyOf(line: String): String? {
        val t = line.trim()
        val eq = t.indexOf('=')
        return if (eq > 0 && !t.startsWith(";") && !t.startsWith("#")) t.substring(0, eq).trim() else null
    }

    fun get(section: String, key: String): String? {
        var current: String? = null
        for (line in lines) {
            sectionOf(line)?.let { current = it; return@let }
            if (current.equals(section, true) && keyOf(line).equals(key, true)) return line.substringAfter('=').trim()
        }
        return null
    }

    fun set(section: String, key: String, value: String) {
        var current: String? = null
        var lastInSection = -1
        for ((i, line) in lines.withIndex()) {
            val s = sectionOf(line)
            if (s != null) {
                current = s
                if (s.equals(section, true)) lastInSection = i
                continue
            }
            if (current.equals(section, true)) {
                if (line.isNotBlank()) lastInSection = i
                if (keyOf(line).equals(key, true)) {
                    lines[i] = "${keyOf(line)}=$value"
                    return
                }
            }
        }
        if (lastInSection >= 0) {
            lines.add(lastInSection + 1, "$key=$value")
        } else {
            if (lines.isNotEmpty() && lines.last().isNotBlank()) lines.add("")
            lines.add("[$section]")
            lines.add("$key=$value")
        }
    }

    /** Toutes les valeurs, section par section (pour appliquer un préréglage). */
    fun entries(): List<Triple<String, String, String>> {
        val out = mutableListOf<Triple<String, String, String>>()
        var current = ""
        for (line in lines) {
            val s = sectionOf(line)
            if (s != null) {
                current = s
                continue
            }
            keyOf(line)?.let { out.add(Triple(current, it, line.substringAfter('=').trim())) }
        }
        return out
    }

    fun save() {
        file.parentFile?.mkdirs()
        file.writeText(lines.joinToString("\r\n"))
    }
}

/** Le premier fichier de ce nom (sans la casse) sous `dir`, ou null. */
private fun findFile(dir: File, name: String, depth: Int = 6): File? {
    if (depth < 0) return null
    val children = dir.listFiles() ?: return null
    children.firstOrNull { it.isFile && it.name.equals(name, true) }?.let { return it }
    for (child in children) {
        if (child.isDirectory && !child.name.equals("Data", true)) findFile(child, name, depth - 1)?.let { return it }
    }
    return null
}

// --- Creation Engine de Bethesda (Skyrim Special/Anniversary Edition, Fallout 4) ---
// Réglages graphiques dans « <jeu>Prefs.ini » (écrit par le jeu à côté de ses sauvegardes) ; préréglages
// Low/Medium/High/Ultra.ini fournis avec le jeu (ceux de son lanceur).

private class CreationEngine(private val context: Context, private val prefs: File, private val gameDir: File) : GameConfig {
    override val engine = "Creation Engine (Bethesda)"

    private fun s(id: Int) = context.getString(id)

    override val presets: List<Pair<String, String>>
        get() = listOf(
            "Low" to s(R.string.settings_preset_low),
            "Medium" to s(R.string.settings_preset_medium),
            "High" to s(R.string.settings_preset_high),
            "Ultra" to s(R.string.settings_preset_ultra),
        ).filter { (id, _) -> findFile(gameDir, "$id.ini", 0) != null }

    override fun applyPreset(id: String) {
        val preset = findFile(gameDir, "$id.ini", 0) ?: return
        val ini = IniFile(prefs)
        for ((section, key, value) in IniFile(preset).entries()) ini.set(section, key, value)
        ini.save()
    }

    override fun options(): List<GameOption> {
        val ini = IniFile(prefs)
        fun v(section: String, key: String, default: String) = ini.get(section, key) ?: default
        fun on(section: String, key: String, default: Boolean) = v(section, key, if (default) "1" else "0") != "0"
        fun num(section: String, key: String, default: String) =
            v(section, key, default).toDoubleOrNull()?.let { if (it == Math.floor(it)) it.toLong().toString() else it.toString() } ?: default
        val w = v("Display", "iSize W", "1920")
        val h = v("Display", "iSize H", "1080")
        val taa = on("Display", "bUseTAA", true)
        val fxaa = on("Display", "bFXAAEnabled", false)
        val resolutions = listOf("1920x1080", "1600x900", "1280x720", "1024x576", "960x540").map { it to it.replace("x", " × ") }
        return listOf(
            ChoiceOption("resolution", s(R.string.settings_resolution), resolutions, "${w}x$h", s(R.string.settings_resolution_help)),
            ChoiceOption("aa", s(R.string.settings_antialiasing),
                listOf("taa" to "TAA", "fxaa" to "FXAA", "none" to s(R.string.settings_none)),
                if (taa) "taa" else if (fxaa) "fxaa" else "none"),
            ChoiceOption("shadowres", s(R.string.settings_shadow_resolution),
                listOf("512", "1024", "2048", "4096").map { it to it }, num("Display", "iShadowMapResolution", "2048")),
            ChoiceOption("shadowdist", s(R.string.settings_shadow_distance),
                listOf("2000", "3000", "5000", "8000", "10000").map { it to it }, num("Display", "fShadowDistance", "8000")),
            ChoiceOption("textures", s(R.string.settings_textures),
                listOf("0" to s(R.string.settings_high), "1" to s(R.string.settings_medium), "2" to s(R.string.settings_low)),
                v("Display", "iTexMipMapSkip", "0")),
            ChoiceOption("grass", s(R.string.settings_grass_distance),
                listOf("1000", "3500", "7000").map { it to it }, num("Grass", "fGrassStartFadeDistance", "7000")),
            ChoiceOption("objects", s(R.string.settings_object_distance),
                listOf("5" to s(R.string.settings_low), "15" to s(R.string.settings_medium), "30" to s(R.string.settings_high)),
                num("LOD", "fLODFadeOutMultObjects", "15")),
            ToggleOption("sao", s(R.string.settings_ambient_occlusion), on("Display", "bSAOEnable", true)),
            ToggleOption("volumetric", s(R.string.settings_volumetric), on("Display", "bVolumetricLightingEnable", true)),
            ToggleOption("ssr", s(R.string.settings_reflections), on("Display", "bScreenSpaceReflectionEnabled", true)),
            ToggleOption("water", s(R.string.settings_water_reflections), on("Water", "bUseWaterReflections", true)),
            ToggleOption("dof", s(R.string.settings_depth_of_field), on("ImageSpace", "bDoDepthOfField", true)),
            ToggleOption("decals", s(R.string.settings_decals), on("Decals", "bDecals", true)),
            ToggleOption("lensflare", s(R.string.settings_lens_flare), on("ImageSpace", "bLensFlare", true)),
            ToggleOption("vsync", s(R.string.settings_vsync), v("Display", "iVSyncPresentInterval", "1") != "0"),
        )
    }

    override fun save(values: Map<String, String>) {
        val ini = IniFile(prefs)
        values["resolution"]?.split("x")?.takeIf { it.size == 2 }?.let { (w, h) ->
            ini.set("Display", "iSize W", w)
            ini.set("Display", "iSize H", h)
        }
        values["aa"]?.let {
            ini.set("Display", "bUseTAA", if (it == "taa") "1" else "0")
            ini.set("Display", "bFXAAEnabled", if (it == "fxaa") "1" else "0")
        }
        values["shadowres"]?.let { ini.set("Display", "iShadowMapResolution", it) }
        values["shadowdist"]?.let { ini.set("Display", "fShadowDistance", "$it.0000") }
        values["textures"]?.let { ini.set("Display", "iTexMipMapSkip", it) }
        values["grass"]?.let { ini.set("Grass", "fGrassStartFadeDistance", "$it.0000") }
        values["objects"]?.let {
            ini.set("LOD", "fLODFadeOutMultObjects", "$it.0000")
            ini.set("LOD", "fLODFadeOutMultItems", if (it == "5") "1.5000" else if (it == "15") "3.0000" else "10.0000")
            ini.set("LOD", "fLODFadeOutMultActors", if (it == "5") "5.0000" else if (it == "15") "6.0000" else "15.0000")
        }
        values["sao"]?.let { ini.set("Display", "bSAOEnable", it) }
        values["volumetric"]?.let { ini.set("Display", "bVolumetricLightingEnable", it) }
        values["ssr"]?.let { ini.set("Display", "bScreenSpaceReflectionEnabled", it) }
        values["water"]?.let {
            ini.set("Water", "bUseWaterReflections", it)
            ini.set("Water", "bUseWaterRefractions", it)
        }
        values["dof"]?.let { ini.set("ImageSpace", "bDoDepthOfField", it) }
        values["decals"]?.let {
            ini.set("Decals", "bDecals", it)
            ini.set("Decals", "bSkinnedDecals", it)
        }
        values["lensflare"]?.let { ini.set("ImageSpace", "bLensFlare", it) }
        values["vsync"]?.let { ini.set("Display", "iVSyncPresentInterval", it) }
        ini.save()
    }

    companion object {
        // Exécutable → nom de base du fichier de réglages.
        private val games = mapOf("skyrimse.exe" to "Skyrim", "skyrimvr.exe" to "SkyrimVR", "fallout4.exe" to "Fallout4")

        fun detect(context: Context, port: Port): GameConfig? {
            val base = games[port.exe.name.lowercase()] ?: return null
            val gameDir = port.exe.parentFile ?: return null
            // Fichier écrit par le jeu ; avant son premier lancement, créé à côté de l'exécutable.
            val prefs = findFile(port.dir, "${base}Prefs.ini") ?: File(gameDir, "${base}Prefs.ini")
            return CreationEngine(context, prefs, gameDir)
        }
    }
}

// --- Unreal Engine 4 / 5 : « GameUserSettings.ini » du profil Windows imité ---
// Le jeu le lit au lancement et le réécrit : qualité par groupes (« sg.* » de [ScalabilityGroups], 0 bas à
// 4 cinématique), définition de rendu (sg.ResolutionQuality, en %), taille et mode de fenêtre, limite
// d'images. Les clés de UGameUserSettings sont dans la section de la classe du jeu (souvent dérivée :
// « /Script/<Jeu>.…GameUserSettings ») : celle qui porte déjà ResolutionSizeX, sinon celle du moteur.

private class UnrealEngine(private val context: Context, private val ini: File) : GameConfig {
    override val engine = "Unreal Engine"

    private fun s(id: Int) = context.getString(id)

    private val groups = listOf(
        "sg.ViewDistanceQuality" to R.string.settings_ue_view_distance,
        "sg.AntiAliasingQuality" to R.string.settings_antialiasing,
        "sg.ShadowQuality" to R.string.settings_shadow_resolution,
        "sg.GlobalIlluminationQuality" to R.string.settings_ue_global_illumination,
        "sg.ReflectionQuality" to R.string.settings_ue_reflections,
        "sg.PostProcessQuality" to R.string.settings_ue_post_process,
        "sg.TextureQuality" to R.string.settings_textures,
        "sg.EffectsQuality" to R.string.settings_ue_effects,
        "sg.FoliageQuality" to R.string.settings_ue_foliage,
        "sg.ShadingQuality" to R.string.settings_ue_shading,
    )

    private fun userSection(file: IniFile): String =
        file.entries().firstOrNull { it.second.equals("ResolutionSizeX", true) }?.first ?: "/Script/Engine.GameUserSettings"

    private fun levels() = listOf(
        "0" to s(R.string.settings_low), "1" to s(R.string.settings_medium), "2" to s(R.string.settings_high),
        "3" to s(R.string.settings_ue_epic), "4" to s(R.string.settings_ue_cinematic),
    )

    override val presets: List<Pair<String, String>>
        get() = listOf(
            "0" to s(R.string.settings_preset_low), "1" to s(R.string.settings_preset_medium),
            "2" to s(R.string.settings_preset_high), "3" to s(R.string.settings_ue_epic),
        )

    override fun applyPreset(id: String) {
        val file = IniFile(ini)
        for ((key, _) in groups) file.set("ScalabilityGroups", key, id)
        file.set("ScalabilityGroups", "sg.LandscapeQuality", id)
        // Définition de rendu associée : plus basse en bas de gamme (un téléphone).
        file.set("ScalabilityGroups", "sg.ResolutionQuality", when (id) { "0" -> "50"; "1" -> "67"; "2" -> "85"; else -> "100" })
        file.save()
    }

    override fun options(): List<GameOption> {
        val file = IniFile(ini)
        val user = userSection(file)
        val w = file.get(user, "ResolutionSizeX") ?: "1920"
        val h = file.get(user, "ResolutionSizeY") ?: "1080"
        val windowed = file.get(user, "FullscreenMode") == "2"
        val resolutions = listOf("1920x1080", "1600x900", "1280x720", "1024x576", "960x540").map { it to it.replace("x", " × ") }
        val scale = file.get("ScalabilityGroups", "sg.ResolutionQuality")?.toDoubleOrNull()?.toInt()?.toString() ?: "100"
        val limit = file.get(user, "FrameRateLimit")?.toDoubleOrNull()?.toInt()?.toString() ?: "0"
        val vsync = file.get(user, "bUseVSync")?.equals("True", true) ?: false
        val out = mutableListOf<GameOption>(
            ChoiceOption("resolution", s(R.string.settings_ue_window), resolutions, "${w}x$h", s(R.string.settings_ue_window_help)),
            ChoiceOption("scale", s(R.string.settings_ue_render_scale),
                listOf("25", "33", "50", "67", "75", "85", "100").map { it to "$it %" }, scale, s(R.string.settings_resolution_help)),
            ChoiceOption("fps", s(R.string.settings_ue_frame_limit),
                listOf("20" to "20", "30" to "30", "60" to "60", "0" to s(R.string.settings_ue_unlimited)), limit),
            ToggleOption("vsync", s(R.string.settings_vsync), vsync),
        )
        for ((key, label) in groups) {
            val v = file.get("ScalabilityGroups", key)?.toDoubleOrNull()?.toInt()?.coerceIn(0, 4)?.toString() ?: "3"
            out += ChoiceOption(key, s(label), levels(), v)
        }
        return out
    }

    override fun save(values: Map<String, String>) {
        val file = IniFile(ini)
        val user = userSection(file)
        values["resolution"]?.split("x")?.takeIf { it.size == 2 }?.let { (w, h) ->
            // Fenêtré : en « plein écran fenêtré » (1) le jeu prend la taille du bureau imité, pas celle-ci.
            for ((k, v) in listOf(
                "ResolutionSizeX" to w, "ResolutionSizeY" to h,
                "LastUserConfirmedResolutionSizeX" to w, "LastUserConfirmedResolutionSizeY" to h,
                "FullscreenMode" to "2", "LastConfirmedFullscreenMode" to "2", "PreferredFullscreenMode" to "2",
            )) file.set(user, k, v)
        }
        values["scale"]?.let { file.set("ScalabilityGroups", "sg.ResolutionQuality", it) }
        values["fps"]?.let { file.set(user, "FrameRateLimit", "$it.000000") }
        values["vsync"]?.let { file.set(user, "bUseVSync", if (it == "1") "True" else "False") }
        for ((key, _) in groups) values[key]?.let { file.set("ScalabilityGroups", key, it) }
        file.save()
    }

    companion object {
        /** Jeu Unreal : « <racine>/<Projet>/Binaries/Win64/<exe> » à côté de « <racine>/Engine ». */
        fun detect(context: Context, port: Port): GameConfig? {
            val win64 = port.exe.parentFile ?: return null
            val project = win64.parentFile?.parentFile ?: return null
            val root = project.parentFile ?: return null
            if (!win64.name.equals("Win64", true) || !win64.parentFile.name.equals("Binaries", true)) return null
            if (!File(root, "Engine").isDirectory) return null
            // Profil Windows imité par le runtime : « C:\Users\Player » est le dossier « Users/Player » de
            // l'exécutable. Avant le premier lancement, le fichier est créé à son emplacement final.
            val ini = File(win64, "Users/Player/AppData/Local/${project.name}/Saved/Config/Windows/GameUserSettings.ini")
            return UnrealEngine(context, ini)
        }
    }
}

// --- Unity : réglages par la ligne de commande du lecteur (résolution, rendu) ---

private class UnityEngine(private val context: Context, private val port: Port) : GameConfig {
    override val engine = "Unity"

    private fun s(id: Int) = context.getString(id)

    private fun args(): List<String> = port.args.split(' ').filter { it.isNotBlank() }
    private fun argValue(name: String): String? = args().let { a -> a.indexOf(name).takeIf { it >= 0 && it + 1 < a.size }?.let { a[it + 1] } }

    override fun options(): List<GameOption> {
        val w = argValue("-screen-width")
        val h = argValue("-screen-height")
        val resolutions = listOf("auto" to s(R.string.settings_auto)) +
            listOf("1920x1080", "1600x900", "1280x720", "960x540").map { it to it.replace("x", " × ") }
        val a = args()
        return listOf(
            ChoiceOption("resolution", s(R.string.settings_resolution), resolutions,
                if (w != null && h != null) "${w}x$h" else "auto", s(R.string.settings_resolution_help)),
            ToggleOption("singlethread", s(R.string.settings_unity_single_thread), "-force-d3d11-singlethreaded" in a,
                s(R.string.settings_unity_single_thread_help)),
            ToggleOption("nolog", s(R.string.settings_unity_no_log), "-nolog" in a),
        )
    }

    override fun save(values: Map<String, String>) {
        // Arguments que ces réglages ne connaissent pas : gardés.
        val known = setOf("-screen-width", "-screen-height")
        val kept = mutableListOf<String>()
        val a = args()
        var i = 0
        while (i < a.size) {
            if (a[i] in known) i += 2
            else {
                if (a[i] != "-force-d3d11-singlethreaded" && a[i] != "-nolog") kept.add(a[i])
                i++
            }
        }
        values["resolution"]?.takeIf { it != "auto" }?.split("x")?.let { (w, h) ->
            kept += listOf("-screen-width", w, "-screen-height", h)
        }
        if (values["singlethread"] == "1") kept += "-force-d3d11-singlethreaded"
        if (values["nolog"] == "1") kept += "-nolog"
        PortStore.setArgs(port, kept.joinToString(" "))
    }

    companion object {
        fun detect(context: Context, port: Port): GameConfig? {
            val dir = port.exe.parentFile ?: return null
            val data = File(dir, port.exe.nameWithoutExtension + "_Data")
            val unity = File(dir, "UnityPlayer.dll").isFile ||
                (data.isDirectory && (File(data, "globalgamemanagers").isFile || File(data, "mainData").isFile))
            return if (unity) UnityEngine(context, port) else null
        }
    }
}
