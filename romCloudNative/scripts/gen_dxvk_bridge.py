"""Génère runtime/rcn_dxvk_bridge_gen.h : le pont entre le jeu traduit (appels COM de Direct3D 11 / DXGI
selon la convention Windows x64, lus dans le contexte du code traduit) et DXVK (objets C++ natifs ARM64).

Chaque objet de DXVK est montré au jeu par un enveloppeur { table virtuelle du jeu, objet natif }. Pour
chaque méthode des interfaces servies, une fonction lit les arguments du jeu, convertit les interfaces
(enveloppeur → objet natif à l'entrée, objet natif → enveloppeur à la sortie, tableaux compris) et appelle
la méthode de DXVK par sa table virtuelle C (en-têtes de MinGW fournis avec DXVK).

    python gen_dxvk_bridge.py <dossier include/native/directx de DXVK>
"""
import os
import re
import sys

INC = sys.argv[1]
HEADERS = ['dxgi.h', 'dxgi1_2.h', 'dxgi1_3.h', 'dxgi1_4.h', 'dxgi1_5.h', 'dxgi1_6.h',
           'd3d11.h', 'd3d11_1.h', 'd3d11_2.h', 'd3d11_3.h', 'd3d11_4.h']
# Familles d'enveloppeurs : interface la plus récente de chaque famille (sa table sert aussi les versions
# antérieures). L'ordre compte pour la résolution dynamique (QueryInterface essayé dans cet ordre).
FAMILIES = ['IDXGIFactory7', 'IDXGIAdapter4', 'IDXGIOutput6', 'IDXGISwapChain4', 'IDXGIDevice4', 'IDXGISurface2',
            'IDXGIResource1', 'IDXGIKeyedMutex',
            'ID3D11Device5', 'ID3D11DeviceContext4', 'ID3D11Buffer', 'ID3D11Texture1D', 'ID3D11Texture2D1',
            'ID3D11Texture3D1', 'ID3D11ShaderResourceView1', 'ID3D11RenderTargetView1', 'ID3D11DepthStencilView',
            'ID3D11UnorderedAccessView1', 'ID3D11VertexShader', 'ID3D11PixelShader', 'ID3D11GeometryShader',
            'ID3D11HullShader', 'ID3D11DomainShader', 'ID3D11ComputeShader', 'ID3D11InputLayout', 'ID3D11BlendState1',
            'ID3D11DepthStencilState', 'ID3D11RasterizerState2', 'ID3D11SamplerState', 'ID3D11Query1',
            'ID3D11Predicate', 'ID3D11Counter', 'ID3D11CommandList', 'ID3D11ClassLinkage', 'ID3D11ClassInstance',
            'ID3D11Fence', 'ID3D11Multithread', 'ID3DUserDefinedAnnotation']
# Méthodes écrites à la main (chaînes larges : WCHAR fait 4 octets chez DXVK pour Android, 2 sous Windows ;
# IUnknown : durée de vie des enveloppeurs).
SPECIAL = {'QueryInterface', 'AddRef', 'Release', 'GetDesc@IDXGIAdapter', 'GetDesc1@IDXGIAdapter1',
           'GetDesc2@IDXGIAdapter2', 'GetDesc3@IDXGIAdapter4', 'GetDesc@IDXGIOutput', 'GetDesc1@IDXGIOutput6',
           'BeginEvent@ID3DUserDefinedAnnotation', 'SetMarker@ID3DUserDefinedAnnotation',
           'GetFrameStatistics@IDXGISwapChain', 'GetFrameStatistics@IDXGIOutput', 'GetData@ID3D11DeviceContext'}

text = ''.join(open(os.path.join(INC, h), encoding='utf-8', errors='replace').read() for h in HEADERS)
vtbls = {m.group(1): m.group(2) for m in re.finditer(r'typedef struct (\w+)Vtbl \{(.*?)\} \1Vtbl;', text, re.S)}
parents = dict(re.findall(r'\n(\w+) : public (\w+)\s*\n\{', text))
for f in FAMILIES:
    if f not in vtbls:
        sys.exit('interface absente des en-têtes : ' + f)


