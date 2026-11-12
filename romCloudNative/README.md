# RomCloud Native

Prototype de recherche : exécuter des jeux PC sur Android **sans émulation du processeur**. À l’import d’un jeu, l’application traduit elle-même le code x86 (32 bits) ou x86-64 de son exécutable en code machine ARM64, une fois pour toutes (recompilation statique, en quelques secondes) ; le jeu s’exécute ensuite comme du code natif, avec un runtime qui fournit les fonctions Windows dont il a besoin. RomCloud PC (Winlator : Box64 + Wine) traduit, lui, le code pendant que le jeu tourne.

## État

**Ce qui fonctionne** : des programmes Windows en mode console, x86-64 ou x86 32 bits, à plusieurs threads. Quatre programmes de test produisent après traduction la même sortie que l’exe d’origine, par les deux générateurs du recompilateur (voir *Fonctionnement*), sur PC et sur téléphone ARM64 (`node scripts/test.mjs --device`) :

- [`samples/demo/demo.c`](samples/demo/demo.c), sans bibliothèque C (`kernel32` seulement) : récursion, tables de sauts, pointeurs de fonctions, arithmétique 64 et 128 bits, flottants SSE2, tas ;
- le même en **x86 32 bits sans SSE** (`demo32`) : arguments sur la pile, flottants par l’unité x87, image chargée à son adresse d’origine ;
- [`samples/crt/crt.c`](samples/crt/crt.c), un programme C ordinaire lié à l’UCRT : démarrage du CRT, `printf` / `snprintf` / `sscanf`, chaînes, tas, fichiers, `qsort` et `atexit` (l’hôte rappelle le jeu), mathématiques, variables locales au thread, `setjmp` / `longjmp` ;
- [`samples/win32/win32.c`](samples/win32/win32.c), les fonctions de `kernel32` : fichiers, threads, opérations atomiques, sections critiques, événements, mutex, sémaphores, verrous légers, variables de condition, texte UTF-16.

**Premier jeu : Warning Forever** (2005, x86 32 bits, Direct3D 7). Traduit à 99,75 % (107 780 instructions) en une seconde sur PC, il tourne sur PC depuis le code machine traduit : logo, écran titre, partie jouable avec une manette simulée, rendu par le GPU et son (images enregistrées par `RCN_FRAMES`, voir [`rcn_gui.c`](runtime/rcn_gui.c)). Sur le téléphone, la version traduite en C sur PC s’est lancée et tournait de façon fluide en rendu logiciel ; la traduction sur l’appareil, le son, le rendu par le GPU et la manette n’y ont pas encore été essayés.

| Bibliothèque | Fichier | Ce qui est fourni |
|---|---|---|
| `user32`, `winmm` | [`rcn_user32.c`](runtime/rcn_user32.c) | une fenêtre plein écran, file de messages, boîte de dialogue validée d’office avec ses réglages par défaut, `timeGetTime`, minuteries (`timeSetEvent`) |
| `ddraw`, `gdi32` | [`rcn_ddraw.c`](runtime/rcn_ddraw.c) | DirectDraw 7 : surfaces de 16 et 32 bits, `Blt` / `BltFast` (agrandissement, couleur transparente, miroirs), `Flip` à 60 images par seconde, `Lock` ; texte sur une surface avec une police fixe intégrée |
| Direct3D 7 | [`rcn_d3d.c`](runtime/rcn_d3d.c), [`rcn_gl.c`](runtime/rcn_gl.c) | triangles, lignes et points : couleurs interpolées, texture de l’étage 0, test d’opacité, mélanges ; sans tampon de profondeur ni éclairage. **Par le GPU** (OpenGL ES 3 dans la fenêtre de l’application, à la résolution de l’écran) quand l’hôte fournit une fenêtre, sinon rendu logiciel. Sur PC, `RCN_GL=1` passe par OpenGL et relit les images, pour comparer les deux rendus |
| `dinput` | [`rcn_dinput.c`](runtime/rcn_dinput.c) | la manette de l’hôte vue comme manette et comme clavier (croix → flèches, A → Z, B → X, Y → D, Select → Échap) |
| `dsound` | [`rcn_dsound.c`](runtime/rcn_dsound.c) | tampons de son (8 / 16 bits, mono / stéréo), volume, balance, fréquence, lecture en boucle, mélangés à 44,1 kHz vers AAudio ; sur PC, `RCN_AUDIO=son.wav` enregistre le son |

Ces bibliothèques ne sont proposées qu’aux jeux 32 bits.

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

1. exceptions SEH et C++ (le point le plus difficile : dérouler la pile du jeu à travers les fonctions traduites), les fonctions « sûres » et larges du CRT (`*_s`, `w*`), le runtime C++ de MSVC (`msvcp140`), `FindFirstFile`, chargement de DLL du jeu ;
2. fenêtre, entrées et son (`user32`, `xinput`, `xaudio2`) vers une surface Android ;
3. Direct3D : DXVK compilé pour ARM64 au-dessus du Vulkan de l’appareil (Turnip sur Adreno) ;
4. instructions : AVX, tables de sauts de MSVC en 64 bits ; l’unité x87 est traduite en double précision (pas les 80 bits du matériel) ;
5. exécutables protégés ou qui produisent du code à l’exécution (Denuvo, JIT) : hors de portée de la recompilation statique.

