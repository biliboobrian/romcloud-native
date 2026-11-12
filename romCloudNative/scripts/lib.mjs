// Outils communs aux scripts de RomCloud Native : recherche des compilateurs, recompilation d'un
// exécutable, écriture d'une archive zip.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import zlib from 'node:zlib';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const runtimeDir = path.join(root, 'runtime');
// Tout le runtime, sauf le lanceur en ligne de commande (rcn_cli.c), réservé aux tests, et le point
// d'entrée des bibliothèques de jeu en C (rcn_linked.c, voir linkedSources).
export const runtimeSources = fs.readdirSync(runtimeDir)
  .filter((f) => f.endsWith('.c') && f !== 'rcn_cli.c' && f !== 'rcn_linked.c')
  .sort()
  .map((f) => path.join(runtimeDir, f));
// À ajouter au runtime pour du code traduit en C (bibliothèque qui contient le jeu).
export const linkedSources = [path.join(runtimeDir, 'rcn_linked.c')];
// Options sans lesquelles le code traduit ne se comporte pas comme l'original : accès mémoire par
// types différents, et pas de multiplication-addition fusionnée (résultats flottants du x86).
export const translatedFlags = ['-fno-strict-aliasing', '-ffp-contract=off'];

const exe = process.platform === 'win32' ? '.exe' : '';

// RCN_MINGW : dossier de llvm-mingw, s'il n'est pas dans le PATH (éditeur de liens de cargo sous
// Windows, compilateur des programmes de test).
if (process.env.RCN_MINGW) {
  const dir = process.env.RCN_MINGW;
  const bin = fs.existsSync(path.join(dir, 'bin')) ? path.join(dir, 'bin') : dir;
  process.env.PATH = bin + path.delimiter + (process.env.PATH ?? '');
}

export function fail(message) {
  console.error(`erreur : ${message}`);
  process.exit(1);
}

export function run(file, args, options = {}) {
  try {
    return execFileSync(file, args, { stdio: ['ignore', 'pipe', 'inherit'], maxBuffer: 1 << 28, ...options });
  } catch (e) {
    if (e.code === 'ENOENT') fail(`programme introuvable : ${file}`);
    fail(`échec de ${path.basename(file)} ${args.slice(0, 3).join(' ')}${args.length > 3 ? ' …' : ''}`);
  }
}

/** Programme du PATH ou d'un des dossiers indiqués, ou null. */
function findProgram(name, dirs = []) {
  const all = [...dirs, ...(process.env.PATH ?? '').split(path.delimiter)];
  for (const dir of all) {
    const file = path.join(dir, name + exe);
    if (dir && fs.existsSync(file)) return file;
  }
  return null;
}

/** Compile le recompilateur (cargo) et renvoie son chemin. */
export function buildRecompiler() {
  const cargo = findProgram('cargo', [path.join(os.homedir(), '.cargo', 'bin')]);
  if (!cargo) fail('cargo introuvable : installez Rust (https://rustup.rs)');
  const dir = path.join(root, 'recompiler');
  run(cargo, ['build', '--release', '--quiet'], { cwd: dir, stdio: 'inherit' });
  // Dossier de sortie : target/release, ou target/<cible>/release si une cible est imposée.
  const candidates = [path.join(dir, 'target', 'release', 'rcn-recomp' + exe)];
  const target = path.join(dir, 'target');
  for (const entry of fs.readdirSync(target)) {
    candidates.push(path.join(target, entry, 'release', 'rcn-recomp' + exe));
  }
  const found = candidates.filter((f) => fs.existsSync(f)).sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
  if (!found) fail('rcn-recomp introuvable après la compilation');
  return found;
}

/** Traduit un exécutable en C (le recompilateur affiche son rapport). */
export function recompile(recompiler, exePath, outC, { comments = false } = {}) {
  const args = [exePath, '-o', outC];
  if (!comments) args.push('--no-comments');
  run(recompiler, args, { stdio: 'inherit' });
}