def chain(name):
    out = []
    while name and name != 'IUnknown':
        out.append(name)
        name = parents.get(name)
    return out


family_of = {}
for i, f in enumerate(FAMILIES):
    for c in chain(f):
        family_of.setdefault(c, i)
INTERFACES = set(vtbls) | {'IUnknown'}


def methods(iface):
    body = vtbls[iface]
    # Chaque entrée : type rendu (STDMETHODCALLTYPE *nom)( paramètres );
    for m in re.finditer(r'(\w[\w\s\*]*?)\s*\(STDMETHODCALLTYPE \*(\w+)\)\((.*?)\);', body, re.S):
        ret, name, params = m.group(1).strip(), m.group(2), m.group(3)
        plist = [p.strip() for p in params.replace('\n', ' ').split(',')]
        yield ret, name, plist


def owner(iface, method):
    """Interface qui déclare la méthode (la plus ancienne de la chaîne qui l'a)."""
    found = iface
    for c in chain(iface):
        if c in vtbls and any(n == method for _, n, _ in methods(c)):
            found = c
    return found


def parse_param(p):
    p = re.sub(r'\s+', ' ', p)
    m = re.match(r'(.*?)(\w+)(\[\d+\])?$', p)
    typ, name, arr = m.group(1).strip(), m.group(2), m.group(3)
    if arr:
        typ += ' *'
    return typ, name


def iface_in(typ):
    """Interface d'un paramètre « I* », « const I* » ; None sinon."""
    m = re.match(r'^(?:const )?(\w+) \*$', typ)
    return m.group(1) if m and m.group(1) in INTERFACES else None


def iface_out(typ):
    m = re.match(r'^(\w+) \*\*$', typ)
    return m.group(1) if m and m.group(1) in INTERFACES else None


def iface_in_array(typ):
    m = re.match(r'^(\w+) \*const \*$', typ)
    return m.group(1) if m and m.group(1) in INTERFACES else None


def is_count(name):
    n = name.lower()
    return n.startswith('num') or n.endswith('count')


def fam_expr(iface):
    return str(family_of[iface]) if iface in family_of else 'BR_FAM_DYNAMIC'


def is_float(typ):
    return typ in ('FLOAT', 'float')


out = ['// Généré par scripts/gen_dxvk_bridge.py depuis les en-têtes de DXVK : ne pas modifier.',
       '// Inclus par rcn_dxvk_bridge.c (qui fournit br_unwrap, br_wrap, br_out, br_family_of_iid…).', '']
out.append('enum { BR_FAMILY_COUNT = %d };' % len(FAMILIES))
out.append('static const IID *const br_family_iid[BR_FAMILY_COUNT] = {%s};' % ', '.join('&IID_' + f for f in FAMILIES))
out.append('static const char *const br_family_name[BR_FAMILY_COUNT] = {%s};' % ', '.join('"%s"' % f for f in FAMILIES))
# IID de chaque interface connue → famille (QueryInterface du jeu).
pairs = sorted((c, i) for c, i in family_of.items())
out.append('static const struct { const IID *iid; int family; } br_iid_family[] = {')
for c, i in pairs:
    out.append('    {&IID_%s, %d},' % (c, i))
out.append('};')
out.append('')

