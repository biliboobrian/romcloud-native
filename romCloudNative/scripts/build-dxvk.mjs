#!/usr/bin/env node
// Construction de DXVK (Direct3D 11 / DXGI par Vulkan) pour Android ARM64, avec le NDK :
//   - sources de DXVK (version DXVK_TAG) récupérées dans build/third_party/dxvk si absentes ;
//   - adaptées pour Android (third_party/dxvk-android/patch.py : fenêtre d'Android, pilote Turnip) ;
//   - compilées par Meson et Ninja dans build/dxvk-android (libdxvk_dxgi.so, libdxvk_d3d11.so).
//
//   node scripts/build-dxvk.mjs [--clean]
//
// Outils : Meson (pip install meson), Ninja (romcloud-build-tools), NDK (clang, glslc).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fail, findNdkClang, root } from './lib.mjs';

const DXVK_TAG = 'v2.7.1';
const src = path.join(root, 'build', 'third_party', 'dxvk');
const out = path.join(root, 'build', 'dxvk-android');
const port = path.join(root, 'third_party', 'dxvk-android');
const tools = path.join(root, 'build', 'dxvk-tools');

const sh = (cmd, args, opts = {}) => execFileSync(cmd, args, { stdio: 'inherit', ...opts });
const clang = findNdkClang();
if (!clang) fail('NDK introuvable');
const ndkBin = path.dirname(clang);
const ndk = path.resolve(ndkBin, '..', '..', '..', '..', '..');
const exe = process.platform === 'win32' ? '.exe' : '';
const glslc = path.join(ndk, 'shader-tools', process.platform === 'win32' ? 'windows-x86_64' : `${process.platform}-x86_64`, `glslc${exe}`);
if (!fs.existsSync(glslc)) fail(`glslc introuvable : ${glslc}`);

// Sources.
if (!fs.existsSync(path.join(src, 'meson.build'))) {
  fs.mkdirSync(path.dirname(src), { recursive: true });
  sh('git', ['clone', '--depth', '1', '--branch', DXVK_TAG, 'https://github.com/doitsujin/dxvk.git', src]);
  sh('git', ['-C', src, 'submodule', 'update', '--init', '--depth', '1']);
}
sh('python', ['-I', path.join(port, 'patch.py'), src]);

// Outils : glslangValidator remplacé par glslc (en-têtes C des nuanceurs internes de DXVK).
fs.mkdirSync(tools, { recursive: true });
fs.copyFileSync(path.join(port, 'glslangValidator.py'), path.join(tools, 'glslangValidator.py'));
if (process.platform === 'win32') {
  fs.writeFileSync(path.join(tools, 'glslangValidator.bat'), `@python -I "%~dp0glslangValidator.py" %*\r\n`);
} else {
  fs.copyFileSync(path.join(port, 'glslangValidator.py'), path.join(tools, 'glslangValidator'));
  fs.chmodSync(path.join(tools, 'glslangValidator'), 0o755);
}

// Fichier de compilation croisée de Meson.
const slash = (p) => p.split(path.sep).join('/');
const cross = path.join(tools, 'android-aarch64.ini');
fs.writeFileSync(cross, `[binaries]
c = ['${slash(path.join(ndkBin, `clang${exe}`))}', '--target=aarch64-linux-android26']
cpp = ['${slash(path.join(ndkBin, `clang++${exe}`))}', '--target=aarch64-linux-android26']
ar = '${slash(path.join(ndkBin, `llvm-ar${exe}`))}'
strip = '${slash(path.join(ndkBin, `llvm-strip${exe}`))}'

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
`);

const buildTools = path.join(os.homedir(), 'AppData', 'Local', 'romcloud-build-tools');
const env = {
  ...process.env,
  GLSLC: glslc,
  PATH: [tools, path.join(buildTools, 'ninja'), process.env.PATH].join(path.delimiter),
};
if (process.argv.includes('--clean')) fs.rmSync(out, { recursive: true, force: true });
const meson = ['-m', 'mesonbuild.mesonmain'];
if (!fs.existsSync(path.join(out, 'build.ninja'))) {
  sh('python', [...meson, 'setup', out, src, '--cross-file', cross, '--buildtype', 'release', '--strip',
    '-Denable_d3d8=false', '-Denable_d3d9=false', '-Denable_d3d10=false',
    '-Dnative_sdl2=disabled', '-Dnative_sdl3=disabled', '-Dnative_glfw=disabled'], { env });
}
sh('python', [...meson, 'compile', '-C', out], { env });
console.log(`DXVK pour Android : ${out}`);
