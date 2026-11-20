"""Génère runtime/rcn_dxvk_d3d9_gen.h : le pont entre un jeu x86 32 bits traduit (appels COM de Direct3D 9,
convention stdcall : « this » puis les arguments sur la pile, lus par rcn_arg) et le Direct3D 9 de l'hôte
(DXVK sur Android, celui de Windows sur PC), objets C natifs 64 bits.

Chaque objet de l'hôte est montré au jeu par un enveloppeur rangé sous 2 Go { table virtuelle de 32 bits,
objet natif }. Pour chaque méthode, une fonction lit les arguments, convertit les interfaces (enveloppeur →
objet natif à l'entrée, objet natif → enveloppeur de 4 octets à la sortie) et appelle la méthode de l'hôte
par sa table virtuelle C. Les structures qui contiennent un pointeur ou un handle (D3DPRESENT_PARAMETERS,
D3DLOCKED_RECT…) n'ont pas la même disposition des deux côtés : leurs méthodes sont écrites à la main
(rcn_dxvk_d3d9.c), comme celles d'IUnknown.

    python gen_dxvk_d3d9.py <dossier include/native/directx de DXVK>
"""
import os
import re
import sys

INC = sys.argv[1]
text = open(os.path.join(INC, 'd3d9.h'), encoding='utf-8', errors='replace').read()

# Familles d'enveloppeurs (ordre : essai de QueryInterface pour un objet de type inconnu, les textures avant
# leurs bases).
FAMILIES = ['IDirect3D9', 'IDirect3DDevice9', 'IDirect3DSwapChain9', 'IDirect3DTexture9', 'IDirect3DVolumeTexture9',
            'IDirect3DCubeTexture9', 'IDirect3DVertexBuffer9', 'IDirect3DIndexBuffer9', 'IDirect3DSurface9',
            'IDirect3DVolume9', 'IDirect3DVertexDeclaration9', 'IDirect3DVertexShader9', 'IDirect3DPixelShader9',
            'IDirect3DStateBlock9', 'IDirect3DQuery9']
# Méthodes écrites à la main (Nom ou Nom@Interface qui la déclare).
SPECIAL = {'QueryInterface', 'AddRef', 'Release',
           'CreateDevice@IDirect3D9', 'Reset@IDirect3DDevice9', 'CreateAdditionalSwapChain@IDirect3DDevice9',
           'GetPresentParameters@IDirect3DSwapChain9', 'GetCreationParameters@IDirect3DDevice9',
           'Present@IDirect3DDevice9', 'Present@IDirect3DSwapChain9',
           'LockRect@IDirect3DTexture9', 'UnlockRect@IDirect3DTexture9', 'LockRect@IDirect3DCubeTexture9',
           'UnlockRect@IDirect3DCubeTexture9', 'LockBox@IDirect3DVolumeTexture9', 'UnlockBox@IDirect3DVolumeTexture9',
           'LockRect@IDirect3DSurface9', 'UnlockRect@IDirect3DSurface9', 'LockBox@IDirect3DVolume9',
           'UnlockBox@IDirect3DVolume9', 'Lock@IDirect3DVertexBuffer9', 'Unlock@IDirect3DVertexBuffer9',
           'Lock@IDirect3DIndexBuffer9', 'Unlock@IDirect3DIndexBuffer9', 'GetAdapterMonitor@IDirect3D9'}
# Méthodes refusées (HDC de Windows : pas de GDI derrière les surfaces).
UNSUPPORTED = {'GetDC@IDirect3DSurface9', 'ReleaseDC@IDirect3DSurface9'}

interfaces = {}
parents = {}
for m in re.finditer(r'DECLARE_INTERFACE_\((\w+),\s*(\w+)\)\s*\{(.*?)\n\};', text, re.S):
    name, parent, body = m.group(1), m.group(2), m.group(3)
    if name in interfaces:
        continue
    parents[name] = parent
    meths = []
    for mm in re.finditer(r'STDMETHOD(?:_\(\s*([\w\s\*]+?)\s*,\s*(\w+)\s*\)|\((\w+)\))\s*\(THIS_?\s*(.*?)\)\s*PURE;', body, re.S):
        ret = (mm.group(1) or 'HRESULT').strip()
        mname = mm.group(2) or mm.group(3)
        params = mm.group(4).strip()
        plist = [p.strip() for p in params.replace('\n', ' ').split(',')] if params else []
        meths.append((ret, mname, plist))
    interfaces[name] = meths