tables = []
generated = 0
generated_fns = set()
call_names = []
for fi, fam in enumerate(FAMILIES):
    entries = []
    for ret, name, plist in methods(fam):
        own = owner(fam, name)
        key_special = name if name in ('QueryInterface', 'AddRef', 'Release') else '%s@%s' % (name, own)
        if key_special in SPECIAL:
            entries.append((name, 'br_special_%s' % (name if name in ('QueryInterface', 'AddRef', 'Release') else '%s_%s' % (own, name))))
            continue
        fn = 'br_%s_%s' % (own, name)
        entries.append((name, fn))
        if fn in generated_fns:
            continue
        generated_fns.add(fn)
        params = [parse_param(p) for p in plist[1:]] if len(plist) > 1 and plist[1] else []
        body = ['static void %s(RcnCtx *ctx) {' % fn, '    br_calls[%d]++;' % generated]
        call_names.append('%s::%s' % (own, name))
        body.append('    %s *self = (%s *)br_unwrap(rcn_arg(ctx, 0));' % (own, own))
        body.append('    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }')
        call_args = ['self']
        post = []
        wide = any('WCHAR' in t or 'LPCWSTR' in t or 'LPWSTR' in t for t, _ in params)
        bad_struct = any(t in ('DXGI_ADAPTER_DESC *', 'DXGI_OUTPUT_DESC *') for t, _ in params)
        if wide or bad_struct:
            body = ['static void %s(RcnCtx *ctx) {' % fn,
                    '    br_unsupported(ctx, "%s::%s");' % (own, name), '}', '']
            out.extend(body)
            call_names.pop()
            continue
        riid_index = None
        for k, (typ, pname) in enumerate(params):
            idx = k + 1
            arg = 'rcn_arg(ctx, %d)' % idx
            if typ == 'REFIID':
                riid_index = k
            if is_float(typ):
                body.append('    FLOAT a%d = br_float_arg(ctx, %d);' % (idx, idx))
                call_args.append('a%d' % idx)
                continue
            i_in = iface_in(typ)
            if i_in:
                body.append('    %s a%d = (%s)br_unwrap(%s);' % (typ, idx, typ, arg))
                call_args.append('a%d' % idx)
                continue
            i_arr = iface_in_array(typ)
            if i_arr:
                count = next((params[j][1] for j in list(range(k - 1, -1, -1)) + list(range(k + 1, len(params))) if is_count(params[j][1])), None)
                if not count:
                    sys.exit('tableau sans compte : %s::%s %s' % (own, name, pname))
                ci = [n for _, n in params].index(count) + 1
                body.append('    u64 g%d = %s; u32 n%d = (u32)rcn_arg(ctx, %d); if (n%d > BR_MAX_ARRAY) n%d = 0;' % (idx, arg, idx, ci, idx, idx))
                body.append('    %s *t%d[BR_MAX_ARRAY];' % (i_arr, idx))
                body.append('    for (u32 i = 0; i < n%d; i++) t%d[i] = g%d ? (%s *)br_unwrap(rd64(g%d + 8ull * i)) : NULL;' % (idx, idx, idx, i_arr, idx))
                call_args.append('g%d ? t%d : NULL' % (idx, idx))
                continue
            i_out = iface_out(typ)
            if i_out:
                plural = pname.endswith('s') and any(is_count(n) for _, n in params[:k])
                fe = fam_expr(i_out)
                if plural:
                    count = next(params[j][1] for j in range(k - 1, -1, -1) if is_count(params[j][1]))
                    ci = [n for _, n in params].index(count) + 1
                    body.append('    u64 g%d = %s; u32 n%d = (u32)rcn_arg(ctx, %d); if (n%d > BR_MAX_ARRAY) n%d = 0;' % (idx, arg, idx, ci, idx, idx))
                    body.append('    %s *t%d[BR_MAX_ARRAY] = {0};' % (i_out, idx))
                    call_args.append('g%d ? t%d : NULL' % (idx, idx))
                    post.append('    for (u32 i = 0; g%d && i < n%d; i++) br_out(ctx, g%d + 8ull * i, t%d[i], %s);' % (idx, idx, idx, idx, fe))
                else:
                    body.append('    u64 g%d = %s; %s *t%d = NULL;' % (idx, arg, i_out, idx))
                    call_args.append('g%d ? &t%d : NULL' % (idx, idx))
                    post.append('    if (g%d) br_out(ctx, g%d, t%d, %s);' % (idx, idx, idx, fe))
                continue
            if typ == 'void **' and riid_index is not None:
                ri = riid_index + 1
                body.append('    u64 g%d = %s; void *t%d = NULL;' % (idx, arg, idx))
                call_args.append('g%d ? &t%d : NULL' % (idx, idx))
                post.append('    if (g%d) br_out_iid(ctx, g%d, t%d, (const IID *)(uintptr_t)rcn_arg(ctx, %d), &r);' % (idx, idx, idx, ri))
                continue
            if typ == 'LUID':
                # Structure de 8 octets : passée dans un registre par la convention Windows x64.
                body.append('    LUID a%d; u64 v%d = %s; memcpy(&a%d, &v%d, 8);' % (idx, idx, arg, idx, idx))
                call_args.append('a%d' % idx)
                continue
            # Valeur ou pointeur ordinaire : même espace d'adresses que le jeu.
            if '*' in typ or typ in ('REFIID', 'REFGUID', 'HWND', 'HANDLE', 'HMODULE', 'HMONITOR', 'HDC', 'LPVOID'):
                body.append('    %s a%d = (%s)(uintptr_t)%s;' % (typ, idx, typ, arg))
            else:
                body.append('    %s a%d = (%s)%s;' % (typ, idx, typ, arg))
            call_args.append('a%d' % idx)
        call = 'self->lpVtbl->%s(%s)' % (name, ', '.join(call_args))
        if name.startswith('CreateSwapChain') or name.startswith('ResizeBuffers'):
            body.append('    br_before_present(ctx); // fenêtre d\'Android donnée à DXVK avant sa surface')
        if name in ('Present', 'Present1') and own.startswith('IDXGISwapChain'):
            # Fenêtre d'Android à jour avant l'image, rythme et entrées du runtime après.
            body.append('    br_before_present(ctx);')
            post.append('    br_after_present(ctx, (IDXGISwapChain *)self, a1);')
        if ret == 'void':
            body.append('    %s;' % call)
            body.extend(p.replace(', &r)', ', NULL)') for p in post)
        elif is_float(ret):
            body.append('    FLOAT r = %s;' % call)
            body.extend(post)
            body.append('    ctx->xmm[0].f[0] = r;')
        else:
            body.append('    %s r = %s;' % (ret, call))
            body.extend(post)
            if ret == 'HRESULT':
                body.append('    if (FAILED(r)) br_failed("%s::%s", r);' % (own, name))
                body.append('    if (r == S_FALSE) br_false[%d]++;' % generated)
            if '*' in ret or ret in ('HANDLE', 'HWND'):
                body.append('    RET((u64)(uintptr_t)(r));')
            else:
                body.append('    RET((u64)(i64)(r));')
        body.append('}')
        body.append('')
        out.extend(body)
        generated += 1
    tables.append((fam, entries))

out.append('enum { BR_CALL_COUNT = %d };' % len(call_names))
out.append('static const char *const br_call_names[BR_CALL_COUNT] = {%s};' % ', '.join('"%s"' % n for n in call_names))
specials = sorted({fn for _, entries in tables for _, fn in entries if fn.startswith('br_special_')})
out.extend('static void %s(RcnCtx *ctx);' % fn for fn in specials)
for fam, entries in tables:
    out.append('static const BrMethod br_methods_%s[] = {' % fam)
    for name, fn in entries:
        out.append('    {"%s", %s},' % (name, fn))
    out.append('};')
out.append('static const BrMethod *const br_family_methods[BR_FAMILY_COUNT] = {%s};' % ', '.join('br_methods_' + f for f, _ in tables))
out.append('static const u32 br_family_method_count[BR_FAMILY_COUNT] = {%s};' % ', '.join(
    'sizeof br_methods_%s / sizeof br_methods_%s[0]' % (f, f) for f, _ in tables))
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'runtime', 'rcn_dxvk_bridge_gen.h')
open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('pont : %d familles, %d méthodes générées' % (len(FAMILIES), generated))
