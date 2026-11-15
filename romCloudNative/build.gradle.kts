import java.util.Properties

// RomCloud Native : exécute des jeux PC Windows en traduisant leur code x86 / x86-64 en ARM64, une fois
// pour toutes, sur le téléphone même : le recompilateur (recompiler/, Rust) est embarqué dans
// l'application, le runtime (runtime/, C) fournit les fonctions de Windows. Ni Wine ni Box64.
// Prototype de recherche : voir README.md.
plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// Même clé de signature que RomCloud.
val keystoreProps = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}
fun signingValue(env: String, prop: String): String? =
    System.getenv(env)?.takeIf { it.isNotBlank() } ?: keystoreProps.getProperty(prop)

val releaseStoreFile = signingValue("ROMCLOUD_KEYSTORE_FILE", "storeFile")

android {
    namespace = "com.romcloud.rcnative"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.romcloud.rcnative"
        minSdk = 26
        targetSdk = 36
        versionCode = System.getenv("ROMCLOUD_VERSION_CODE")?.toIntOrNull() ?: 1
        versionName = System.getenv("ROMCLOUD_VERSION_NAME")?.takeIf { it.isNotBlank() } ?: "0.1.0-dev"
        // Le code traduit suppose un hôte 64 bits (les pointeurs du jeu sont ceux de l'hôte).
        ndk { abiFilters += "arm64-v8a" }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    // Bibliothèques natives extraites de l'APK (nativeLibraryDir) : libadrenotools y charge ses relais
    // (main_hook…) pour glisser le pilote Vulkan choisi (Turnip) sous le chargeur Vulkan d'Android.
    packaging { jniLibs.useLegacyPackaging = true }

    signingConfigs {
        if (releaseStoreFile != null) {
            create("release") {
                storeFile = rootProject.file(releaseStoreFile)
                storePassword = signingValue("ROMCLOUD_KEYSTORE_PASSWORD", "storePassword")
                keyAlias = signingValue("ROMCLOUD_KEY_ALIAS", "keyAlias")
                keyPassword = signingValue("ROMCLOUD_KEY_PASSWORD", "keyPassword")
            }
        }
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".debug"
        }
        release {
            // Méthodes appelées depuis le code natif (PortSink) : pas de réduction.
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = true
    }
}

// Recompilateur (Rust) compilé pour ARM64 Android : librcn_recomp.so, ajoutée aux bibliothèques natives.
// Il faut Rust (cargo) avec la cible aarch64-linux-android (rustup target add aarch64-linux-android).
val recompilerDir = file("recompiler")
val recompilerLibs = layout.buildDirectory.dir("generated/recompilerLibs")
val buildRecompiler by tasks.registering(Exec::class) {
    description = "Compile le recompilateur pour ARM64 Android"
    val windows = System.getProperty("os.name").lowercase().contains("windows")
    val cargo = listOf(System.getenv("CARGO"), System.getProperty("user.home") + "/.cargo/bin/cargo" + if (windows) ".exe" else "")
        .firstOrNull { it != null && file(it).exists() } ?: "cargo"
    val ndkDir = androidComponents.sdkComponents.ndkDirectory
    val host = if (windows) "windows-x86_64" else if (System.getProperty("os.name").lowercase().contains("mac")) "darwin-x86_64" else "linux-x86_64"
    val linkerPath = "toolchains/llvm/prebuilt/$host/bin/aarch64-linux-android26-clang" + if (windows) ".cmd" else ""
    inputs.dir(recompilerDir.resolve("src"))
    inputs.file(recompilerDir.resolve("Cargo.toml"))
    outputs.dir(recompilerLibs)
    workingDir = recompilerDir
    commandLine(cargo, "build", "--release", "--lib", "--target", "aarch64-linux-android")
    // Éditeur de liens : le clang du NDK du projet.
    doFirst { environment("CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER", ndkDir.get().file(linkerPath).asFile.absolutePath) }
    doLast {
        copy {
            from(recompilerDir.resolve("target/aarch64-linux-android/release/librcn_recomp.so"))
            into(recompilerLibs.get().dir("arm64-v8a"))
        }
    }
}
// Dossier fixe (pas un Provider, refusé par le modèle de sources) ; la dépendance passe par preBuild.
android.sourceSets.getByName("main").jniLibs.directories.add(recompilerLibs.get().asFile.path)
// DXVK (Direct3D 11 par Vulkan), construit par scripts/build-dxvk.mjs : livré s'il a été construit.
layout.buildDirectory.dir("dxvk-jni").get().asFile.let { if (it.isDirectory) android.sourceSets.getByName("main").jniLibs.directories.add(it.path) }
tasks.named("preBuild") { dependsOn(buildRecompiler) }

dependencies {
    implementation("androidx.documentfile:documentfile:1.1.0")
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    implementation(libs.compose.material.icons.extended)
    testImplementation(libs.junit)
}