L’alternative à 1–3 est de charger le code traduit dans Wine ARM64, qui fournit déjà toute l’API Windows : à évaluer avant d’écrire ces fonctions une à une.

## Fonctionnement

```
                               ┌── code machine (Cranelift) ──► jeu.rcnx ──► runtime : rcn_run_native
jeu.exe ──► rcn-recomp ────────┤        (sur l’appareil, dans l’application)
 (x86)      (recompiler/)      └── C (emit.rs) ──► clang + runtime ──► programme de test (PC, adb)
```

- **[`recompiler/`](recompiler/)** (Rust, [iced-x86](https://github.com/icedland/iced), [Cranelift](https://cranelift.dev)). Il trouve les fonctions (point d’entrée, `.pdata`, exports, cibles des appels, adresses de code dans les données) et les décode par descente récursive. Les registres du jeu vivent dans une structure (`RcnCtx`), sa pile est une zone de mémoire ordinaire, seuls les drapeaux lus plus loin sont calculés ; une instruction non gérée devient un arrêt qui la nomme, et le rapport de fin les compte. Deux générateurs, de même sémantique :
  - **code machine** ([`native.rs`](recompiler/src/native.rs)) : chaque fonction x86 devient une fonction de l’hôte compilée par Cranelift (ARM64 sur le téléphone, x86-64 sur PC pour les tests). Les registres sont gardés dans ceux de l’hôte et recopiés dans `RcnCtx` à chaque écriture ; les opérations longues (SSE sur 128 bits, fonctions du x87, divisions) appellent le runtime ([`rcn_ops.c`](runtime/rcn_ops.c)). Résultat : un fichier `.rcnx` (code et endroits à compléter au chargement), que le runtime charge en mémoire exécutable ([`rcn_native.c`](runtime/rcn_native.c)). C’est ce générateur que l’application embarque (`librcn_recomp.so`) ;
  - **C** ([`emit.rs`](recompiler/src/emit.rs)) : une fonction C par fonction x86, compilée avec le runtime en un programme de test (voir `scripts/test.mjs`) ; il sert de référence au premier.
- **[`runtime/`](runtime/)** (C) : charge l’exécutable d’origine (sections, relocalisations, imports) — les pointeurs du jeu sont de vrais pointeurs de l’hôte —, fournit les fonctions Windows (tableaux ci-dessus) et les instructions trop longues pour être émises en ligne ([`rcn.h`](runtime/rcn.h)). Le code traduit ne contient aucune donnée du jeu : elles sont lues dans l’exe, qui doit être celui qui a été traduit. `setjmp` / `longjmp` : dans le code machine, `longjmp` rétablit les registres puis les fonctions traduites remontent jusqu’à celle qui a appelé `setjmp`.
- **Application Android** ([`src/`](src/)) : importe un jeu (son dossier, ou une archive `.zip`), demande quel exécutable traduire s’il y en a plusieurs, le traduit sur l’appareil (barre de progression par fonction), puis l’exécute dans un processus à part (un plantage du code traduit ne ferme que lui). Le runtime est dans `librcnhost.so`. Un jeu traduit par une autre version du recompilateur est retraduit avant d’être lancé.

Le C traduit se compile avec `-fno-strict-aliasing -ffp-contract=off` : sans la seconde option, ARM64 fusionne multiplications et additions et les résultats flottants diffèrent de ceux du x86 ; le code machine de Cranelift ne les fusionne pas.

## Utilisation

Il faut Rust (avec la cible `aarch64-linux-android` : `rustup target add aarch64-linux-android`), Node.js ≥ 22, le NDK Android et, pour les tests, [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (dans le `PATH`, ou `RCN_MINGW=<dossier>`).

```bash
cd android/romCloudNative

# Tests différentiels : exe d'origine contre code traduit (C et code machine), sur le PC puis sur l'appareil
node scripts/test.mjs --device

# Traduction sur PC (mise au point) : code machine ou C, selon l'extension
recompiler/target/release/rcn-recomp jeu.exe -o jeu.rcnx [--target aarch64]
recompiler/target/release/rcn-recomp jeu.exe -o jeu.c

# Envoyer un jeu sur l'appareil branché : il est importé et traduit à l'ouverture de l'application
node scripts/push-game.mjs "C:/Jeux/Mon jeu" --package com.romcloud.rcnative.debug

# Application (compile aussi le recompilateur pour ARM64)
cd .. && ./gradlew :romCloudNative:assembleDebug
```

Les jeux importés se trouvent dans les fichiers privés de l’application, un dossier chacun : les fichiers du jeu, `game.rcnx` (code traduit) et `rcn.json` (`{ "format": 2, "name", "exe", "code", "recompiler" }`). Le code traduit vient de votre propre copie du jeu et reste sur l’appareil.

Toute modification de `RcnCtx` ou des fonctions appelées par le code machine doit s’accompagner d’un changement de version du recompilateur (`recompiler/Cargo.toml`), pour que les jeux déjà importés soient retraduits.