# RomCloud Native

[![Release](https://img.shields.io/github/v/release/biliboobrian/romcloud-native?label=APK&logo=android)](https://github.com/biliboobrian/romcloud-native/releases/latest)
[![RomCloud Native](https://github.com/biliboobrian/romcloud-native/actions/workflows/release.yml/badge.svg)](https://github.com/biliboobrian/romcloud-native/actions/workflows/release.yml)

**RomCloud Native** (`com.romcloud.rcnative`) est un prototype de recherche : il exécute des jeux PC Windows sur un téléphone ARM64 en **traduisant leur code x86 / x86-64 en ARM64 une fois pour toutes**, sur le téléphone même (recompilateur statique en Rust et Cranelift, embarqué dans l’application), avec un runtime en C qui fournit les fonctions de Windows (Direct3D par DXVK et Turnip). Ni Wine ni Box64.

Tout est décrit dans [romCloudNative/README.md](romCloudNative/README.md) : architecture, jeux essayés, outils de mise au point.

## Dépôts RomCloud

| Dépôt | Contenu |
|---|---|
| [romcloud-server](https://github.com/biliboobrian/romcloud-server) | Serveur (Node.js, image Docker) : bibliothèque de jeux, scraping, API |
| [romcloud-android](https://github.com/biliboobrian/romcloud-android) | Application Android (téléphone, tablette) et Android TV |
| [romcloud-desktop](https://github.com/biliboobrian/romcloud-desktop) | Application Windows (Tauri) et son moteur intégré |
| [romcloud-pc](https://github.com/biliboobrian/romcloud-pc) | RomCloud PC : jeux PC sous Android (Winlator Cmod), application compagnon |
| [romcloud-native](https://github.com/biliboobrian/romcloud-native) | RomCloud Native : jeux PC traduits en ARM64 (prototype de recherche) |

Historique commun jusqu’à la version 1.105 : [romCloud](https://github.com/biliboobrian/romCloud) (archivé).

## Installation

`RomCloud-Native-<version>.apk` de la [dernière Release](https://github.com/biliboobrian/romcloud-native/releases/latest), ou Obtainium avec l’URL `https://github.com/biliboobrian/romcloud-native`. Android 8 minimum, ARM64 seulement. Les jeux se copient depuis l’application (dossier du jeu choisi sur l’appareil) ; ils ne sont pas fournis.

## Développement

```bash
git clone --recurse-submodules https://github.com/biliboobrian/romcloud-native   # libadrenotools en sous-module
node romCloudNative/scripts/build-dxvk.mjs            # DXVK pour Android (Meson, Ninja, NDK)
./gradlew :romCloudNative:assembleRelease             # compile aussi le recompilateur (Rust, cible aarch64-linux-android)
```

**Publication** : le workflow [`.github/workflows/release.yml`](.github/workflows/release.yml) compile le recompilateur, DXVK et l’APK à chaque push ; sur un tag `v*`, il joint `RomCloud-Native-<version>.apk` à la Release. Secrets : ceux de la clé de signature de RomCloud (`ROMCLOUD_KEYSTORE_BASE64`, `ROMCLOUD_KEYSTORE_PASSWORD`, `ROMCLOUD_KEY_ALIAS`, `ROMCLOUD_KEY_PASSWORD`).
