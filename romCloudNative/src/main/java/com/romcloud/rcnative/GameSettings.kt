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
    /**
     * Réglages du jeu : ceux de son moteur s'il est reconnu, et pour tous les jeux, ceux du rendu sur
     * l'appareil (pilote Vulkan). Jamais null.
     */
    fun detect(context: Context, port: Port): GameConfig? =
        CommonSettings(context, port, CreationEngine.detect(context, port) ?: UnityEngine.detect(context, port) ?: UnrealEngine.detect(context, port))
}

/** Réglages de tous les jeux (rendu sur l'appareil), ajoutés à ceux du moteur. */
private class CommonSettings(private val context: Context, private val port: Port, private val engineConfig: GameConfig?) : GameConfig {
    override val engine = engineConfig?.engine ?: context.getString(R.string.settings_engine_unknown)

    private val engineOptions by lazy { engineConfig?.options().orEmpty() }

    /** Résolution par l'écran annoncé (RCN_SCREEN) : pour les jeux dont le moteur n'a pas la sienne. */
    private val screenOption: GameOption?
        get() = if (engineOptions.any { it.id == "resolution" }) null else ChoiceOption(
            "screen",
            context.getString(R.string.settings_screen),
            listOf("" to "1920 × 1080") +
                listOf("1600x900", "1280x720", "1024x576", "960x540").map { it to it.replace("x", " × ") },
            port.screen,
            context.getString(R.string.settings_screen_help),
        )

    override fun options(): List<GameOption> = engineOptions + listOfNotNull(screenOption) + ToggleOption(
        "sysmem",
        context.getString(R.string.settings_turnip_sysmem),
        port.sysmem,
        context.getString(R.string.settings_turnip_sysmem_help),
    ) + ToggleOption(
        "tso",
        context.getString(R.string.settings_tso),
        port.tso,
        context.getString(R.string.settings_tso_help),
    )

    override fun save(values: Map<String, String>) {
        engineConfig?.save(values)
        values["sysmem"]?.let { PortStore.setSysmem(port, it == "1") }
        values["screen"]?.let { PortStore.setScreen(port, it) }
        values["tso"]?.let { PortStore.setTso(port, it == "1") }
    }

    override val presets: List<Pair<String, String>> get() = engineConfig?.presets.orEmpty()
    override fun applyPreset(id: String) {
        engineConfig?.applyPreset(id)
    }
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