for f in FAMILIES:
    if f not in interfaces:
        sys.exit('interface absente de d3d9.h : ' + f)


def chain(name):
    out = []
    while name and name != 'IUnknown':
        out.append(name)
        name = parents.get(name)
    return out


def owner(iface, method):
    """Interface la plus ancienne de la chaîne qui déclare la méthode."""
    found = iface
    for c in chain(iface):
        if c in interfaces and any(n == method for _, n, _ in interfaces[c]):
            found = c
    return found


def parse_param(p):
    p = re.sub(r'\bstruct\s+', '', p)
    p = re.sub(r'\bCONST\b', 'const', p)
    p = re.sub(r'\s*\*\s*', ' * ', p)
    p = re.sub(r'\s+', ' ', p).strip().replace('* *', '**')
    if p.endswith('*') or ' ' not in p:  # paramètre sans nom (« void* », « D3DPRIMITIVETYPE »)
        return p, 'unnamed'
    m = re.match(r'(.*?)(\w+)(\[\d*\])?$', p)
    typ, name, arr = m.group(1).strip(), m.group(2), m.group(3)
    if arr:
        typ += ' *'
    typ = re.sub(r'\s+', ' ', typ).replace('* *', '**')
    return typ, name


KNOWN = set(interfaces) | {'IUnknown'}
family_index = {f: i for i, f in enumerate(FAMILIES)}


def iface_in(typ):
    m = re.match(r'^(?:const )?(\w+) \*$', typ)
    return m.group(1) if m and m.group(1) in KNOWN else None


def iface_out(typ):
    m = re.match(r'^(\w+) \*\*$', typ)
    return m.group(1) if m and m.group(1) in KNOWN else None


def fam_expr(iface):
    return str(family_index[iface]) if iface in family_index else 'D9_FAM_DYNAMIC'


out = ['// Généré par scripts/gen_dxvk_d3d9.py depuis d3d9.h (en-têtes de DXVK) : ne pas modifier.',
       '// Inclus par rcn_dxvk_d3d9.c (qui fournit d9_unwrap, d9_out, d9_out_iid, d9_hwnd, d9_float_arg…).', '']
out.append('enum { D9_FAMILY_COUNT = %d };' % len(FAMILIES))
out.append('static const IID *const d9_family_iid[D9_FAMILY_COUNT] = {%s};' % ', '.join('&IID_' + f for f in FAMILIES))
out.append('static const char *const d9_family_name[D9_FAMILY_COUNT] = {%s};' % ', '.join('"%s"' % f for f in FAMILIES))
out.append('')

