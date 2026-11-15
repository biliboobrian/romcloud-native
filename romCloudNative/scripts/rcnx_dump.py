"""Code machine d'une fonction traduite (.rcnx) en regard de son x86 d'origine, pour étudier ce que produit
le recompilateur. Les décalages sont ceux de simpleperf (« game.rcnx[+423c] » : depuis le début du code).

    python rcnx_dump.py <jeu.rcnx> <jeu.exe> <décalage hex>… [--x86]
"""
import os
import struct
import subprocess
import sys
import tempfile

CODE_ALIGN = 64 << 10


def read_rcnx(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'RCNX'
    (version, arch, bits, base, size, stamp, entry, nfunc, nhelp, nmsg, code_size, nreloc, ngot) = struct.unpack_from('<IIIQIIIIIIIII', data, 4)
    pos = 4 + struct.calcsize('<IIIQIIIIIIIII')

    def text(pos):
        n = struct.unpack_from('<I', data, pos)[0]
        return data[pos + 4:pos + 4 + n].decode('utf-8', 'replace'), pos + 4 + n
    _, pos = text(pos)
    helpers = []
    for _ in range(nhelp):
        h, pos = text(pos)
        helpers.append(h)
    for _ in range(nmsg):
        _, pos = text(pos)
    funcs = []
    for _ in range(nfunc):
        rva, off = struct.unpack_from('<II', data, pos)
        funcs.append((off, rva))
        pos += 8
    pos += nreloc * 20
    code_start = (pos + CODE_ALIGN - 1) // CODE_ALIGN * CODE_ALIGN
    funcs.sort()
    return data, code_start, code_size, funcs, base, helpers


def objdump():
    ndk = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'ndk')
    ver = sorted(os.listdir(ndk))[-1]
    return os.path.join(ndk, ver, 'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin', 'llvm-objdump.exe')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show_x86 = '--x86' in sys.argv
    rcnx, exe = args[0], args[1]
    data, code_start, code_size, funcs, base, helpers = read_rcnx(rcnx)
    import bisect
    starts = [f[0] for f in funcs]
    # « r1449 » : fonction de RVA 0x1449 (son début) ; sinon décalage dans le code.
    by_rva = {rva: off for off, rva in funcs}
    offsets = [by_rva[int(a[1:], 16)] if a.startswith('r') else int(a, 16) for a in args[2:]]
    for off in offsets:
        i = bisect.bisect_right(starts, off) - 1
        fstart, rva = funcs[i]
        fend = funcs[i + 1][0] if i + 1 < len(funcs) else code_size
        print(f'=== +{off:x} : fonction RVA 0x{rva:x}, code +{fstart:x}..+{fend:x} ({fend - fstart} octets)')
        with tempfile.NamedTemporaryFile(delete=False, suffix='.bin') as t:
            t.write(data[code_start + fstart:code_start + fend])
        # llvm-objdump ne lit pas le binaire brut : mots rendus en « .inst » (vus comme du code) dans un objet
        # ELF assemblé par clang.
        asm = t.name + '.s'
        obj = t.name + '.o'
        code = data[code_start + fstart:code_start + fend]
        words = struct.unpack_from('<%dI' % (len(code) // 4), code)
        open(asm, 'w').write('.text\n' + ''.join('.inst 0x%08x\n' % w for w in words))
        subprocess.run([os.path.join(os.path.dirname(objdump()), 'clang.exe'), '--target=aarch64-linux-android26', '-c', asm, '-o', obj],
                       check=True)
        out = subprocess.run([objdump(), '-d', obj], capture_output=True, text=True).stdout
        os.unlink(asm)
        os.unlink(obj)
        lines = [l for l in out.splitlines() if l[:1] == ' ' and ':' in l and 'file format' not in l]
        for l in lines:
            a = int(l.split(':')[0].strip(), 16)
            mark = '>>' if fstart + a <= off < fstart + a + 4 else '  '
            print(mark, f'{fstart + a:8x}', l.split(':', 1)[1].strip()[:90])
        os.unlink(t.name)
        if show_x86:
            x = subprocess.run([objdump(), '-d', '--no-show-raw-insn', '-M', 'intel', f'--start-address=0x{base + rva:x}',
                                f'--stop-address=0x{base + rva + 0x200:x}', exe], capture_output=True, text=True).stdout
            print('--- x86 ---')
            print('\n'.join(x.splitlines()[6:80]))


main()