    /** Retire la clé de la section (sans effet si elle est absente). */
    fun remove(section: String, key: String) {
        var current: String? = null
        val iter = lines.listIterator()
        while (iter.hasNext()) {
            val line = iter.next()
            val name = sectionOf(line)
            if (name != null) current = name
            else if (current.equals(section, true) && keyOf(line).equals(key, true)) iter.remove()
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

/**
 * Réglages du moteur d'un jeu Unreal ([SystemSettings] de Engine.ini, qui priment sur ceux du projet),
 * gardés par l'application dans « rcn_unreal.ini » du dossier du jeu : le jeu réécrit son Engine.ini en
 * quittant et en retire ces lignes, elles y sont donc remises à chaque lancement ([apply]).
 */
object UnrealOverrides {
    /** Profil « mémoire réduite » : réglages mesurés sur Clair Obscur (mémoire du processeur graphique de
     *  4,3 à 2,7 Go dans le prologue) — sans Lumen ni ombres virtuelles, maillages et textures allégés. */
    val lowMemory = listOf(
        "r.Shadow.Virtual.Enable" to "0", "r.Lumen.DiffuseIndirect.Allow" to "0", "r.Lumen.Reflections.Allow" to "0",
        "r.DynamicGlobalIlluminationMethod" to "0", "r.ReflectionMethod" to "0", "r.Streaming.MaxTempMemoryAllowed" to "16",
        "r.Shadow.MaxResolution" to "512", "r.Shadow.MaxCSMResolution" to "512", "r.Shadow.CSM.MaxCascades" to "1",
        "r.Nanite.Streaming.StreamingPoolSize" to "128", "r.VT.PoolSizeScale" to "0.25", "r.SkeletalMeshLODBias" to "2",
        "r.StaticMeshLODBias" to "1", "r.Streaming.MipBias" to "1", "r.SkinCache.Mode" to "0", "r.VolumetricFog" to "0",
        "r.MaxAnisotropy" to "0", "r.SSR.Quality" to "0", "r.AmbientOcclusionLevels" to "0", "r.DepthOfFieldQuality" to "0",
        "r.MotionBlurQuality" to "0", "fx.Niagara.QualityLevel" to "0",
    )

    /** GameUserSettings.ini du profil Windows imité, ou null si le jeu n'est pas fait avec Unreal. */
    fun userSettings(port: Port): File? {
        val win64 = port.exe.parentFile ?: return null
        val project = win64.parentFile?.parentFile ?: return null
        val root = project.parentFile ?: return null
        if (!win64.name.equals("Win64", true) || !win64.parentFile.name.equals("Binaries", true)) return null
        if (!File(root, "Engine").isDirectory) return null
        // « C:\Users\Player » est le dossier « Users/Player » de l'exécutable (runtime).
        return File(win64, "Users/Player/AppData/Local/${project.name}/Saved/Config/Windows/GameUserSettings.ini")
    }

    fun store(port: Port) = File(port.dir, "rcn_unreal.ini")

    /** Section des réglages de l'application elle-même (pas recopiée dans Engine.ini). */
    const val APP_SECTION = "RomCloud"

    /**
     * Réduction des grandes textures sans mips (interface, cartes…) à leur création, par le runtime : côté
     * minimal en pixels (texture réduite de moitié dans chaque dimension), ou null si désactivée.
     */
    fun textureReduce(port: Port): String? =
        IniFile(store(port)).get(APP_SECTION, "TextureReduce")?.takeIf { it.toIntOrNull() != null }

    /** Avant le lancement : réglages gardés recopiés dans Engine.ini (sans effet hors Unreal). */
    fun apply(port: Port) {
        val user = userSettings(port) ?: return
        val store = store(port)
        if (!store.isFile) return
        val engine = IniFile(File(user.parentFile, "Engine.ini"))
        for ((section, key, value) in IniFile(store).entries()) if (section != APP_SECTION) engine.set(section, key, value)
        engine.save()
    }
}

private class UnrealEngine(private val context: Context, private val ini: File, private val port: Port) : GameConfig {
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
        val overrides = IniFile(UnrealOverrides.store(port))
        val pool = overrides.get("SystemSettings", "r.Streaming.PoolSize") ?: "default"
        val lowMem = overrides.get("SystemSettings", UnrealOverrides.lowMemory.first().first) != null
        val reduce = overrides.get(UnrealOverrides.APP_SECTION, "TextureReduce") ?: "off"
        val out = mutableListOf<GameOption>(
            ChoiceOption("resolution", s(R.string.settings_ue_window), resolutions, "${w}x$h", s(R.string.settings_ue_window_help)),
            ChoiceOption("scale", s(R.string.settings_ue_render_scale),
                listOf("25", "33", "50", "67", "75", "85", "100").map { it to "$it %" }, scale, s(R.string.settings_resolution_help)),
            ChoiceOption("fps", s(R.string.settings_ue_frame_limit),
                listOf("20" to "20", "30" to "30", "60" to "60", "0" to s(R.string.settings_ue_unlimited)), limit),
            ToggleOption("vsync", s(R.string.settings_vsync), vsync),
            ChoiceOption("texpool", s(R.string.settings_ue_texture_pool),
                listOf("default" to s(R.string.settings_auto)) + listOf("200", "400", "512", "768", "1024", "1536", "2048").map { it to "$it Mo" },
                pool, s(R.string.settings_ue_texture_pool_help)),
            ToggleOption("lowmem", s(R.string.settings_ue_low_memory), lowMem, s(R.string.settings_ue_low_memory_help)),
            ChoiceOption("texreduce", s(R.string.settings_texture_reduce),
                listOf(
                    "off" to s(R.string.settings_texture_reduce_off),
                    "4096" to s(R.string.settings_texture_reduce_4096),
                    "2048" to s(R.string.settings_texture_reduce_2048),
                    "1024" to s(R.string.settings_texture_reduce_1024),
                ),
                reduce, s(R.string.settings_texture_reduce_help)),
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
        // Réglages du moteur : gardés par l'application, remis dans Engine.ini à chaque lancement (et tout de suite).
        val store = IniFile(UnrealOverrides.store(port))
        val engine = IniFile(File(ini.parentFile, "Engine.ini"))
        fun put(key: String, value: String?) {
            if (value == null) {
                store.remove("SystemSettings", key)
                engine.remove("SystemSettings", key)
            } else {
                store.set("SystemSettings", key, value)
                engine.set("SystemSettings", key, value)
            }
        }
        values["texpool"]?.let { put("r.Streaming.PoolSize", it.takeIf { v -> v != "default" }) }
        values["lowmem"]?.let { on -> for ((key, value) in UnrealOverrides.lowMemory) put(key, value.takeIf { on == "1" }) }
        values["texreduce"]?.let {
            if (it == "off") store.remove(UnrealOverrides.APP_SECTION, "TextureReduce")
            else store.set(UnrealOverrides.APP_SECTION, "TextureReduce", it)
        }
        store.save()
        engine.save()
    }

    companion object {
        /** Jeu Unreal : « <racine>/<Projet>/Binaries/Win64/<exe> » à côté de « <racine>/Engine ». */
        fun detect(context: Context, port: Port): GameConfig? {
            // Avant le premier lancement, le fichier est créé à son emplacement final.
            val ini = UnrealOverrides.userSettings(port) ?: return null
            return UnrealEngine(context, ini, port)
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
            ChoiceOption("quality", s(R.string.settings_unity_quality),
                listOf("auto" to s(R.string.settings_auto), "Low" to "Low", "Medium" to "Medium", "High" to "High"),
                argValue("-screen-quality") ?: "auto", s(R.string.settings_unity_quality_help)),
            ToggleOption("gfxmt", s(R.string.settings_unity_gfx_thread), "-force-gfx-mt" in a, s(R.string.settings_unity_gfx_thread_help)),
            ToggleOption("cpuskin", s(R.string.settings_unity_cpu_skinning), "-disable-gpu-skinning" in a,
                s(R.string.settings_unity_cpu_skinning_help)),
            ToggleOption("singlethread", s(R.string.settings_unity_single_thread), "-force-d3d11-singlethreaded" in a,
                s(R.string.settings_unity_single_thread_help)),
            ToggleOption("nolog", s(R.string.settings_unity_no_log), "-nolog" in a),
        )
    }

    override fun save(values: Map<String, String>) {
        // Arguments que ces réglages ne connaissent pas : gardés.
        val known = setOf("-screen-width", "-screen-height", "-screen-quality")
        val kept = mutableListOf<String>()
        val a = args()
        var i = 0
        while (i < a.size) {
            if (a[i] in known) i += 2
            else {
                if (a[i] !in setOf("-force-d3d11-singlethreaded", "-nolog", "-force-gfx-mt", "-disable-gpu-skinning")) kept.add(a[i])
                i++
            }
        }
        values["resolution"]?.takeIf { it != "auto" }?.split("x")?.let { (w, h) ->
            kept += listOf("-screen-width", w, "-screen-height", h)
        }
        values["quality"]?.takeIf { it != "auto" }?.let { kept += listOf("-screen-quality", it) }
        if (values["gfxmt"] == "1") kept += "-force-gfx-mt"
        if (values["cpuskin"] == "1") kept += "-disable-gpu-skinning"
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