tables = []
done = set()
count = 0
for fam in FAMILIES:
    entries = []
    for ret, name, plist in interfaces[fam]:
        own = owner(fam, name)
        key = name if name in ('QueryInterface', 'AddRef', 'Release') else '%s@%s' % (name, own)
        params = [parse_param(p) for p in plist]
        pop = 4 * (1 + len(params))
        if key in SPECIAL:
            fn = 'd9_special_%s' % (name if name in ('QueryInterface', 'AddRef', 'Release') else '%s_%s' % (own, name))
            entries.append((name, fn, pop))
            continue
        fn = 'd9_%s_%s' % (own, name)
        entries.append((name, fn, pop))
        if fn in done:
            continue
        done.add(fn)
        if key in UNSUPPORTED:
            out += ['static void %s(RcnCtx *ctx) { d9_unsupported(ctx, "%s::%s"); }' % (fn, own, name), '']
            continue
        body = ['static void %s(RcnCtx *ctx) {' % fn,
                '    %s *self = (%s *)d9_unwrap(rcn_arg(ctx, 0));' % (own, own),
                '    if (!self) { RET((u32)E_POINTER); return; }']
        args = ['self']
        post = []
        riid = None
        for k, (typ, pname) in enumerate(params):
            idx = k + 1
            arg = 'rcn_arg(ctx, %d)' % idx
            if typ in ('REFIID', 'REFGUID'):
                if typ == 'REFIID':
                    riid = idx
                body.append('    %s a%d = (%s)(uintptr_t)%s;' % (typ, idx, typ, arg))
                args.append('a%d' % idx)
                continue
            if typ in ('float', 'FLOAT'):
                body.append('    float a%d = d9_float_arg(ctx, %d);' % (idx, idx))
                args.append('a%d' % idx)
                continue
            if typ == 'HWND':
                body.append('    HWND a%d = d9_hwnd(%s);' % (idx, arg))
                args.append('a%d' % idx)
                continue
            if typ == 'HANDLE *':
                # Ressources partagées entre processus : sans objet ici.
                args.append('NULL')
                continue
            i_in = iface_in(typ)
            if i_in:
                body.append('    %s a%d = (%s)d9_unwrap(%s);' % (typ, idx, typ, arg))
                args.append('a%d' % idx)
                continue
            i_out = iface_out(typ)
            if i_out:
                body.append('    u64 g%d = %s; %s *t%d = NULL;' % (idx, arg, i_out, idx))
                args.append('g%d ? &t%d : NULL' % (idx, idx))
                post.append('    if (g%d) d9_out(ctx, g%d, t%d, %s);' % (idx, idx, idx, fam_expr(i_out)))
                continue
            if typ == 'void **' and riid:
                body.append('    u64 g%d = %s; void *t%d = NULL;' % (idx, arg, idx))
                args.append('g%d ? &t%d : NULL' % (idx, idx))
                post.append('    if (g%d) d9_out_iid(ctx, g%d, t%d, a%d, &r);' % (idx, idx, idx, riid))
                continue
            if '**' in typ or typ in ('HDC', 'HDC *', 'HMONITOR'):
                sys.exit('type non géré : %s::%s %s %s' % (own, name, typ, pname))
            if '*' in typ:
                body.append('    %s a%d = (%s)(uintptr_t)%s;' % (typ, idx, typ, arg))
            else:
                body.append('    %s a%d = (%s)(u32)%s;' % (typ, idx, typ, arg))
            args.append('a%d' % idx)
        call = 'self->lpVtbl->%s(%s)' % (name, ', '.join(args))
        if ret == 'void':
            body.append('    %s;' % call)
            body.extend(p.replace(', &r)', ', NULL)') for p in post)
        elif ret in ('float', 'FLOAT'):
            body.append('    float r = %s;' % call)
            body.append('    rcn_fpush(ctx, r);')
        else:
            body.append('    %s r = %s;' % (ret, call))
            body.extend(post)
            if ret == 'HRESULT':
                body.append('    if (FAILED(r)) d9_failed("%s::%s", r);' % (own, name))
            body.append('    RET((u32)r);')
        body.append('}')
        body.append('')
        out.extend(body)
        count += 1
    tables.append((fam, entries))

specials = sorted({fn for _, entries in tables for _, fn, _ in entries if fn.startswith('d9_special_')})
out.extend('static void %s(RcnCtx *ctx);' % fn for fn in specials)
out.append('')
out.append('typedef struct { const char *name; RcnFn fn; u32 pop; } D9Method;')
for fam, entries in tables:
    out.append('static const D9Method d9_methods_%s[] = {' % fam)
    for name, fn, pop in entries:
        out.append('    {"%s", %s, %d},' % (name, fn, pop))
    out.append('};')
out.append('static const D9Method *const d9_family_methods[D9_FAMILY_COUNT] = {%s};' % ', '.join('d9_methods_' + f for f, _ in tables))
out.append('static const u32 d9_family_method_count[D9_FAMILY_COUNT] = {%s};' % ', '.join(
    'sizeof d9_methods_%s / sizeof d9_methods_%s[0]' % (f, f) for f, _ in tables))
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'runtime', 'rcn_dxvk_d3d9_gen.h')
open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('pont Direct3D 9 : %d familles, %d méthodes générées' % (len(FAMILIES), count))
