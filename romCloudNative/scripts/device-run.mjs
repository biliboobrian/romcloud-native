#!/usr/bin/env node
// Essai d'un jeu traduit sur l'appareil branché, en ligne de commande (adb), sans passer par l'application :
// lanceur ARM64 construit avec le NDK, code traduit (.rcnx) et fichiers du jeu poussés dans
// /data/local/tmp/rcn-<nom>, jeu lancé avec les variables de suivi, journaux et images rapatriés.
// Les fichiers déjà présents sur l'appareil (même taille) ne sont pas renvoyés.
//
//   node scripts/device-run.mjs --game <dossier du jeu> --code <dossier des .rcnx ARM64> --exe <nom.exe>
//        [--seconds 120] [--env NOM=valeur]… [--out <dossier local>] [--no-data]
//
// Les bibliothèques de plateformes en ligne (steam_api*, steamclient, EOSSDK*, Discord) ne sont jamais poussées.
// GOG Galaxy fait exception : les jeux GOG sont sans DRM, Galaxy n'y est qu'un SDK en ligne facultatif
// (hors ligne, la connexion échoue proprement) dont le jeu a besoin pour démarrer.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {
  androidTarget, fail, findAdb, findNdkClang, root, run, runtimeDir, runtimeSources, translatedFlags,
} from './lib.mjs';

const args = process.argv.slice(2);
const opt = { seconds: 120, env: [], out: null, data: true };
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--game') opt.game = args[++i];
  else if (a === '--code') opt.code = args[++i];
  else if (a === '--exe') opt.exe = args[++i];
  else if (a === '--seconds') opt.seconds = Number(args[++i]);
  else if (a === '--env') opt.env.push(args[++i]);
  else if (a === '--out') opt.out = args[++i];
  else if (a === '--no-data') opt.data = false;
  else fail(`option inconnue : ${a}`);
}
if (!opt.game || !opt.code || !opt.exe) fail('usage : device-run.mjs --game <dossier> --code <dossier .rcnx> --exe <nom.exe>');
const adb = findAdb();
const cc = findNdkClang();
if (!adb || !cc) fail('adb ou NDK introuvable');
if (!run(adb, ['devices'], { encoding: 'utf8' }).split('\n').slice(1).some((l) => /\tdevice/.test(l))) fail('aucun appareil branché');

const name = path.basename(path.resolve(opt.game)).replace(/[^A-Za-z0-9]+/g, '-').toLowerCase();
const dir = `/data/local/tmp/rcn-${name}`;
const outDir = opt.out ?? path.join(root, 'build', `device-${name}`);
fs.mkdirSync(outDir, { recursive: true });
const platformLib = /^(steam_api|steamclient|eossdk|discord_game_sdk)/i;

// Lanceur ARM64.
const cli = path.join(outDir, 'rcn_native');
run(cc, [androidTarget, '-O2', '-w', ...translatedFlags, '-DRCN_NATIVE', '-I', runtimeDir, ...runtimeSources,
  path.join(runtimeDir, 'rcn_cli.c'), '-lEGL', '-lGLESv3', '-landroid', '-llog', '-lm', '-o', cli], { stdio: 'inherit' });
run(adb, ['shell', 'mkdir', '-p', dir]);

