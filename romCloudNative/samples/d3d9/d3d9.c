// Test d3d9 : exécutable x86 32 bits sans bibliothèque C qui se sert de Direct3D 9 — appareil sur une petite
// fenêtre, tampons de sommets et d'index (statique, dynamique, relecture), texture gérée remplie par
// LockRect (rectangle partiel), rendu d'un carré texturé (sommets transformés, sans shader), image relue
// par GetRenderTargetData et quelques pixels écrits. Sous Windows, le même résultat que le Direct3D 9
// servi par le runtime. Sortie par WriteFile.
#include <windows.h>
#include <d3d9.h>

static HANDLE out;
static void put(const char *s) {
    DWORD n = 0, len = 0;
    while (s[len]) len++;
    WriteFile(out, s, len, &n, NULL);
}
static void put_hex(DWORD v) {
    char buf[9];
    for (int i = 7; i >= 0; i--, v >>= 4) buf[i] = "0123456789abcdef"[v & 15];
    buf[8] = 0;
    put(buf);
}
static void put_int(int v) {
    char buf[16];
    int i = 15;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    buf[i] = 0;
    do buf[--i] = (char)('0' + u % 10);
    while (u /= 10);
    if (v < 0) buf[--i] = '-';
    put(buf + i);
}
static void check(const char *what, HRESULT r) {
    put(what);
    put(r == D3D_OK ? " ok\n" : " echec ");
    if (r != D3D_OK) {
        put_hex((DWORD)r);
        put("\n");
        ExitProcess(1);
    }
}

typedef struct {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
} Vertex;
#define FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(w, m, wp, lp); }

