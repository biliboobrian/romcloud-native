#!/usr/bin/env node
// Transpose un exécutable Windows x86-64 en paquet pour RomCloud Native (Android ARM64) :
//
//   node scripts/transpose.mjs jeu.exe [-o jeu.rcn.zip] [--name "Nom du jeu"] [--install] [--package id]
//
// 1. rcn-recomp traduit le code x86-64 de l'exe en C ;
// 2. le clang du NDK Android le compile pour ARM64 avec le runtime, en une bibliothèque ;
// 3. le paquet réunit cette bibliothèque, l'exe d'origine (ses données y sont lues) et rcn.json.
// --install dépose le paquet sur l'appareil branché (adb) : il est importé à l'ouverture de l'application.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
  androidTarget, buildRecompiler, fail, findAdb, findNdkClang, recompile, run, runtimeDir, runtimeSources,
  translatedFlags, writeZip,
} from './lib.mjs';

const args = process.argv.slice(2);
let exePath = null;
let out = null;
let name = null;
let install = false;
let appId = 'com.romcloud.rcnative';
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '-o') out = args[++i];
  else if (a === '--name') name = args[++i];
  else if (a === '--install') install = true;
  else if (a === '--package') appId = args[++i];
  else if (a.startsWith('-')) fail(`option inconnue : ${a}`);
  else exePath = a;
}
if (!exePath) fail('usage : node scripts/transpose.mjs jeu.exe [-o jeu.rcn.zip] [--name "Nom"] [--install] [--package id]');
if (!fs.existsSync(exePath)) fail(`fichier introuvable : ${exePath}`);
const exeName = path.basename(exePath);
name ??= path.basename(exeName, path.extname(exeName));
out ??= path.join(path.dirname(exePath), `${name}.rcn.zip`);

const clang = findNdkClang();
if (!clang) fail('NDK Android introuvable : définissez ANDROID_NDK_HOME ou ANDROID_HOME');
const recompiler = buildRecompiler();

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'rcn-'));
try {
  const source = path.join(work, 'game.c');
  const lib = path.join(work, 'libgame.so');
  console.log(`Traduction de ${exeName}…`);
  recompile(recompiler, exePath, source);
  console.log('Compilation pour ARM64…');
  run(clang, [
    androidTarget, '-shared', '-fPIC', '-O2', '-w', '-fvisibility=hidden', ...translatedFlags,
    '-I', runtimeDir, source, ...runtimeSources, '-lm', '-Wl,-z,max-page-size=16384', '-o', lib,
  ], { stdio: 'inherit' });
  const version = run(recompiler, ['--version'], { encoding: 'utf8' }).trim();
  const manifest = { format: 1, name, exe: exeName, lib: 'libgame.so', abi: 'arm64-v8a', recompiler: version };
  writeZip(out, [
    { name: 'rcn.json', data: Buffer.from(JSON.stringify(manifest, null, 2)) },
    { name: 'libgame.so', data: fs.readFileSync(lib) },
    { name: exeName, data: fs.readFileSync(exePath) },
  ]);
  console.log(`Paquet : ${out} (${(fs.statSync(out).size / 1024).toFixed(0)} Ko)`);
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}

if (install) {
  const adb = findAdb();
  if (!adb) fail('adb introuvable');
  const inbox = `/sdcard/Android/data/${appId}/files/import`;
  run(adb, ['shell', 'mkdir', '-p', inbox], { stdio: 'inherit' });
  run(adb, ['push', out, `${inbox}/${path.basename(out)}`], { stdio: 'inherit' });
  console.log(`Déposé sur l'appareil : ouvrez RomCloud Native (${appId}) pour l'importer.`);
}
