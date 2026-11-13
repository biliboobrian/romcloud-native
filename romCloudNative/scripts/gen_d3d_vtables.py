"""Génère runtime/rcn_d3d11_vt.h : l'ordre des méthodes des interfaces Direct3D 11 / DXGI, lu dans les
en-têtes de llvm-mingw (les tables virtuelles des objets fournis au jeu doivent le suivre exactement).
Usage : python gen_d3d_vtables.py <dossier include de llvm-mingw>"""
import os, re, sys

INC = sys.argv[1]
HEADERS = ['dxgi.h', 'dxgi1_2.h', 'dxgi1_3.h', 'dxgi1_4.h', 'dxgi1_5.h', 'dxgi1_6.h',
           'd3d11.h', 'd3d11_1.h', 'd3d11_2.h', 'd3d11_3.h', 'd3d11_4.h']
# Interface la plus récente de chaque famille : un objet la sert pour toutes ses versions.
IFACES = ['IDXGIFactory7', 'IDXGIAdapter4', 'IDXGIOutput6', 'IDXGISwapChain4', 'IDXGIDevice4', 'IDXGISurface2',
          'ID3D11Device5', 'ID3D11DeviceContext4', 'ID3D11Buffer', 'ID3D11Texture1D', 'ID3D11Texture2D1',
          'ID3D11Texture3D1', 'ID3D11ShaderResourceView1', 'ID3D11RenderTargetView1', 'ID3D11DepthStencilView',
          'ID3D11UnorderedAccessView1', 'ID3D11VertexShader', 'ID3D11PixelShader', 'ID3D11GeometryShader',
          'ID3D11HullShader', 'ID3D11DomainShader', 'ID3D11ComputeShader', 'ID3D11InputLayout', 'ID3D11BlendState1',
          'ID3D11DepthStencilState', 'ID3D11RasterizerState2', 'ID3D11SamplerState', 'ID3D11Query1',
          'ID3D11Multithread', 'ID3DUserDefinedAnnotation', 'ID3D11ClassLinkage']

text = ''.join(open(os.path.join(INC, h), encoding='utf-8', errors='replace').read() for h in HEADERS)
out = ['// Généré par scripts/gen_d3d_vtables.py depuis les en-têtes de llvm-mingw : ne pas modifier.',
       '// Ordre des méthodes de chaque interface (IUnknown compris).', '#ifndef RCN_D3D11_VT_H', '#define RCN_D3D11_VT_H', '']
for name in IFACES:
    m = re.search(r'typedef struct %sVtbl \{(.*?)\} %sVtbl;' % (name, name), text, re.S)
    if not m:
        sys.exit('interface absente : ' + name)
    methods = re.findall(r'\(STDMETHODCALLTYPE \*(\w+)\)', m.group(1))
    out.append('static const char *const vt_%s[] = {' % name)
    for i in range(0, len(methods), 6):
        out.append('    ' + ' '.join('"%s",' % x for x in methods[i:i + 6]))
    out.append('};')
    out.append('#define VT_%s_COUNT %d' % (name, len(methods)))
    out.append('')
# Chaîne d'héritage (« X : public Y ») et IID de chaque interface.
parents = dict(re.findall(r'\n(\w+) : public (\w+)\s*\n\{', text))
guids = re.findall(r'DEFINE_GUID\(IID_(\w+), (0x\w+), (0x\w+), (0x\w+), ((?:0x\w+,\s*){7}0x\w+)\)', text)
for name in IFACES:
    chain, x = [], name
    while x and x != 'IUnknown':
        chain.append(x)
        x = parents.get(x)
    out.append('static const char *const chain_%s[] = {%s NULL};' % (name, ' '.join('"%s",' % c for c in chain)))
out.append('')
out.append('typedef struct { u32 a; u16 b, c; u8 d[8]; const char *name; } RcnIid;')
out.append('static const RcnIid rcn_d3d_iids[] = {')
seen = set()
for g in guids:
    if g[0] in seen: continue
    seen.add(g[0])
    d = ', '.join(x.strip() for x in g[4].split(','))
    out.append('    {%s, %s, %s, {%s}, "%s"},' % (g[1], g[2], g[3], d, g[0]))
out.append('};')
out.append('')
out.append('#endif')
open(os.path.join(os.path.dirname(__file__), '..', 'runtime', 'rcn_d3d11_vt.h'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('ok', len(IFACES))