/** Compilateur C produisant des exécutables Windows x86-64 (llvm-mingw), ou null. */
export function findMingw(bits = 64) {
  const arch = bits === 32 ? 'i686' : 'x86_64';
  return findProgram(`${arch}-w64-mingw32-clang`) ?? findProgram(`${arch}-w64-mingw32-gcc`);
}

/** Compilateur C de la machine (pour exécuter le code traduit sur le PC). */
export function findHostCompiler() {
  if (process.platform === 'win32') return findMingw();
  return findProgram('clang') ?? findProgram('cc') ?? findProgram('gcc');
}

function sdkDir() {
  for (const v of ['ANDROID_HOME', 'ANDROID_SDK_ROOT']) {
    if (process.env[v] && fs.existsSync(process.env[v])) return process.env[v];
  }
  // android/local.properties : « sdk.dir=C\:/Users/… »
  const props = path.join(root, '..', 'local.properties');
  if (fs.existsSync(props)) {
    const m = /^sdk\.dir=(.*)$/m.exec(fs.readFileSync(props, 'utf8'));
    if (m) return m[1].trim().replace(/\\([:\\])/g, '$1');
  }
  return null;
}

/** clang du NDK Android (le plus récent installé). */
export function findNdkClang() {
  const sdk = sdkDir();
  const ndks = [];
  if (process.env.ANDROID_NDK_HOME) ndks.push(process.env.ANDROID_NDK_HOME);
  if (sdk && fs.existsSync(path.join(sdk, 'ndk'))) {
    const versions = fs.readdirSync(path.join(sdk, 'ndk')).sort((a, b) => b.localeCompare(a, undefined, { numeric: true }));
    ndks.push(...versions.map((v) => path.join(sdk, 'ndk', v)));
  }
  const host = { win32: 'windows-x86_64', linux: 'linux-x86_64', darwin: 'darwin-x86_64' }[process.platform];
  for (const ndk of ndks) {
    const clang = path.join(ndk, 'toolchains', 'llvm', 'prebuilt', host, 'bin', 'clang' + exe);
    if (fs.existsSync(clang)) return clang;
  }
  return null;
}

export function findAdb() {
  const sdk = sdkDir();
  return findProgram('adb', sdk ? [path.join(sdk, 'platform-tools')] : []);
}

export const androidTarget = '--target=aarch64-linux-android26';

/** Archive zip (fichiers compressés par deflate) : entries = [{ name, data }]. */
export function writeZip(file, entries) {
  const parts = [];
  const central = [];
  let offset = 0;
  for (const { name, data } of entries) {
    const nameBytes = Buffer.from(name, 'utf8');
    const packed = zlib.deflateRawSync(data, { level: 9 });
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50, 0);
    header.writeUInt16LE(20, 4); // version requise
    header.writeUInt16LE(0x0800, 6); // noms en UTF-8
    header.writeUInt16LE(8, 8); // deflate
    header.writeUInt32LE(0x00210000, 10); // date fixe (1980-01-01) : archive reproductible
    header.writeUInt32LE(zlib.crc32(data), 14);
    header.writeUInt32LE(packed.length, 18);
    header.writeUInt32LE(data.length, 22);
    header.writeUInt16LE(nameBytes.length, 26);
    parts.push(header, nameBytes, packed);
    const entry = Buffer.alloc(46);
    entry.writeUInt32LE(0x02014b50, 0);
    entry.writeUInt16LE(20, 4);
    header.copy(entry, 6, 4, 30); // version, drapeaux, méthode, date, crc, tailles, longueur du nom
    entry.writeUInt32LE(offset, 42);
    central.push(entry, nameBytes);
    offset += header.length + nameBytes.length + packed.length;
  }
  const centralSize = central.reduce((n, b) => n + b.length, 0);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(centralSize, 12);
  end.writeUInt32LE(offset, 16);
  fs.writeFileSync(file, Buffer.concat([...parts, ...central, end]));
}