// Tailles des fichiers déjà sur l'appareil.
const remote = new Map();
for (const line of run(adb, ['shell', `cd ${dir} && find . -type f -exec stat -c '%s %n' {} +`], { encoding: 'utf8' }).split('\n')) {
  const m = line.trim().match(/^(\d+) \.\/(.*)$/);
  if (m) remote.set(m[2], Number(m[1]));
}
function push(local, rel, always = false) {
  const size = fs.statSync(local).size;
  if (!always && remote.get(rel) === size) return;
  if (rel.includes('/')) run(adb, ['shell', 'mkdir', '-p', `"${dir}/${path.posix.dirname(rel)}"`]);
  console.log(`→ ${rel} (${(size / 1048576).toFixed(1)} Mo)`);
  run(adb, ['push', local, `${dir}/${rel}`], { stdio: ['ignore', 'ignore', 'inherit'] });
}
push(cli, 'rcn_native', true); // toujours : un lanceur modifié peut garder la même taille
run(adb, ['shell', 'chmod', '755', `${dir}/rcn_native`]);
for (const f of fs.readdirSync(opt.code)) {
  if (f.endsWith('.rcnx') && !platformLib.test(f)) push(path.join(opt.code, f), f);
}
// Fichiers du jeu : exécutables et DLL de la racine, un par un ; dossiers de données (sauf --no-data) en
// « adb push --sync » (seuls les fichiers modifiés partent). Un dossier qui contient une bibliothèque de
// plateforme en ligne est poussé élément par élément, sans elle : elle ne quitte jamais le PC.
const excluded = (rel) => platformLib.test(path.basename(rel)) || /steam_emu|\.rne$|\.cdx$/i.test(rel);
const hasExcluded = (full) => fs.readdirSync(full, { recursive: true }).some((e) => excluded(e.split(path.sep).join('/')));
function pushTree(full, rel) {
  if (!hasExcluded(full)) {
    console.log(`→ ${rel}/ (synchronisation)`);
    const parent = path.posix.dirname(rel);
    run(adb, ['shell', 'mkdir', '-p', `"${dir}/${parent}"`]);
    run(adb, ['push', '--sync', full, parent === '.' ? dir : `${dir}/${parent}`], { stdio: ['ignore', 'ignore', 'inherit'] });
    return;
  }
  for (const child of fs.readdirSync(full)) {
    const f = path.join(full, child), r = `${rel}/${child}`;
    if (excluded(r)) continue;
    if (fs.statSync(f).isDirectory()) pushTree(f, r);
    else push(f, r);
  }
}
for (const entry of fs.readdirSync(opt.game)) {
  const full = path.join(opt.game, entry);
  const st = fs.statSync(full);
  if (st.isFile() && !excluded(entry) && /\.(exe|dll)$/i.test(entry)) push(full, entry);
  if (st.isDirectory() && opt.data && !/^_redist$/i.test(entry)) pushTree(full, entry);
}

// Lancement.
const env = ['RCN_FRAMES=frames', 'RCN_FRAME_EVERY=120', 'RCN_D3D_LOG=1', ...opt.env].join(' ');
run(adb, ['shell', `cd ${dir} && rm -rf frames && mkdir frames && rm -f code`]);
console.log(`lancement (${opt.seconds} s)…`);
// Jeu détaché de la session adb (setsid nohup) : un débranchement du câble ne l'arrête pas. Fin attendue en
// interrogeant le fichier « code », en tolérant les coupures d'adb.
fs.writeFileSync(path.join(outDir, 'launch.sh'),
  `cd ${dir}
${env} timeout ${opt.seconds} ./rcn_native game.rcnx "${opt.exe}" > out.txt 2> err.txt
echo $? > code
`);
run(adb, ['push', path.join(outDir, 'launch.sh'), `${dir}/launch.sh`], { stdio: 'ignore' });
run(adb, ['shell', `cd ${dir} && setsid nohup sh launch.sh > /dev/null 2>&1 < /dev/null &`]);
const deadline = Date.now() + (opt.seconds + 120) * 1000;
for (;;) {
  const sleep = new Int32Array(new SharedArrayBuffer(4));
  Atomics.wait(sleep, 0, 0, 5000);
  let done = '';
  try {
    done = execFileSync(adb, ['shell', `cat ${dir}/code 2>/dev/null`], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
  } catch {
    try {
      execFileSync(adb, ['wait-for-device'], { stdio: 'ignore', timeout: 60000 });
    } catch {}
  }
  if (done) break;
  if (Date.now() > deadline) {
    console.log("le jeu ne s'est pas terminé à temps");
    break;
  }
}
for (const f of ['out.txt', 'err.txt', 'code']) run(adb, ['pull', `${dir}/${f}`, path.join(outDir, f)], { stdio: 'ignore' });
fs.rmSync(path.join(outDir, 'frames'), { recursive: true, force: true });
run(adb, ['pull', `${dir}/frames`, outDir], { stdio: 'ignore' });
console.log(`code de sortie ${fs.readFileSync(path.join(outDir, 'code'), 'utf8').trim()} ; journaux et images dans ${outDir}`);
