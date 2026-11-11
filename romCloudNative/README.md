# RomCloud Native

Prototype de recherche : exécuter des jeux PC sur Android **sans émulation du processeur**. Le code x86 (32 bits) ou x86-64 de l’exécutable Windows est traduit une fois pour toutes en ARM64 sur un PC (recompilation statique), puis exécuté sur l’appareil comme du code natif, avec un hôte qui fournit les fonctions Windows dont le jeu a besoin. RomCloud PC (Winlator : Box64 + Wine) traduit, lui, le code pendant que le jeu tourne.

## État

**Ce qui fonctionne** : des programmes Windows en mode console, x86-64 ou x86 32 bits, à plusieurs threads. Quatre programmes de test produisent après traduction la même sortie que l’exe d’origine, sur PC et sur téléphone ARM64 (`node scripts/test.mjs --device`) :

- [`samples/demo/demo.c`](samples/demo/demo.c), sans bibliothèque C (`kernel32` seulement) : récursion, tables de sauts, pointeurs de fonctions, arithmétique 64 et 128 bits, flottants SSE2, tas ;
- le même en **x86 32 bits sans SSE** (`demo32`) : arguments sur la pile, flottants par l’unité x87, image chargée à son adresse d’origine ;
- [`samples/crt/crt.c`](samples/crt/crt.c), un programme C ordinaire lié à l’UCRT : démarrage du CRT, `printf` / `snprintf` / `sscanf`, chaînes, tas, fichiers, `qsort` et `atexit` (l’hôte rappelle le jeu), mathématiques, variables locales au thread, `setjmp` / `longjmp` ;
- [`samples/win32/win32.c`](samples/win32/win32.c), les fonctions de `kernel32` : fichiers, threads, opérations atomiques, sections critiques, événements, mutex, sémaphores, verrous légers, variables de condition, texte UTF-16.

**Premier jeu visé : Warning Forever** (2005, x86 32 bits, DirectDraw 7). Son code est traduit à 99,75 % (107 590 instructions, les 270 restantes sont presque toutes des données décodées à tort comme du code) ; sur PC comme sur téléphone, il passe tout le démarrage de sa bibliothèque C et s’arrête à son premier appel de DirectDraw (`DirectDrawEnumerateExA`). Restent à fournir : `ddraw`, `dinput`, `dsound`, `winmm`, la fenêtre (`user32`) et le texte (`gdi32`), reliés à une surface, aux entrées et au son d’Android.

**Programmes 32 bits** : leurs pointeurs tiennent sur 4 octets et servent tels quels à l’hôte ; l’image est donc placée à son adresse d’origine et toute la mémoire que le jeu voit vient d’une zone réservée sous 2 Go ([`rcn_mem.c`](runtime/rcn_mem.c)). Les fonctions Windows reçoivent leurs arguments sur la pile ; chacune doit être vérifiée pour le 32 bits (tailles des pointeurs et des structures) avant d’y être proposée — celles qui ne le sont pas encore y apparaissent comme absentes. Sont prêtes : celles de `kernel32` qu’utilise le démarrage d’un programme MSVC, les fichiers de base, threads et sections critiques ; pas encore la bibliothèque C importée (`msvcrt`), `printf` compris. La variable d’environnement `RCN_TRACE` fait noter chaque appel d’une fonction Windows sur la sortie d’erreur.

**Fonctions Windows fournies** (une fonction absente arrête le programme en la nommant quand il l’appelle) :

