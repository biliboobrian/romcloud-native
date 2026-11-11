import java.util.Properties

// RomCloud Native : exécute des jeux PC « transposés » — code x86-64 traduit une fois pour toutes en
// ARM64 par le recompilateur statique (recompiler/, sur PC), sans Wine ni Box64 à l'exécution.
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

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    implementation(libs.compose.material.icons.extended)
    testImplementation(libs.junit)
}