void start(void) {
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"rcnd3d9";
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, L"rcnd3d9", L"d3d9", WS_POPUP, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    put(d3d ? "Direct3DCreate9 ok\n" : "Direct3DCreate9 echec\n");
    if (!d3d) ExitProcess(1);
    put("adaptateurs ");
    put_int(IDirect3D9_GetAdapterCount(d3d) > 0);
    put("\n");

    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *dev = NULL;
    check("CreateDevice", IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev));
    put("fenetre rendue ");
    put_int(pp.hDeviceWindow == window);
    put("\n");
    D3DDEVICE_CREATION_PARAMETERS cp;
    check("GetCreationParameters", IDirect3DDevice9_GetCreationParameters(dev, &cp));
    put("fenetre de creation ");
    put_int(cp.hFocusWindow == window);
    put("\n");

    // Tampon de sommets statique : rempli, puis relu en lecture seule.
    Vertex quad[4] = {
        {8, 8, 0.5f, 1, 0xffffffff, 0, 0},
        {56, 8, 0.5f, 1, 0xffffffff, 1, 0},
        {8, 56, 0.5f, 1, 0xff8080ff, 0, 1},
        {56, 56, 0.5f, 1, 0xff8080ff, 1, 1},
    };
    IDirect3DVertexBuffer9 *vb = NULL;
    check("CreateVertexBuffer", IDirect3DDevice9_CreateVertexBuffer(dev, sizeof quad, D3DUSAGE_WRITEONLY, FVF, D3DPOOL_MANAGED, &vb, NULL));
    void *p = NULL;
    check("Lock", IDirect3DVertexBuffer9_Lock(vb, 0, 0, &p, 0));
    put("adresse sous 2 Go ");
    put_int((DWORD)p < 0x80000000u);
    put("\n");
    for (unsigned i = 0; i < sizeof quad; i++) ((BYTE *)p)[i] = ((const BYTE *)quad)[i];
    check("Unlock", IDirect3DVertexBuffer9_Unlock(vb));
    check("Lock lecture", IDirect3DVertexBuffer9_Lock(vb, sizeof(Vertex), sizeof(Vertex), &p, D3DLOCK_READONLY));
    put("relu ");
    put_int((int)((const Vertex *)p)->x);
    put(" ");
    put_hex(((const Vertex *)p)->color);
    put("\n");
    IDirect3DVertexBuffer9_Unlock(vb);

    // Tampon d'index dynamique : écrit en deux fois (DISCARD puis NOOVERWRITE).
    IDirect3DIndexBuffer9 *ib = NULL;
    check("CreateIndexBuffer", IDirect3DDevice9_CreateIndexBuffer(dev, 6 * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
                                                                  D3DPOOL_DEFAULT, &ib, NULL));
    WORD *idx = NULL;
    check("Lock index", IDirect3DIndexBuffer9_Lock(ib, 0, 6, (void **)&idx, D3DLOCK_DISCARD));
    idx[0] = 0, idx[1] = 1, idx[2] = 2;
    IDirect3DIndexBuffer9_Unlock(ib);
    check("Lock index suite", IDirect3DIndexBuffer9_Lock(ib, 6, 6, (void **)&idx, D3DLOCK_NOOVERWRITE));
    idx[0] = 2, idx[1] = 1, idx[2] = 3;
    IDirect3DIndexBuffer9_Unlock(ib);

    // Texture gérée 4 × 4 : fond rouge, puis un rectangle 2 × 2 en bas à droite vert (LockRect partiel).
    IDirect3DTexture9 *tex = NULL;
    check("CreateTexture", IDirect3DDevice9_CreateTexture(dev, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL));
    D3DLOCKED_RECT lr;
    check("LockRect", IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = (x + y) & 1 ? 0xffff0000 : 0xffc00000;
    IDirect3DTexture9_UnlockRect(tex, 0);
    RECT part = {2, 2, 4, 4};
    check("LockRect partiel", IDirect3DTexture9_LockRect(tex, 0, &lr, &part, 0));
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xff00ff00;
    IDirect3DTexture9_UnlockRect(tex, 0);

    // Rendu.
    check("Clear", IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff102030, 1.0f, 0));
    check("BeginScene", IDirect3DDevice9_BeginScene(dev));
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    check("SetTexture", IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tex));
    check("SetFVF", IDirect3DDevice9_SetFVF(dev, FVF));
    check("SetStreamSource", IDirect3DDevice9_SetStreamSource(dev, 0, vb, 0, sizeof(Vertex)));
    check("SetIndices", IDirect3DDevice9_SetIndices(dev, ib));
    check("DrawIndexedPrimitive", IDirect3DDevice9_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
    check("EndScene", IDirect3DDevice9_EndScene(dev));

    // Objets rendus par l'appareil : mêmes enveloppeurs (identité des pointeurs).
    IDirect3DBaseTexture9 *bound = NULL;
    check("GetTexture", IDirect3DDevice9_GetTexture(dev, 0, &bound));
    put("meme texture ");
    put_int(bound == (IDirect3DBaseTexture9 *)tex);
    put("\n");
    IDirect3DBaseTexture9_Release(bound);

    // Image relue.
    IDirect3DSurface9 *back = NULL, *copy = NULL;
    check("GetBackBuffer", IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back));
    check("CreateOffscreenPlainSurface", IDirect3DDevice9_CreateOffscreenPlainSurface(dev, 64, 64, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, NULL));
    check("GetRenderTargetData", IDirect3DDevice9_GetRenderTargetData(dev, back, copy));
    check("LockRect image", IDirect3DSurface9_LockRect(copy, &lr, NULL, D3DLOCK_READONLY));
    static const int points[][2] = {{2, 2}, {12, 12}, {20, 12}, {12, 50}, {50, 50}, {60, 60}};
    for (int i = 0; i < 6; i++) {
        put("pixel ");
        put_int(points[i][0]);
        put(",");
        put_int(points[i][1]);
        put(" ");
        put_hex(((const DWORD *)((const BYTE *)lr.pBits + points[i][1] * lr.Pitch))[points[i][0]] & 0xffffff);
        put("\n");
    }
    IDirect3DSurface9_UnlockRect(copy);
    // Fenêtre cachée : S_PRESENT_OCCLUDED, un succès.
    HRESULT presented = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
    check("Present", SUCCEEDED(presented) ? D3D_OK : presented);

    IDirect3DSurface9_Release(copy);
    IDirect3DSurface9_Release(back);
    IDirect3DTexture9_Release(tex);
    IDirect3DIndexBuffer9_Release(ib);
    IDirect3DVertexBuffer9_Release(vb);
    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    put("fin\n");
    ExitProcess(0);
}
