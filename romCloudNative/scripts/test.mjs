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
  runtimeDir, runtimeSources, linkedSources, translatedFlags,
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
    // Le même, en x86 32 bits sans SSE : arguments sur la pile, flottants par l'unité x87, divisions de
    // 64 bits par des fonctions du compilateur. Même sortie attendue.
    name: 'demo',
    bits: 32,
    flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-mno-sse', '-Wl,-e,_start'],
    libs: ['-lkernel32'],
    builtins: true,
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
  {
    // Exceptions C++ (déroulement SEH de Windows x64 : RaiseException, RtlUnwindEx, reprise dans un cadre
    // appelant). Code machine seulement : le générateur C ne fait pas de reprise non locale.
    name: 'exc',
    cpp: true,
    nativeOnly: true,
    flags: ['-static'],
    libs: [],
    exit: 0,
    text: true,
  },
  {
    // x86 32 bits : DLL du jeu liée à la même adresse que l'exécutable, donc chargée ailleurs et relocalisée
    // (adresses absolues du code traduit rendues relatives à sa base réelle). Sans bibliothèque C.
    name: 'dll32',
    label: 'dll32',
    bits: 32,
    nativeOnly: true,
    dll: { name: 'dll32lib', flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-Wl,--image-base=0x400000', '-Wl,-e,_DllMain@12'] },
    flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-Wl,-e,_start'],
    libs: ['-lkernel32'],
    exit: 5,
  },
  {
    // x86 32 bits : « steam_api.dll » (API flat de Steamworks, convention cdecl), fournie par le runtime
    // (rcn_steam.c) — le DLL n'existe pas sous Windows, donc pas de comparaison avec l'exe d'origine.
    name: 'steam32',
    label: 'steam32',
    bits: 32,
    noOriginal: true,
    flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-Wl,-e,_start'],
    libs: ['-lkernel32'],
    exit: 0,
  },
  {
    // x86 32 bits : ancienne API Steam (avant 2013). « SteamUser », « SteamApps »… rendent une interface
    // dont les méthodes s'appellent par la table virtuelle (__thiscall : la méthode retire ses arguments),
    // fournie par le runtime (rcn_steam.c + tables générées) — pas d'exe d'origine, le DLL n'existe pas.
    name: 'steam32legacy',
    label: 'steam32legacy',
    cpp: true,
    bits: 32,
    noOriginal: true,
    flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-Wl,-e,_start'],
    libs: ['-lkernel32'],
    exit: 0,
  },
  {
    // x86 32 bits : Direct3D 9 (rcn_dxvk_d3d9.c) servi par le Direct3D 9 de Windows sur le PC — appareil,
    // ressources, verrous (copies sous 2 Go), rendu d'un carré texturé relu pixel par pixel. Sur le PC
    // seulement (le téléphone n'a de fenêtre que dans l'application).
    name: 'd3d9',
    label: 'd3d9',
    bits: 32,
    nativeOnly: true,
    pcOnly: true,
    env: { RCN_D3D: 'dxvk' },
    flags: ['-ffreestanding', '-nostdlib', '-mno-stack-arg-probe', '-Wl,-e,_start'],
    libs: ['-ld3d9', '-luser32', '-lkernel32'],
    exit: 0,
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
function capture(file, args, env) {
  const r = spawnSync(file, args, { maxBuffer: 1 << 26, cwd: work, env: env ? { ...process.env, ...env } : process.env });
  if (r.error) fail(`${file} : ${r.error.message}`);
  return { out: r.stdout, code: r.status, err: r.stderr.toString() };
}

const normalize = (sample, out) => (sample.text ? Buffer.from(out.toString('latin1').replaceAll('\r\n', '\n'), 'latin1') : out);

// Threads : bibliothèque liée dans l'exe sous Windows (pas de DLL de llvm-mingw à trouver), déjà dans la
// bibliothèque C d'Android.
// Rendu GPU (rcn_gl.c) : OpenGL sous Windows, EGL et OpenGL ES sous Android.
const threadFlags = (target) =>
  target.length ? ['-lEGL', '-lGLESv3', '-landroid', '-llog'] : process.platform === 'win32' ? ['-pthread', '-static', '-lopengl32', '-lgdi32', '-luser32'] : ['-pthread'];

let failures = 0;

// Lanceurs du code machine traduit (.rcnx) : le runtime seul, sans jeu, pour le PC et pour l'appareil.
const nativeCli = (cc, target, out) =>
  run(cc, [...target, '-O2', '-w', ...translatedFlags, ...threadFlags(target), '-DRCN_NATIVE', '-I', runtimeDir, ...runtimeSources, cli, '-lm', '-o', out], { stdio: 'inherit' });
const hostNative = path.join(work, `rcn_native${process.platform === 'win32' ? '.exe' : ''}`);
nativeCli(hostCc, [], hostNative);
const deviceNative = path.join(work, 'rcn_native_arm64');
if (onDevice) {
  nativeCli(ndkClang, [androidTarget], deviceNative);
  run(adb, ['push', deviceNative, `${deviceDir}/rcn_native`]);
  run(adb, ['shell', 'chmod', '755', `${deviceDir}/rcn_native`]);
}
function check(sample, expected, label, result) {
  const out = normalize(sample, result.out);
  const ok = result.code === sample.exit && Buffer.compare(out, expected) === 0;
  console.log(`${ok ? 'OK    ' : 'ÉCHEC '} ${sample.id} : ${label}`);
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
  // Nom des fichiers produits et des lignes du rapport : « demo », « demo32 ».
  const id = sample.label ?? sample.name + (sample.bits === 32 ? '32' : '');
  sample.id = id;
  const exe = path.join(work, `${id}.exe`);
  const cc = (sample.bits === 32 ? findMingw(32) : mingw)?.replace(/clang(\.exe)?$/, sample.cpp ? 'clang++$1' : 'clang$1');
  if (!cc) fail('i686-w64-mingw32-clang introuvable');
  // Fonctions d'appoint du compilateur (divisions de 64 bits en 32 bits), à lier à la main sans bibliothèque C.
  const builtins = sample.builtins ? [run(cc, ['--print-libgcc-file-name'], { encoding: 'utf8' }).trim()] : [];
  // DLL du jeu : compilée d'abord (l'exécutable se lie à sa bibliothèque d'import), traduite à côté de lui
  // sous « <nom en minuscules>.dll.rcnx », là où le runtime la cherche.
  const dllFile = sample.dll ? path.join(work, `${sample.dll.name}.dll`) : null;
  const dllLink = [];
  if (sample.dll) {
    const implib = path.join(work, `lib${sample.dll.name}.a`);
    run(cc, ['-O2', '-shared', ...sample.dll.flags, '-o', dllFile, path.join(dir, `${sample.dll.name}.c`), `-Wl,--out-implib,${implib}`, '-lkernel32'], { stdio: 'inherit' });
    dllLink.push(implib);
    run(recompiler, [dllFile, '-o', path.join(work, `${sample.dll.name.toLowerCase()}.dll.rcnx`)], { stdio: ['ignore', 'ignore', 'inherit'] });
  }
  run(cc, ['-O2', ...sample.flags, '-o', exe, path.join(dir, `${sample.name}.${sample.cpp ? 'cpp' : 'c'}`), ...dllLink, ...sample.libs, ...builtins], { stdio: 'inherit' });

  if (update) {
    if (process.platform !== 'win32') fail("--update exécute les exe d'origine : Windows seulement");
    if (sample.noOriginal) fail(`${id} : pas d'exe d'origine (expected.txt est écrit à la main)`);
    const ref = capture(exe, []);
    if (ref.code !== sample.exit) fail(`${id}.exe s'est terminé avec le code ${ref.code}`);
    fs.writeFileSync(expectedFile, normalize(sample, ref.out));
  }
  const expected = fs.readFileSync(expectedFile);
  if (process.platform === 'win32' && !sample.noOriginal) check(sample, expected, `exe d'origine (${sample.bits === 32 ? 'x86 32 bits' : 'x86-64'}, Windows)`, capture(exe, []));

  const source = path.join(work, `${id}_recomp.c`);
  if (!sample.nativeOnly) recompile(recompiler, exe, source, { comments: true });
  const compile = (cc, target, out) =>
    run(cc, [...target, '-O1', '-w', ...translatedFlags, ...threadFlags(target), '-I', runtimeDir, source, ...runtimeSources, ...linkedSources, cli, '-lm', '-o', out], { stdio: 'inherit' });

  const hostBin = path.join(work, `${id}_native${process.platform === 'win32' ? '.exe' : ''}`);
  if (!sample.nativeOnly) {
    compile(hostCc, [], hostBin);
    check(sample, expected, `code traduit, recompilé pour le PC (${process.arch})`, capture(hostBin, [exe]));
  }

  // Code machine traduit directement (Cranelift), pour le PC.
  const code = path.join(work, `${id}.rcnx`);
  run(recompiler, [exe, '-o', code], { stdio: ['ignore', 'ignore', 'inherit'] });
  check(sample, expected, `code machine traduit pour le PC (${process.arch})`, capture(hostNative, [code, exe], sample.env));

  if (onDevice && !sample.pcOnly) {
    // Code machine ARM64, traduit sur le PC (le même générateur tourne dans l'application).
    const armCode = path.join(work, `${id}_arm64.rcnx`);
    run(recompiler, [exe, '-o', armCode, '--target', 'aarch64'], { stdio: ['ignore', 'ignore', 'inherit'] });
    run(adb, ['push', armCode, `${deviceDir}/${id}.rcnx`]);
    run(adb, ['push', exe, `${deviceDir}/${id}.exe`]);
    if (sample.dll) {
      const armDll = path.join(work, `${sample.dll.name.toLowerCase()}_arm64.dll.rcnx`);
      run(recompiler, [dllFile, '-o', armDll, '--target', 'aarch64'], { stdio: ['ignore', 'ignore', 'inherit'] });
      run(adb, ['push', armDll, `${deviceDir}/${sample.dll.name.toLowerCase()}.dll.rcnx`]);
      run(adb, ['push', dllFile, `${deviceDir}/${sample.dll.name}.dll`]);
    }
    const nout = run(adb, ['exec-out', `cd ${deviceDir} && ./rcn_native ${id}.rcnx ${id}.exe 2>/dev/null; echo $? > code`]);
    const ncode = Number(run(adb, ['shell', 'cat', `${deviceDir}/code`], { encoding: 'utf8' }).trim());
    check(sample, expected, `code machine traduit sur l'appareil (ARM64, ${model})`, { out: nout, code: ncode, err: '' });

    if (sample.nativeOnly) continue;
    const bin = path.join(work, `${id}_arm64`);
    compile(ndkClang, [androidTarget], bin);
    run(adb, ['push', bin, `${deviceDir}/${id}`]);
    run(adb, ['push', exe, `${deviceDir}/${id}.exe`]);
    run(adb, ['shell', 'chmod', '755', `${deviceDir}/${id}`]);
    // exec-out : sortie binaire intacte ; le code de sortie est écrit à part, la sortie d'erreur ignorée.
    const out = run(adb, ['exec-out', `cd ${deviceDir} && ./${id} ${id}.exe 2>/dev/null; echo $? > code`]);
    const code = Number(run(adb, ['shell', 'cat', `${deviceDir}/code`], { encoding: 'utf8' }).trim());
    check(sample, expected, `code traduit sur l'appareil (ARM64, ${model})`, { out, code, err: '' });
  }
}

if (onDevice) run(adb, ['shell', 'rm', '-r', deviceDir]);
process.exit(failures ? 1 : 0);
