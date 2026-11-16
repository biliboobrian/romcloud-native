#!/usr/bin/env node
// Construit le lanceur ARM64 en ligne de commande (rcn_native, avec OpenGL ES) sans rien pousser :
//   node scripts/build-cli-android.mjs <sortie>
// Sert à lancer un jeu déjà installé dans l'application (run-as) pour le mettre au point.
import path from 'node:path';
import { androidTarget, fail, findNdkClang, run, runtimeDir, runtimeSources, translatedFlags } from './lib.mjs';

const out = process.argv[2];
if (!out) fail('usage : build-cli-android.mjs <sortie>');
const cc = findNdkClang();
if (!cc) fail('NDK introuvable');
run(cc, [androidTarget, '-O2', '-w', ...translatedFlags, '-DRCN_NATIVE', '-I', runtimeDir, ...runtimeSources,
  path.join(runtimeDir, 'rcn_cli.c'), '-lEGL', '-lGLESv3', '-landroid', '-llog', '-lm', '-o', out], { stdio: 'inherit' });