| Bibliothèque | Fichier | Fonctions |
|---|---|---|
| `kernel32`, généralités | [`rcn_kernel32.c`](runtime/rcn_kernel32.c) | processus (`ExitProcess`, `GetModuleFileName`, `GetCommandLine`), tas (`Heap*`), mémoire (`Virtual*`), temps (`GetTickCount*`, `QueryPerformance*`, `Sleep`, `GetSystemTimeAsFileTime`), texte (`MultiByteToWideChar`, `WideCharToMultiByte`, toute page de codes traitée comme de l’UTF-8) |
| `kernel32`, fichiers | [`rcn_k32_file.c`](runtime/rcn_k32_file.c) | `CreateFile`, `ReadFile`, `WriteFile` (console et fichiers), `SetFilePointer(Ex)`, `GetFileSize(Ex)`, `GetFileAttributes`, `CreateDirectory`, `RemoveDirectory`, `DeleteFile`, en « A » et en « W ». Le dossier du jeu est son `C:\` |
| `kernel32`, threads | [`rcn_k32_sync.c`](runtime/rcn_k32_sync.c) | `CreateThread` (un thread de l’hôte par thread du jeu, avec son processeur, sa pile et ses variables locales), `WaitForSingleObject` / `MultipleObjects`, événements, mutex, sémaphores, sections critiques, `SRWLOCK`, variables de condition, `TlsAlloc` / `FlsAlloc` |
| UCRT, `msvcrt`, `vcruntime` | [`rcn_crt.c`](runtime/rcn_crt.c), [`rcn_format.c`](runtime/rcn_format.c) | démarrage et arrêt (`_initterm`, `exit`, `atexit`), tas, chaînes et mémoire, classes de caractères, conversions (`strtol` sur 32 bits…), mathématiques (`double` et `float`), `qsort` / `bsearch`, fichiers (`fopen` … `fseek`, chemins Windows ramenés au dossier du jeu, noms sans distinction de casse), `printf` et `scanf` aux tailles de Windows (`%ls` en UTF-16, `%I64d`, `long` de 32 bits), temps |

Limites de ces fonctions : fichiers du CRT toujours en mode binaire (pas de conversion `\r\n` du mode texte), pas de paramètres régionaux, environnement vide, `scanf` sans chaînes larges ; pas d’énumération des dossiers (`FindFirstFile`), de lecture asynchrone ni de fichiers projetés en mémoire ; objets sans nom, `SuspendThread` et `TerminateThread` absents.

**Threads et ARM64** : les instructions à préfixe `lock` (et `xchg` avec la mémoire) sont traduites en opérations atomiques de l’hôte. En revanche x86 garantit l’ordre des écritures entre threads (TSO), pas ARM64 : du code qui se passe de verrou en comptant sur cet ordre peut mal se comporter. Si `ExitProcess` est appelé alors que d’autres threads tournent, ils ne sont pas arrêtés : le jeu doit avoir son propre processus (c’est le cas dans l’application).

**Ce qui manque avant un vrai jeu**, dans l’ordre prévu :

1. exceptions SEH et C++ (le point le plus difficile : dérouler la pile du jeu à travers les fonctions C traduites), les fonctions « sûres » et larges du CRT (`*_s`, `w*`), le runtime C++ de MSVC (`msvcp140`), `FindFirstFile`, chargement de DLL du jeu ;
2. fenêtre, entrées et son (`user32`, `xinput`, `xaudio2`) vers une surface Android ;
3. Direct3D : DXVK compilé pour ARM64 au-dessus du Vulkan de l’appareil (Turnip sur Adreno) ;
4. instructions : AVX, tables de sauts de MSVC en 64 bits ; l’unité x87 est traduite en double précision (pas les 80 bits du matériel) ;
5. exécutables protégés ou qui produisent du code à l’exécution (Denuvo, JIT) : hors de portée de la recompilation statique.

L’alternative à 1–3 est de charger le code traduit dans Wine ARM64, qui fournit déjà toute l’API Windows : à évaluer avant d’écrire ces fonctions une à une.

## Fonctionnement

```
jeu.exe ──► rcn-recomp ──► jeu.c ──► clang du NDK ──► libgame.so ─┐
 (x86-64)   (recompiler/)            + runtime/                    ├─► jeu.rcn.zip ──► application Android
jeu.exe ──────────────────────────────────────────────────────────┘
```

- **[`recompiler/`](recompiler/)** (Rust, [iced-x86](https://github.com/icedland/iced)) : `rcn-recomp jeu.exe -o jeu.c`. Il trouve les fonctions (point d’entrée, `.pdata`, exports, cibles des appels, adresses de code dans les données), les décode par descente récursive et émet une fonction C par fonction x86. Les registres vivent dans une structure (`RcnCtx`), la pile du jeu est une zone de mémoire ordinaire, et seuls les drapeaux lus plus loin sont calculés. Une instruction non gérée devient un arrêt qui la nomme ; le rapport de fin les compte.
- **[`runtime/`](runtime/)** (C) : charge l’exécutable d’origine (sections, relocalisations, imports) à une adresse quelconque — les pointeurs du jeu sont de vrais pointeurs de l’hôte —, fournit les fonctions Windows (tableau ci-dessus ; `setjmp` est traduit en un `setjmp` de l’hôte dans la fonction C correspondante) et les instructions trop longues pour être émises en ligne ([`rcn.h`](runtime/rcn.h)). Le code traduit ne contient aucune donnée du jeu : elles sont lues dans l’exe, qui doit être celui qui a été traduit.
- **Application Android** ([`src/`](src/)) : importe des paquets `.rcn.zip`, charge la bibliothèque du jeu et l’exécute dans un processus à part (un plantage du code traduit ne ferme que lui), en affichant sa sortie.

Le C traduit se compile avec `-fno-strict-aliasing -ffp-contract=off` : sans la seconde option, ARM64 fusionne multiplications et additions et les résultats flottants diffèrent de ceux du x86.

## Utilisation

Il faut Rust, Node.js ≥ 22, le NDK Android et, pour les tests, [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (dans le `PATH`, ou `RCN_MINGW=<dossier>`).

```bash
cd android/romCloudNative

# Test différentiel : exe d'origine contre code traduit, sur le PC puis sur l'appareil branché
node scripts/test.mjs --device

# Transposer un exécutable et déposer le paquet sur l'appareil (importé à l'ouverture de l'application)
node scripts/transpose.mjs jeu.exe --name "Mon jeu" --install --package com.romcloud.rcnative.debug

# Application
cd .. && ./gradlew :romCloudNative:assembleDebug
```

Sans `--install`, le paquet `jeu.rcn.zip` s’importe depuis l’application (*Importer un jeu*).

### Paquet `.rcn.zip`

| Fichier | Contenu |
|---|---|
| `rcn.json` | `{ "format": 1, "name", "exe", "lib", "abi": "arm64-v8a", "recompiler" }` |
| `libgame.so` | Code traduit et runtime ; exporte `rcn_port_run` ([`rcn_host.h`](runtime/rcn_host.h)) |
| l’exécutable | Exe d’origine, lu au lancement pour ses données |

Le paquet contient le code du jeu, traduit et d’origine : il se fabrique à partir de sa propre copie du jeu et ne se redistribue pas.
