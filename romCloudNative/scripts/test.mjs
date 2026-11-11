#!/usr/bin/env node
// Tests différentiels du recompilateur : chaque programme de samples/ est compilé en exe Windows
// x86-64, traduit en C, recompilé, et sa sortie doit être identique à celle de l'original
// (samples/<nom>/expected.txt).
//
//   node scripts/test.mjs            sur le PC (code traduit recompilé pour le PC)
//   node scripts/test.mjs --device   aussi sur l'appareil Android branché (ARM64, par adb)
//   node scripts/test.mjs --update   réécrit les expected.txt depuis les exe d'origine (Windows seulement)
//
// Il faut llvm-mingw (x86_64-w64-mingw32-clang dans le PATH, ou RCN_MINGW=<dossier>), Rust et,
// pour --device, le NDK Android.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {
  androidTarget, buildRecompiler, fail, findAdb, findHostCompiler, findMingw, findNdkClang, recompile, root, run,
  runtimeDir, runtimeSources, translatedFlags,
} from './lib.mjs';

const samples = [
  {
    // Sans bibliothèque C : kernel32 seulement. Sortie comparée octet pour octet.
    name: 'demo',
    flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-Wl,-e,start'],
    libs: ['-lkernel32'],
    exit: 42,
  },
  {
    // Programme C ordinaire lié à l'UCRT. Sa sortie standard est en mode texte sous Windows (fins de
    // ligne « \r\n », que le runtime n'imite pas) : comparée sans les retours chariot.
    name: 'crt',
    flags: [],
    libs: [],
    exit: 7,
    text: true,
  },
  {
    // Fonctions de kernel32 : fichiers, threads, synchronisation, texte.
    name: 'win32',
    flags: [],
    libs: [],
    exit: 3,
    text: true,
  },
];

const onDevice = process.argv.includes('--device');
const update = process.argv.includes('--update');
const work = path.join(root, 'build', 'rcn-test');
fs.mkdirSync(work, { recursive: true });
const cli = path.join(runtimeDir, 'rcn_cli.c');

const mingw = findMingw();
if (!mingw) fail('llvm-mingw introuvable (x86_64-w64-mingw32-clang) : ajoutez-le au PATH ou définissez RCN_MINGW');
const hostCc = findHostCompiler();
if (!hostCc) fail('compilateur C introuvable pour le PC');
const ndkClang = onDevice ? findNdkClang() : null;
const adb = onDevice ? findAdb() : null;
if (onDevice && (!ndkClang || !adb)) fail('NDK Android ou adb introuvable');
const recompiler = buildRecompiler();
const deviceDir = '/data/local/tmp/rcn-test';
let model = '';
if (onDevice) {
  model = run(adb, ['shell', 'getprop', 'ro.product.model'], { encoding: 'utf8' }).trim();
  run(adb, ['shell', 'mkdir', '-p', deviceDir]);
}

/** Exécute un programme dans le dossier de travail ; renvoie sa sortie et son code de sortie. */
function capture(file, args) {
  const r = spawnSync(file, args, { maxBuffer: 1 << 26, cwd: work });
  if (r.error) fail(`${file} : ${r.error.message}`);
  return { out: r.stdout, code: r.status, err: r.stderr.toString() };
}

const normalize = (sample, out) => (sample.text ? Buffer.from(out.toString('latin1').replaceAll('\r\n', '\n'), 'latin1') : out);

// Threads : bibliothèque liée dans l'exe sous Windows (pas de DLL de llvm-mingw à trouver), déjà dans la
// bibliothèque C d'Android.
const threadFlags = (target) => (target.length ? [] : process.platform === 'win32' ? ['-pthread', '-static'] : ['-pthread']);

let failures = 0;
function check(sample, expected, label, result) {
  const out = normalize(sample, result.out);
  const ok = result.code === sample.exit && Buffer.compare(out, expected) === 0;
  console.log(`${ok ? 'OK    ' : 'ÉCHEC '} ${sample.name} : ${label}`);
  if (ok) return;
  failures++;
  console.log(`       code de sortie ${result.code} (attendu ${sample.exit}), ${out.length} octets (attendu ${expected.length})`);
  // Première ligne qui diffère.
  const got = out.toString('latin1').split('\n');
  const want = expected.toString('latin1').split('\n');
  const line = want.findIndex((l, i) => l !== got[i]);
  if (line >= 0) console.log(`       ligne ${line + 1} : attendu « ${want[line]} », obtenu « ${got[line] ?? '(fin)'} »`);
  if (result.err) console.log(`       ${result.err.trim().split('\n').slice(-3).join('\n       ')}`);
}

for (const sample of samples) {
  const dir = path.join(root, 'samples', sample.name);
  const expectedFile = path.join(dir, 'expected.txt');
  const exe = path.join(work, `${sample.name}.exe`);
  run(mingw, ['-O2', ...sample.flags, '-o', exe, path.join(dir, `${sample.name}.c`), ...sample.libs], { stdio: 'inherit' });

  if (update) {
    if (process.platform !== 'win32') fail("--update exécute les exe d'origine : Windows seulement");
    const ref = capture(exe, []);
    if (ref.code !== sample.exit) fail(`${sample.name}.exe s'est terminé avec le code ${ref.code}`);
    fs.writeFileSync(expectedFile, normalize(sample, ref.out));
  }
  const expected = fs.readFileSync(expectedFile);
  if (process.platform === 'win32') check(sample, expected, "exe d'origine (x86-64, Windows)", capture(exe, []));

  const source = path.join(work, `${sample.name}_recomp.c`);
  recompile(recompiler, exe, source, { comments: true });
  const compile = (cc, target, out) =>
    run(cc, [...target, '-O1', '-w', ...translatedFlags, ...threadFlags(target), '-I', runtimeDir, source, ...runtimeSources, cli, '-lm', '-o', out], { stdio: 'inherit' });

  const hostBin = path.join(work, `${sample.name}_native${process.platform === 'win32' ? '.exe' : ''}`);
  compile(hostCc, [], hostBin);
  check(sample, expected, `code traduit, recompilé pour le PC (${process.arch})`, capture(hostBin, [exe]));

  if (onDevice) {
    const bin = path.join(work, `${sample.name}_arm64`);
    compile(ndkClang, [androidTarget], bin);
    run(adb, ['push', bin, `${deviceDir}/${sample.name}`]);
    run(adb, ['push', exe, `${deviceDir}/${sample.name}.exe`]);
    run(adb, ['shell', 'chmod', '755', `${deviceDir}/${sample.name}`]);
    // exec-out : sortie binaire intacte ; le code de sortie est écrit à part, la sortie d'erreur ignorée.
    const out = run(adb, ['exec-out', `cd ${deviceDir} && ./${sample.name} ${sample.name}.exe 2>/dev/null; echo $? > code`]);
    const code = Number(run(adb, ['shell', 'cat', `${deviceDir}/code`], { encoding: 'utf8' }).trim());
    check(sample, expected, `code traduit sur l'appareil (ARM64, ${model})`, { out, code, err: '' });
  }
}

if (onDevice) run(adb, ['shell', 'rm', '-r', deviceDir]);
process.exit(failures ? 1 : 0);
