#!/usr/bin/env python3
"""Remplaçant de glslangValidator pour la construction de DXVK, au-dessus de glslc (shaderc, livré avec le
NDK) : compile un nuanceur GLSL en SPIR-V et écrit l'en-tête C qu'attend DXVK (tableau `--vn`).

    glslangValidator [--quiet] --target-env vulkan1.3 --vn NOM [--depfile F] entrée.glsl -o sortie.h
"""
import os
import subprocess
import sys
import tempfile

args = sys.argv[1:]
target, name, depfile, output, inputs, defines = 'vulkan1.3', None, None, None, [], []
i = 0
while i < len(args):
    a = args[i]
    if a == '--target-env':
        target = args[i + 1]; i += 2; continue
    if a == '--vn':
        name = args[i + 1]; i += 2; continue
    if a == '--depfile':
        depfile = args[i + 1]; i += 2; continue
    if a == '-o':
        output = args[i + 1]; i += 2; continue
    if a.startswith('-D'):
        defines.append(a); i += 1; continue
    if a in ('--quiet', '-V'):
        i += 1; continue
    inputs.append(a); i += 1

glslc = os.environ.get('GLSLC', 'glslc')
src = inputs[0]
stage = {'.vert': 'vert', '.frag': 'frag', '.comp': 'comp', '.geom': 'geom', '.tesc': 'tesc', '.tese': 'tese'}.get(os.path.splitext(src)[1])
fd, spv = tempfile.mkstemp(suffix='.spv')
os.close(fd)
try:
    cmd = [glslc, f'--target-env={target}', '-I', os.path.dirname(src) or '.', '-o', spv] + defines
    if stage:
        cmd.append(f'-fshader-stage={stage}')
    if depfile:
        cmd += ['-MD', '-MF', depfile]
    cmd.append(src)
    subprocess.run(cmd, check=True)
    data = open(spv, 'rb').read()
finally:
    os.remove(spv)
words = [int.from_bytes(data[k:k + 4], 'little') for k in range(0, len(data), 4)]
with open(output, 'w', encoding='utf-8', newline='\n') as out:
    out.write('#pragma once\n#include <stdint.h>\n')
    out.write(f'const uint32_t {name}[] = {{\n')
    for k in range(0, len(words), 8):
        out.write('  ' + ', '.join(f'0x{w:08x}' for w in words[k:k + 8]) + ',\n')
    out.write('};\n')
if depfile and os.path.exists(depfile):
    # glslc écrit la cible de la règle d'après -o (le .spv temporaire) : remise au nom de l'en-tête.
    text = open(depfile, encoding='utf-8').read()
    # Séparateur « : » suivi d'une espace (les chemins Windows contiennent « C: »).
    if ': ' in text:
        open(depfile, 'w', encoding='utf-8').write(output.replace(' ', '\\ ') + ': ' + text.split(': ', 1)[1])
