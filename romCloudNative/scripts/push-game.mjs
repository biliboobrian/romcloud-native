#!/usr/bin/env node
// Envoie un jeu PC sur l'appareil branché (adb) : son dossier est mis en archive .zip et déposé dans le
// dossier d'import de RomCloud Native, qui le copie et le traduit à son ouverture.
//
//   node scripts/push-game.mjs <dossier du jeu> [--name "Nom"] [--package com.romcloud.rcnative.debug]
//
// L'archive (sans le dossier) peut aussi être importée à la main depuis l'application.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fail, findAdb, run, writeZip } from './lib.mjs';

const args = process.argv.slice(2);
let folder = null;
let name = null;
let appId = 'com.romcloud.rcnative';
let keep = false;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--name') name = args[++i];
  else if (a === '--package') appId = args[++i];
  else if (a === '--keep') keep = true;
  else if (a.startsWith('-')) fail(`option inconnue : ${a}`);
  else folder = a;
}
if (!folder || !fs.statSync(folder, { throwIfNoEntry: false })?.isDirectory()) {
  fail('usage : node scripts/push-game.mjs <dossier du jeu> [--name "Nom"] [--package id] [--keep]');
}
name ??= path.basename(path.resolve(folder));
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