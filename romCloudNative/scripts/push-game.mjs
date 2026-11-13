#!/usr/bin/env node
// Envoie un jeu PC sur l'appareil branché (adb) : son dossier est mis en archive .zip et déposé dans le
// dossier d'import de RomCloud Native, qui le copie et le traduit à son ouverture.
//
//   node scripts/push-game.mjs <dossier du jeu> [--name "Nom"] [--package com.romcloud.rcnative.debug] [--folder]
//
// L'archive (sans le dossier) peut aussi être importée à la main depuis l'application.
// --folder : pas d'archive (gros jeux) ; le dossier est poussé tel quel (adb push --sync), sous un nom
// commençant par « . » tant que l'envoi dure, puis renommé : l'application ne l'importe qu'une fois complet.
// Les bibliothèques de plateformes en ligne (steam_api*, Galaxy*, EOSSDK*…) ne sont jamais poussées.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fail, findAdb, run, writeZip } from './lib.mjs';

const args = process.argv.slice(2);
let folder = null;
let name = null;
let appId = 'com.romcloud.rcnative';
let keep = false;
let asFolder = false;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--name') name = args[++i];
  else if (a === '--package') appId = args[++i];
  else if (a === '--keep') keep = true;
  else if (a === '--folder') asFolder = true;
  else if (a.startsWith('-')) fail(`option inconnue : ${a}`);
  else folder = a;
}
if (!folder || !fs.statSync(folder, { throwIfNoEntry: false })?.isDirectory()) {
  fail('usage : node scripts/push-game.mjs <dossier du jeu> [--name "Nom"] [--package id] [--keep]');
}
name ??= path.basename(path.resolve(folder));
if (asFolder) {
  pushFolder();
  process.exit(0);
}
const entries = [];
for (const file of fs.readdirSync(folder, { recursive: true })) {
  const full = path.join(folder, file);
  if (fs.statSync(full).isFile()) entries.push({ name: file.split(path.sep).join('/'), data: fs.readFileSync(full) });
}
const zip = path.join(keep ? process.cwd() : os.tmpdir(), `${name}.zip`);
writeZip(zip, entries);
console.log(`Archive : ${zip} (${(fs.statSync(zip).size / 1024).toFixed(0)} Ko, ${entries.length} fichiers)`);
const adb = findAdb();
if (!adb) fail('adb introuvable');
const inbox = `/sdcard/Android/data/${appId}/files/import`;
run(adb, ['shell', 'mkdir', '-p', inbox], { stdio: 'inherit' });
run(adb, ['push', zip, `${inbox}/${name}.zip`], { stdio: 'inherit' });
if (!keep) fs.rmSync(zip);
console.log(`Déposé sur l'appareil : ouvrez RomCloud Native (${appId}) pour l'importer et le traduire.`);
function pushFolder() {
  const adb = findAdb();
  if (!adb) fail('adb introuvable');
  const inbox = `/sdcard/Android/data/${appId}/files/import`;
  const temp = `${inbox}/.envoi-${name}`;
  const platformLib = /^(steam_api|steamclient|galaxy|eossdk|discord_game_sdk)|steam_emu|\.rne$|\.cdx$/i;
  const excluded = (rel) => platformLib.test(path.basename(rel));
  const hasExcluded = (full) => fs.readdirSync(full, { recursive: true }).some((e) => excluded(e));
  const pushOne = (f, remote) => run(adb, ['push', '--sync', f, remote], { stdio: ['ignore', 'ignore', 'inherit'] });
  const pushTree = (full, remote) => {
    if (!hasExcluded(full)) {
      console.log(`→ ${path.relative(folder, full)}/ (synchronisation)`);
      run(adb, ['shell', 'mkdir', '-p', `"${path.posix.dirname(remote)}"`]);
      pushOne(full, path.posix.dirname(remote));
      return;
    }
    run(adb, ['shell', 'mkdir', '-p', `"${remote}"`]);
    for (const child of fs.readdirSync(full)) {
      if (excluded(child)) continue;
      const f = path.join(full, child);
      if (fs.statSync(f).isDirectory()) pushTree(f, `${remote}/${child}`);
      else pushOne(f, `${remote}/${child}`);
    }
  };
  run(adb, ['shell', 'mkdir', '-p', `"${temp}"`]);
  for (const child of fs.readdirSync(folder)) {
    if (excluded(child) || /^_redist$/i.test(child)) continue;
    const f = path.join(folder, child);
    if (fs.statSync(f).isDirectory()) pushTree(f, `${temp}/${child}`);
    else pushOne(f, `${temp}/${child}`);
  }
  run(adb, ['shell', 'rm', '-rf', `"${inbox}/${name}"`]);
  run(adb, ['shell', 'mv', `"${temp}"`, `"${inbox}/${name}"`]);
  console.log(`Déposé sur l'appareil : ouvrez RomCloud Native (${appId}) pour l'importer et le traduire.`);
}
