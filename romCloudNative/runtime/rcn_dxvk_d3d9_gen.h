// Généré par scripts/gen_dxvk_d3d9.py depuis d3d9.h (en-têtes de DXVK) : ne pas modifier.
// Inclus par rcn_dxvk_d3d9.c (qui fournit d9_unwrap, d9_out, d9_out_iid, d9_hwnd, d9_float_arg…).

enum { D9_FAMILY_COUNT = 15 };
static const IID *const d9_family_iid[D9_FAMILY_COUNT] = {&IID_IDirect3D9, &IID_IDirect3DDevice9, &IID_IDirect3DSwapChain9, &IID_IDirect3DTexture9, &IID_IDirect3DVolumeTexture9, &IID_IDirect3DCubeTexture9, &IID_IDirect3DVertexBuffer9, &IID_IDirect3DIndexBuffer9, &IID_IDirect3DSurface9, &IID_IDirect3DVolume9, &IID_IDirect3DVertexDeclaration9, &IID_IDirect3DVertexShader9, &IID_IDirect3DPixelShader9, &IID_IDirect3DStateBlock9, &IID_IDirect3DQuery9};
static const char *const d9_family_name[D9_FAMILY_COUNT] = {"IDirect3D9", "IDirect3DDevice9", "IDirect3DSwapChain9", "IDirect3DTexture9", "IDirect3DVolumeTexture9", "IDirect3DCubeTexture9", "IDirect3DVertexBuffer9", "IDirect3DIndexBuffer9", "IDirect3DSurface9", "IDirect3DVolume9", "IDirect3DVertexDeclaration9", "IDirect3DVertexShader9", "IDirect3DPixelShader9", "IDirect3DStateBlock9", "IDirect3DQuery9"};

static void d9_IDirect3D9_RegisterSoftwareDevice(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    void * a1 = (void *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->RegisterSoftwareDevice(self, a1);
    if (FAILED(r)) d9_failed("IDirect3D9::RegisterSoftwareDevice", r);
    RET((u32)r);
}

static void d9_IDirect3D9_GetAdapterCount(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetAdapterCount(self);
    RET((u32)r);
}

static void d9_IDirect3D9_GetAdapterIdentifier(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)(u32)rcn_arg(ctx, 2);
    D3DADAPTER_IDENTIFIER9 * a3 = (D3DADAPTER_IDENTIFIER9 *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetAdapterIdentifier(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3D9::GetAdapterIdentifier", r);
    RET((u32)r);
}

static void d9_IDirect3D9_GetAdapterModeCount(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DFORMAT a2 = (D3DFORMAT)(u32)rcn_arg(ctx, 2);
    UINT r = self->lpVtbl->GetAdapterModeCount(self, a1, a2);
    RET((u32)r);
}

static void d9_IDirect3D9_EnumAdapterModes(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DFORMAT a2 = (D3DFORMAT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    D3DDISPLAYMODE * a4 = (D3DDISPLAYMODE *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->EnumAdapterModes(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3D9::EnumAdapterModes", r);
    RET((u32)r);
}

static void d9_IDirect3D9_GetAdapterDisplayMode(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDISPLAYMODE * a2 = (D3DDISPLAYMODE *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetAdapterDisplayMode(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3D9::GetAdapterDisplayMode", r);
    RET((u32)r);
}

static void d9_IDirect3D9_CheckDeviceType(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDEVTYPE a2 = (D3DDEVTYPE)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DFORMAT a4 = (D3DFORMAT)(u32)rcn_arg(ctx, 4);
    WINBOOL a5 = (WINBOOL)(u32)rcn_arg(ctx, 5);
    HRESULT r = self->lpVtbl->CheckDeviceType(self, a1, a2, a3, a4, a5);
    if (FAILED(r)) d9_failed("IDirect3D9::CheckDeviceType", r);
    RET((u32)r);
}

static void d9_IDirect3D9_CheckDeviceFormat(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDEVTYPE a2 = (D3DDEVTYPE)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    DWORD a4 = (DWORD)(u32)rcn_arg(ctx, 4);
    D3DRESOURCETYPE a5 = (D3DRESOURCETYPE)(u32)rcn_arg(ctx, 5);
    D3DFORMAT a6 = (D3DFORMAT)(u32)rcn_arg(ctx, 6);
    HRESULT r = self->lpVtbl->CheckDeviceFormat(self, a1, a2, a3, a4, a5, a6);
    if (FAILED(r)) d9_failed("IDirect3D9::CheckDeviceFormat", r);
    RET((u32)r);
}

static void d9_IDirect3D9_CheckDeviceMultiSampleType(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDEVTYPE a2 = (D3DDEVTYPE)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    WINBOOL a4 = (WINBOOL)(u32)rcn_arg(ctx, 4);
    D3DMULTISAMPLE_TYPE a5 = (D3DMULTISAMPLE_TYPE)(u32)rcn_arg(ctx, 5);
    DWORD * a6 = (DWORD *)(uintptr_t)rcn_arg(ctx, 6);
    HRESULT r = self->lpVtbl->CheckDeviceMultiSampleType(self, a1, a2, a3, a4, a5, a6);
    if (FAILED(r)) d9_failed("IDirect3D9::CheckDeviceMultiSampleType", r);
    RET((u32)r);
}

static void d9_IDirect3D9_CheckDepthStencilMatch(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDEVTYPE a2 = (D3DDEVTYPE)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DFORMAT a4 = (D3DFORMAT)(u32)rcn_arg(ctx, 4);
    D3DFORMAT a5 = (D3DFORMAT)(u32)rcn_arg(ctx, 5);
    HRESULT r = self->lpVtbl->CheckDepthStencilMatch(self, a1, a2, a3, a4, a5);
    if (FAILED(r)) d9_failed("IDirect3D9::CheckDepthStencilMatch", r);
    RET((u32)r);
}

static void d9_IDirect3D9_CheckDeviceFormatConversion(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDEVTYPE a2 = (D3DDEVTYPE)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DFORMAT a4 = (D3DFORMAT)(u32)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->CheckDeviceFormatConversion(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3D9::CheckDeviceFormatConversion", r);
    RET((u32)r);
}

static void d9_IDirect3D9_GetDeviceCaps(RcnCtx *ctx) {
    IDirect3D9 *self = (IDirect3D9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDEVTYPE a2 = (D3DDEVTYPE)(u32)rcn_arg(ctx, 2);
    D3DCAPS9 * a3 = (D3DCAPS9 *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetDeviceCaps(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3D9::GetDeviceCaps", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_TestCooperativeLevel(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->TestCooperativeLevel(self);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::TestCooperativeLevel", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetAvailableTextureMem(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetAvailableTextureMem(self);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_EvictManagedResources(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->EvictManagedResources(self);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::EvictManagedResources", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetDirect3D(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3D9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDirect3D(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 0);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetDirect3D", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetDeviceCaps(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DCAPS9 * a1 = (D3DCAPS9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDeviceCaps(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetDeviceCaps", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetDisplayMode(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DDISPLAYMODE * a2 = (D3DDISPLAYMODE *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetDisplayMode(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetDisplayMode", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetCursorProperties(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    IDirect3DSurface9 * a3 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 3));
    HRESULT r = self->lpVtbl->SetCursorProperties(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetCursorProperties", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetCursorPosition(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    int a1 = (int)(u32)rcn_arg(ctx, 1);
    int a2 = (int)(u32)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    self->lpVtbl->SetCursorPosition(self, a1, a2, a3);
}

static void d9_IDirect3DDevice9_ShowCursor(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)(u32)rcn_arg(ctx, 1);
    WINBOOL r = self->lpVtbl->ShowCursor(self, a1);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetSwapChain(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DSwapChain9 *t2 = NULL;
    HRESULT r = self->lpVtbl->GetSwapChain(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetSwapChain", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetNumberOfSwapChains(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetNumberOfSwapChains(self);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetBackBuffer(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    D3DBACKBUFFER_TYPE a3 = (D3DBACKBUFFER_TYPE)(u32)rcn_arg(ctx, 3);
    u64 g4 = rcn_arg(ctx, 4); IDirect3DSurface9 *t4 = NULL;
    HRESULT r = self->lpVtbl->GetBackBuffer(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) d9_out(ctx, g4, t4, 8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetBackBuffer", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetRasterStatus(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DRASTER_STATUS * a2 = (D3DRASTER_STATUS *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetRasterStatus(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetRasterStatus", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetDialogBoxMode(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetDialogBoxMode(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetDialogBoxMode", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetGammaRamp(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)(u32)rcn_arg(ctx, 2);
    const D3DGAMMARAMP * a3 = (const D3DGAMMARAMP *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->SetGammaRamp(self, a1, a2, a3);
}

static void d9_IDirect3DDevice9_GetGammaRamp(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DGAMMARAMP * a2 = (D3DGAMMARAMP *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->GetGammaRamp(self, a1, a2);
}

static void d9_IDirect3DDevice9_CreateTexture(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    DWORD a4 = (DWORD)(u32)rcn_arg(ctx, 4);
    D3DFORMAT a5 = (D3DFORMAT)(u32)rcn_arg(ctx, 5);
    D3DPOOL a6 = (D3DPOOL)(u32)rcn_arg(ctx, 6);
    u64 g7 = rcn_arg(ctx, 7); IDirect3DTexture9 *t7 = NULL;
    HRESULT r = self->lpVtbl->CreateTexture(self, a1, a2, a3, a4, a5, a6, g7 ? &t7 : NULL, NULL);
    if (g7) d9_out(ctx, g7, t7, 3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateTexture", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateVolumeTexture(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    UINT a4 = (UINT)(u32)rcn_arg(ctx, 4);
    DWORD a5 = (DWORD)(u32)rcn_arg(ctx, 5);
    D3DFORMAT a6 = (D3DFORMAT)(u32)rcn_arg(ctx, 6);
    D3DPOOL a7 = (D3DPOOL)(u32)rcn_arg(ctx, 7);
    u64 g8 = rcn_arg(ctx, 8); IDirect3DVolumeTexture9 *t8 = NULL;
    HRESULT r = self->lpVtbl->CreateVolumeTexture(self, a1, a2, a3, a4, a5, a6, a7, g8 ? &t8 : NULL, NULL);
    if (g8) d9_out(ctx, g8, t8, 4);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateVolumeTexture", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateCubeTexture(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    D3DFORMAT a4 = (D3DFORMAT)(u32)rcn_arg(ctx, 4);
    D3DPOOL a5 = (D3DPOOL)(u32)rcn_arg(ctx, 5);
    u64 g6 = rcn_arg(ctx, 6); IDirect3DCubeTexture9 *t6 = NULL;
    HRESULT r = self->lpVtbl->CreateCubeTexture(self, a1, a2, a3, a4, a5, g6 ? &t6 : NULL, NULL);
    if (g6) d9_out(ctx, g6, t6, 5);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateCubeTexture", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateVertexBuffer(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)(u32)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    D3DPOOL a4 = (D3DPOOL)(u32)rcn_arg(ctx, 4);
    u64 g5 = rcn_arg(ctx, 5); IDirect3DVertexBuffer9 *t5 = NULL;
    HRESULT r = self->lpVtbl->CreateVertexBuffer(self, a1, a2, a3, a4, g5 ? &t5 : NULL, NULL);
    if (g5) d9_out(ctx, g5, t5, 6);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateVertexBuffer", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateIndexBuffer(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DPOOL a4 = (D3DPOOL)(u32)rcn_arg(ctx, 4);
    u64 g5 = rcn_arg(ctx, 5); IDirect3DIndexBuffer9 *t5 = NULL;
    HRESULT r = self->lpVtbl->CreateIndexBuffer(self, a1, a2, a3, a4, g5 ? &t5 : NULL, NULL);
    if (g5) d9_out(ctx, g5, t5, 7);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateIndexBuffer", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateRenderTarget(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DMULTISAMPLE_TYPE a4 = (D3DMULTISAMPLE_TYPE)(u32)rcn_arg(ctx, 4);
    DWORD a5 = (DWORD)(u32)rcn_arg(ctx, 5);
    WINBOOL a6 = (WINBOOL)(u32)rcn_arg(ctx, 6);
    u64 g7 = rcn_arg(ctx, 7); IDirect3DSurface9 *t7 = NULL;
    HRESULT r = self->lpVtbl->CreateRenderTarget(self, a1, a2, a3, a4, a5, a6, g7 ? &t7 : NULL, NULL);
    if (g7) d9_out(ctx, g7, t7, 8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateRenderTarget", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateDepthStencilSurface(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DMULTISAMPLE_TYPE a4 = (D3DMULTISAMPLE_TYPE)(u32)rcn_arg(ctx, 4);
    DWORD a5 = (DWORD)(u32)rcn_arg(ctx, 5);
    WINBOOL a6 = (WINBOOL)(u32)rcn_arg(ctx, 6);
    u64 g7 = rcn_arg(ctx, 7); IDirect3DSurface9 *t7 = NULL;
    HRESULT r = self->lpVtbl->CreateDepthStencilSurface(self, a1, a2, a3, a4, a5, a6, g7 ? &t7 : NULL, NULL);
    if (g7) d9_out(ctx, g7, t7, 8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateDepthStencilSurface", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_UpdateSurface(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DSurface9 * a1 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 1));
    const RECT * a2 = (const RECT *)(uintptr_t)rcn_arg(ctx, 2);
    IDirect3DSurface9 * a3 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 3));
    const POINT * a4 = (const POINT *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->UpdateSurface(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::UpdateSurface", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_UpdateTexture(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DBaseTexture9 * a1 = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 1));
    IDirect3DBaseTexture9 * a2 = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->UpdateTexture(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::UpdateTexture", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetRenderTargetData(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DSurface9 * a1 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 1));
    IDirect3DSurface9 * a2 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->GetRenderTargetData(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetRenderTargetData", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetFrontBufferData(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    IDirect3DSurface9 * a2 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->GetFrontBufferData(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetFrontBufferData", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_StretchRect(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DSurface9 * a1 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 1));
    const RECT * a2 = (const RECT *)(uintptr_t)rcn_arg(ctx, 2);
    IDirect3DSurface9 * a3 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 3));
    const RECT * a4 = (const RECT *)(uintptr_t)rcn_arg(ctx, 4);
    D3DTEXTUREFILTERTYPE a5 = (D3DTEXTUREFILTERTYPE)(u32)rcn_arg(ctx, 5);
    HRESULT r = self->lpVtbl->StretchRect(self, a1, a2, a3, a4, a5);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::StretchRect", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_ColorFill(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DSurface9 * a1 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 1));
    const RECT * a2 = (const RECT *)(uintptr_t)rcn_arg(ctx, 2);
    D3DCOLOR a3 = (D3DCOLOR)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->ColorFill(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::ColorFill", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateOffscreenPlainSurface(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    D3DFORMAT a3 = (D3DFORMAT)(u32)rcn_arg(ctx, 3);
    D3DPOOL a4 = (D3DPOOL)(u32)rcn_arg(ctx, 4);
    u64 g5 = rcn_arg(ctx, 5); IDirect3DSurface9 *t5 = NULL;
    HRESULT r = self->lpVtbl->CreateOffscreenPlainSurface(self, a1, a2, a3, a4, g5 ? &t5 : NULL, NULL);
    if (g5) d9_out(ctx, g5, t5, 8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateOffscreenPlainSurface", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetRenderTarget(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    IDirect3DSurface9 * a2 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->SetRenderTarget(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetRenderTarget", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetRenderTarget(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DSurface9 *t2 = NULL;
    HRESULT r = self->lpVtbl->GetRenderTarget(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetRenderTarget", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetDepthStencilSurface(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DSurface9 * a1 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->SetDepthStencilSurface(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetDepthStencilSurface", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetDepthStencilSurface(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DSurface9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDepthStencilSurface(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetDepthStencilSurface", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_BeginScene(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->BeginScene(self);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::BeginScene", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_EndScene(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->EndScene(self);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::EndScene", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_Clear(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    const D3DRECT * a2 = (const D3DRECT *)(uintptr_t)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    D3DCOLOR a4 = (D3DCOLOR)(u32)rcn_arg(ctx, 4);
    float a5 = d9_float_arg(ctx, 5);
    DWORD a6 = (DWORD)(u32)rcn_arg(ctx, 6);
    HRESULT r = self->lpVtbl->Clear(self, a1, a2, a3, a4, a5, a6);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::Clear", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetTransform(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DTRANSFORMSTATETYPE a1 = (D3DTRANSFORMSTATETYPE)(u32)rcn_arg(ctx, 1);
    const D3DMATRIX * a2 = (const D3DMATRIX *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetTransform(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetTransform", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetTransform(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DTRANSFORMSTATETYPE a1 = (D3DTRANSFORMSTATETYPE)(u32)rcn_arg(ctx, 1);
    D3DMATRIX * a2 = (D3DMATRIX *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetTransform(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetTransform", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_MultiplyTransform(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DTRANSFORMSTATETYPE a1 = (D3DTRANSFORMSTATETYPE)(u32)rcn_arg(ctx, 1);
    const D3DMATRIX * a2 = (const D3DMATRIX *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->MultiplyTransform(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::MultiplyTransform", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetViewport(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const D3DVIEWPORT9 * a1 = (const D3DVIEWPORT9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetViewport(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetViewport", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetViewport(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DVIEWPORT9 * a1 = (D3DVIEWPORT9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetViewport(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetViewport", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetMaterial(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const D3DMATERIAL9 * a1 = (const D3DMATERIAL9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetMaterial(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetMaterial", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetMaterial(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DMATERIAL9 * a1 = (D3DMATERIAL9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetMaterial(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetMaterial", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetLight(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    const D3DLIGHT9 * a2 = (const D3DLIGHT9 *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetLight(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetLight", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetLight(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    D3DLIGHT9 * a2 = (D3DLIGHT9 *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetLight(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetLight", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_LightEnable(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    WINBOOL a2 = (WINBOOL)(u32)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->LightEnable(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::LightEnable", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetLightEnable(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    WINBOOL * a2 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetLightEnable(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetLightEnable", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetClipPlane(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    const float * a2 = (const float *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetClipPlane(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetClipPlane", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetClipPlane(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    float * a2 = (float *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetClipPlane(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetClipPlane", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetRenderState(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DRENDERSTATETYPE a1 = (D3DRENDERSTATETYPE)(u32)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)(u32)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetRenderState(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetRenderState", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetRenderState(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DRENDERSTATETYPE a1 = (D3DRENDERSTATETYPE)(u32)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetRenderState(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetRenderState", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateStateBlock(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DSTATEBLOCKTYPE a1 = (D3DSTATEBLOCKTYPE)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DStateBlock9 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateStateBlock(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 13);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateStateBlock", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_BeginStateBlock(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->BeginStateBlock(self);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::BeginStateBlock", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_EndStateBlock(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DStateBlock9 *t1 = NULL;
    HRESULT r = self->lpVtbl->EndStateBlock(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 13);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::EndStateBlock", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetClipStatus(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const D3DCLIPSTATUS9 * a1 = (const D3DCLIPSTATUS9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetClipStatus(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetClipStatus", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetClipStatus(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DCLIPSTATUS9 * a1 = (D3DCLIPSTATUS9 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetClipStatus(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetClipStatus", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetTexture(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DBaseTexture9 *t2 = NULL;
    HRESULT r = self->lpVtbl->GetTexture(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, D9_FAM_DYNAMIC);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetTexture", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetTexture(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    IDirect3DBaseTexture9 * a2 = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->SetTexture(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetTexture", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetTextureStageState(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    D3DTEXTURESTAGESTATETYPE a2 = (D3DTEXTURESTAGESTATETYPE)(u32)rcn_arg(ctx, 2);
    DWORD * a3 = (DWORD *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetTextureStageState(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetTextureStageState", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetTextureStageState(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    D3DTEXTURESTAGESTATETYPE a2 = (D3DTEXTURESTAGESTATETYPE)(u32)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetTextureStageState(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetTextureStageState", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetSamplerState(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    D3DSAMPLERSTATETYPE a2 = (D3DSAMPLERSTATETYPE)(u32)rcn_arg(ctx, 2);
    DWORD * a3 = (DWORD *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetSamplerState(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetSamplerState", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetSamplerState(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    D3DSAMPLERSTATETYPE a2 = (D3DSAMPLERSTATETYPE)(u32)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetSamplerState(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetSamplerState", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_ValidateDevice(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD * a1 = (DWORD *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->ValidateDevice(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::ValidateDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetPaletteEntries(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const PALETTEENTRY * a2 = (const PALETTEENTRY *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetPaletteEntries(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetPaletteEntries", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetPaletteEntries(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    PALETTEENTRY * a2 = (PALETTEENTRY *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetPaletteEntries(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetPaletteEntries", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetCurrentTexturePalette(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetCurrentTexturePalette(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetCurrentTexturePalette", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetCurrentTexturePalette(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetCurrentTexturePalette(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetCurrentTexturePalette", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetScissorRect(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const RECT * a1 = (const RECT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetScissorRect(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetScissorRect", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetScissorRect(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    RECT * a1 = (RECT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetScissorRect(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetScissorRect", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetSoftwareVertexProcessing(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetSoftwareVertexProcessing(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetSoftwareVertexProcessing", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetSoftwareVertexProcessing(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->GetSoftwareVertexProcessing(self);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetNPatchMode(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    float a1 = d9_float_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetNPatchMode(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetNPatchMode", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetNPatchMode(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    float r = self->lpVtbl->GetNPatchMode(self);
    rcn_fpush(ctx, r);
}

static void d9_IDirect3DDevice9_DrawPrimitive(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DPRIMITIVETYPE a1 = (D3DPRIMITIVETYPE)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->DrawPrimitive(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DrawPrimitive", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_DrawIndexedPrimitive(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DPRIMITIVETYPE a1 = (D3DPRIMITIVETYPE)(u32)rcn_arg(ctx, 1);
    INT a2 = (INT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    UINT a4 = (UINT)(u32)rcn_arg(ctx, 4);
    UINT a5 = (UINT)(u32)rcn_arg(ctx, 5);
    UINT a6 = (UINT)(u32)rcn_arg(ctx, 6);
    HRESULT r = self->lpVtbl->DrawIndexedPrimitive(self, a1, a2, a3, a4, a5, a6);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DrawIndexedPrimitive", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_DrawPrimitiveUP(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DPRIMITIVETYPE a1 = (D3DPRIMITIVETYPE)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    const void * a3 = (const void *)(uintptr_t)rcn_arg(ctx, 3);
    UINT a4 = (UINT)(u32)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->DrawPrimitiveUP(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DrawPrimitiveUP", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_DrawIndexedPrimitiveUP(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DPRIMITIVETYPE a1 = (D3DPRIMITIVETYPE)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    UINT a4 = (UINT)(u32)rcn_arg(ctx, 4);
    const void * a5 = (const void *)(uintptr_t)rcn_arg(ctx, 5);
    D3DFORMAT a6 = (D3DFORMAT)(u32)rcn_arg(ctx, 6);
    const void * a7 = (const void *)(uintptr_t)rcn_arg(ctx, 7);
    UINT a8 = (UINT)(u32)rcn_arg(ctx, 8);
    HRESULT r = self->lpVtbl->DrawIndexedPrimitiveUP(self, a1, a2, a3, a4, a5, a6, a7, a8);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DrawIndexedPrimitiveUP", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_ProcessVertices(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    IDirect3DVertexBuffer9 * a4 = (IDirect3DVertexBuffer9 *)d9_unwrap(rcn_arg(ctx, 4));
    IDirect3DVertexDeclaration9 * a5 = (IDirect3DVertexDeclaration9 *)d9_unwrap(rcn_arg(ctx, 5));
    DWORD a6 = (DWORD)(u32)rcn_arg(ctx, 6);
    HRESULT r = self->lpVtbl->ProcessVertices(self, a1, a2, a3, a4, a5, a6);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::ProcessVertices", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateVertexDeclaration(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const D3DVERTEXELEMENT9 * a1 = (const D3DVERTEXELEMENT9 *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DVertexDeclaration9 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateVertexDeclaration(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 10);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateVertexDeclaration", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetVertexDeclaration(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DVertexDeclaration9 * a1 = (IDirect3DVertexDeclaration9 *)d9_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->SetVertexDeclaration(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetVertexDeclaration", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetVertexDeclaration(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DVertexDeclaration9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetVertexDeclaration(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 10);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetVertexDeclaration", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetFVF(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetFVF(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetFVF", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetFVF(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD * a1 = (DWORD *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetFVF(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetFVF", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateVertexShader(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const DWORD * a1 = (const DWORD *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DVertexShader9 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateVertexShader(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 11);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateVertexShader", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetVertexShader(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DVertexShader9 * a1 = (IDirect3DVertexShader9 *)d9_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->SetVertexShader(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetVertexShader", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetVertexShader(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DVertexShader9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetVertexShader(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 11);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetVertexShader", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetVertexShaderConstantF(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const float * a2 = (const float *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetVertexShaderConstantF(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetVertexShaderConstantF", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetVertexShaderConstantF(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    float * a2 = (float *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetVertexShaderConstantF(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetVertexShaderConstantF", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetVertexShaderConstantI(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const int * a2 = (const int *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetVertexShaderConstantI(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetVertexShaderConstantI", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetVertexShaderConstantI(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    int * a2 = (int *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetVertexShaderConstantI(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetVertexShaderConstantI", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetVertexShaderConstantB(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const WINBOOL * a2 = (const WINBOOL *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetVertexShaderConstantB(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetVertexShaderConstantB", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetVertexShaderConstantB(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    WINBOOL * a2 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetVertexShaderConstantB(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetVertexShaderConstantB", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetStreamSource(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    IDirect3DVertexBuffer9 * a2 = (IDirect3DVertexBuffer9 *)d9_unwrap(rcn_arg(ctx, 2));
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    UINT a4 = (UINT)(u32)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->SetStreamSource(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetStreamSource", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetStreamSource(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DVertexBuffer9 *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->GetStreamSource(self, a1, g2 ? &t2 : NULL, a3, a4);
    if (g2) d9_out(ctx, g2, t2, 6);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetStreamSource", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetStreamSourceFreq(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetStreamSourceFreq(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetStreamSourceFreq", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetStreamSourceFreq(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetStreamSourceFreq(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetStreamSourceFreq", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetIndices(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DIndexBuffer9 * a1 = (IDirect3DIndexBuffer9 *)d9_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->SetIndices(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetIndices", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetIndices(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DIndexBuffer9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetIndices(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 7);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetIndices", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreatePixelShader(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const DWORD * a1 = (const DWORD *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DPixelShader9 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreatePixelShader(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 12);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreatePixelShader", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetPixelShader(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DPixelShader9 * a1 = (IDirect3DPixelShader9 *)d9_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->SetPixelShader(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetPixelShader", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetPixelShader(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DPixelShader9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetPixelShader(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 12);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetPixelShader", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetPixelShaderConstantF(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const float * a2 = (const float *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetPixelShaderConstantF(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetPixelShaderConstantF", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetPixelShaderConstantF(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    float * a2 = (float *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPixelShaderConstantF(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetPixelShaderConstantF", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetPixelShaderConstantI(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const int * a2 = (const int *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetPixelShaderConstantI(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetPixelShaderConstantI", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetPixelShaderConstantI(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    int * a2 = (int *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPixelShaderConstantI(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetPixelShaderConstantI", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_SetPixelShaderConstantB(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const WINBOOL * a2 = (const WINBOOL *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetPixelShaderConstantB(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::SetPixelShaderConstantB", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_GetPixelShaderConstantB(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    WINBOOL * a2 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPixelShaderConstantB(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::GetPixelShaderConstantB", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_DrawRectPatch(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const float * a2 = (const float *)(uintptr_t)rcn_arg(ctx, 2);
    const D3DRECTPATCH_INFO * a3 = (const D3DRECTPATCH_INFO *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->DrawRectPatch(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DrawRectPatch", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_DrawTriPatch(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    const float * a2 = (const float *)(uintptr_t)rcn_arg(ctx, 2);
    const D3DTRIPATCH_INFO * a3 = (const D3DTRIPATCH_INFO *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->DrawTriPatch(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DrawTriPatch", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_DeletePatch(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->DeletePatch(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::DeletePatch", r);
    RET((u32)r);
}

static void d9_IDirect3DDevice9_CreateQuery(RcnCtx *ctx) {
    IDirect3DDevice9 *self = (IDirect3DDevice9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DQUERYTYPE a1 = (D3DQUERYTYPE)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DQuery9 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateQuery(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 14);
    if (FAILED(r)) d9_failed("IDirect3DDevice9::CreateQuery", r);
    RET((u32)r);
}

static void d9_IDirect3DSwapChain9_GetFrontBufferData(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = (IDirect3DSwapChain9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    IDirect3DSurface9 * a1 = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->GetFrontBufferData(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DSwapChain9::GetFrontBufferData", r);
    RET((u32)r);
}

static void d9_IDirect3DSwapChain9_GetBackBuffer(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = (IDirect3DSwapChain9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DBACKBUFFER_TYPE a2 = (D3DBACKBUFFER_TYPE)(u32)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); IDirect3DSurface9 *t3 = NULL;
    HRESULT r = self->lpVtbl->GetBackBuffer(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) d9_out(ctx, g3, t3, 8);
    if (FAILED(r)) d9_failed("IDirect3DSwapChain9::GetBackBuffer", r);
    RET((u32)r);
}

static void d9_IDirect3DSwapChain9_GetRasterStatus(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = (IDirect3DSwapChain9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DRASTER_STATUS * a1 = (D3DRASTER_STATUS *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetRasterStatus(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DSwapChain9::GetRasterStatus", r);
    RET((u32)r);
}

static void d9_IDirect3DSwapChain9_GetDisplayMode(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = (IDirect3DSwapChain9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DDISPLAYMODE * a1 = (D3DDISPLAYMODE *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDisplayMode(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DSwapChain9::GetDisplayMode", r);
    RET((u32)r);
}

static void d9_IDirect3DSwapChain9_GetDevice(RcnCtx *ctx) {
    IDirect3DSwapChain9 *self = (IDirect3DSwapChain9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DSwapChain9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DResource9_GetDevice(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DResource9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DResource9_SetPrivateData(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    const void * a2 = (const void *)(uintptr_t)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    DWORD a4 = (DWORD)(u32)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->SetPrivateData(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3DResource9::SetPrivateData", r);
    RET((u32)r);
}

static void d9_IDirect3DResource9_GetPrivateData(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    void * a2 = (void *)(uintptr_t)rcn_arg(ctx, 2);
    DWORD * a3 = (DWORD *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DResource9::GetPrivateData", r);
    RET((u32)r);
}

static void d9_IDirect3DResource9_FreePrivateData(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->FreePrivateData(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DResource9::FreePrivateData", r);
    RET((u32)r);
}

static void d9_IDirect3DResource9_SetPriority(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    DWORD r = self->lpVtbl->SetPriority(self, a1);
    RET((u32)r);
}

static void d9_IDirect3DResource9_GetPriority(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD r = self->lpVtbl->GetPriority(self);
    RET((u32)r);
}

static void d9_IDirect3DResource9_PreLoad(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    self->lpVtbl->PreLoad(self);
}

static void d9_IDirect3DResource9_GetType(RcnCtx *ctx) {
    IDirect3DResource9 *self = (IDirect3DResource9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DRESOURCETYPE r = self->lpVtbl->GetType(self);
    RET((u32)r);
}

static void d9_IDirect3DBaseTexture9_SetLOD(RcnCtx *ctx) {
    IDirect3DBaseTexture9 *self = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    DWORD r = self->lpVtbl->SetLOD(self, a1);
    RET((u32)r);
}

static void d9_IDirect3DBaseTexture9_GetLOD(RcnCtx *ctx) {
    IDirect3DBaseTexture9 *self = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD r = self->lpVtbl->GetLOD(self);
    RET((u32)r);
}

static void d9_IDirect3DBaseTexture9_GetLevelCount(RcnCtx *ctx) {
    IDirect3DBaseTexture9 *self = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD r = self->lpVtbl->GetLevelCount(self);
    RET((u32)r);
}

static void d9_IDirect3DBaseTexture9_SetAutoGenFilterType(RcnCtx *ctx) {
    IDirect3DBaseTexture9 *self = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DTEXTUREFILTERTYPE a1 = (D3DTEXTUREFILTERTYPE)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetAutoGenFilterType(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DBaseTexture9::SetAutoGenFilterType", r);
    RET((u32)r);
}

static void d9_IDirect3DBaseTexture9_GetAutoGenFilterType(RcnCtx *ctx) {
    IDirect3DBaseTexture9 *self = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DTEXTUREFILTERTYPE r = self->lpVtbl->GetAutoGenFilterType(self);
    RET((u32)r);
}

static void d9_IDirect3DBaseTexture9_GenerateMipSubLevels(RcnCtx *ctx) {
    IDirect3DBaseTexture9 *self = (IDirect3DBaseTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    self->lpVtbl->GenerateMipSubLevels(self);
}

static void d9_IDirect3DTexture9_GetLevelDesc(RcnCtx *ctx) {
    IDirect3DTexture9 *self = (IDirect3DTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DSURFACE_DESC * a2 = (D3DSURFACE_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetLevelDesc(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DTexture9::GetLevelDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DTexture9_GetSurfaceLevel(RcnCtx *ctx) {
    IDirect3DTexture9 *self = (IDirect3DTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DSurface9 *t2 = NULL;
    HRESULT r = self->lpVtbl->GetSurfaceLevel(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 8);
    if (FAILED(r)) d9_failed("IDirect3DTexture9::GetSurfaceLevel", r);
    RET((u32)r);
}

static void d9_IDirect3DTexture9_AddDirtyRect(RcnCtx *ctx) {
    IDirect3DTexture9 *self = (IDirect3DTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const RECT * a1 = (const RECT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->AddDirtyRect(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DTexture9::AddDirtyRect", r);
    RET((u32)r);
}

static void d9_IDirect3DVolumeTexture9_GetLevelDesc(RcnCtx *ctx) {
    IDirect3DVolumeTexture9 *self = (IDirect3DVolumeTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DVOLUME_DESC * a2 = (D3DVOLUME_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetLevelDesc(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DVolumeTexture9::GetLevelDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DVolumeTexture9_GetVolumeLevel(RcnCtx *ctx) {
    IDirect3DVolumeTexture9 *self = (IDirect3DVolumeTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDirect3DVolume9 *t2 = NULL;
    HRESULT r = self->lpVtbl->GetVolumeLevel(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out(ctx, g2, t2, 9);
    if (FAILED(r)) d9_failed("IDirect3DVolumeTexture9::GetVolumeLevel", r);
    RET((u32)r);
}

static void d9_IDirect3DVolumeTexture9_AddDirtyBox(RcnCtx *ctx) {
    IDirect3DVolumeTexture9 *self = (IDirect3DVolumeTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    const D3DBOX * a1 = (const D3DBOX *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->AddDirtyBox(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DVolumeTexture9::AddDirtyBox", r);
    RET((u32)r);
}

static void d9_IDirect3DCubeTexture9_GetLevelDesc(RcnCtx *ctx) {
    IDirect3DCubeTexture9 *self = (IDirect3DCubeTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    UINT a1 = (UINT)(u32)rcn_arg(ctx, 1);
    D3DSURFACE_DESC * a2 = (D3DSURFACE_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetLevelDesc(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DCubeTexture9::GetLevelDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DCubeTexture9_GetCubeMapSurface(RcnCtx *ctx) {
    IDirect3DCubeTexture9 *self = (IDirect3DCubeTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DCUBEMAP_FACES a1 = (D3DCUBEMAP_FACES)(u32)rcn_arg(ctx, 1);
    UINT a2 = (UINT)(u32)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); IDirect3DSurface9 *t3 = NULL;
    HRESULT r = self->lpVtbl->GetCubeMapSurface(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) d9_out(ctx, g3, t3, 8);
    if (FAILED(r)) d9_failed("IDirect3DCubeTexture9::GetCubeMapSurface", r);
    RET((u32)r);
}

static void d9_IDirect3DCubeTexture9_AddDirtyRect(RcnCtx *ctx) {
    IDirect3DCubeTexture9 *self = (IDirect3DCubeTexture9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DCUBEMAP_FACES a1 = (D3DCUBEMAP_FACES)(u32)rcn_arg(ctx, 1);
    const RECT * a2 = (const RECT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->AddDirtyRect(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DCubeTexture9::AddDirtyRect", r);
    RET((u32)r);
}

static void d9_IDirect3DVertexBuffer9_GetDesc(RcnCtx *ctx) {
    IDirect3DVertexBuffer9 *self = (IDirect3DVertexBuffer9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DVERTEXBUFFER_DESC * a1 = (D3DVERTEXBUFFER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DVertexBuffer9::GetDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DIndexBuffer9_GetDesc(RcnCtx *ctx) {
    IDirect3DIndexBuffer9 *self = (IDirect3DIndexBuffer9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DINDEXBUFFER_DESC * a1 = (D3DINDEXBUFFER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DIndexBuffer9::GetDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DSurface9_GetContainer(RcnCtx *ctx) {
    IDirect3DSurface9 *self = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    HRESULT r = self->lpVtbl->GetContainer(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out_iid(ctx, g2, t2, a1, &r);
    if (FAILED(r)) d9_failed("IDirect3DSurface9::GetContainer", r);
    RET((u32)r);
}

static void d9_IDirect3DSurface9_GetDesc(RcnCtx *ctx) {
    IDirect3DSurface9 *self = (IDirect3DSurface9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DSURFACE_DESC * a1 = (D3DSURFACE_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DSurface9::GetDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DSurface9_GetDC(RcnCtx *ctx) { d9_unsupported(ctx, "IDirect3DSurface9::GetDC"); }

static void d9_IDirect3DSurface9_ReleaseDC(RcnCtx *ctx) { d9_unsupported(ctx, "IDirect3DSurface9::ReleaseDC"); }

static void d9_IDirect3DVolume9_GetDevice(RcnCtx *ctx) {
    IDirect3DVolume9 *self = (IDirect3DVolume9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DVolume9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DVolume9_SetPrivateData(RcnCtx *ctx) {
    IDirect3DVolume9 *self = (IDirect3DVolume9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    const void * a2 = (const void *)(uintptr_t)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    DWORD a4 = (DWORD)(u32)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->SetPrivateData(self, a1, a2, a3, a4);
    if (FAILED(r)) d9_failed("IDirect3DVolume9::SetPrivateData", r);
    RET((u32)r);
}

static void d9_IDirect3DVolume9_GetPrivateData(RcnCtx *ctx) {
    IDirect3DVolume9 *self = (IDirect3DVolume9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    void * a2 = (void *)(uintptr_t)rcn_arg(ctx, 2);
    DWORD * a3 = (DWORD *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DVolume9::GetPrivateData", r);
    RET((u32)r);
}

static void d9_IDirect3DVolume9_FreePrivateData(RcnCtx *ctx) {
    IDirect3DVolume9 *self = (IDirect3DVolume9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->FreePrivateData(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DVolume9::FreePrivateData", r);
    RET((u32)r);
}

static void d9_IDirect3DVolume9_GetContainer(RcnCtx *ctx) {
    IDirect3DVolume9 *self = (IDirect3DVolume9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    HRESULT r = self->lpVtbl->GetContainer(self, a1, g2 ? &t2 : NULL);
    if (g2) d9_out_iid(ctx, g2, t2, a1, &r);
    if (FAILED(r)) d9_failed("IDirect3DVolume9::GetContainer", r);
    RET((u32)r);
}

static void d9_IDirect3DVolume9_GetDesc(RcnCtx *ctx) {
    IDirect3DVolume9 *self = (IDirect3DVolume9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DVOLUME_DESC * a1 = (D3DVOLUME_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DVolume9::GetDesc", r);
    RET((u32)r);
}

static void d9_IDirect3DVertexDeclaration9_GetDevice(RcnCtx *ctx) {
    IDirect3DVertexDeclaration9 *self = (IDirect3DVertexDeclaration9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DVertexDeclaration9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DVertexDeclaration9_GetDeclaration(RcnCtx *ctx) {
    IDirect3DVertexDeclaration9 *self = (IDirect3DVertexDeclaration9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DVERTEXELEMENT9 * a1 = (D3DVERTEXELEMENT9 *)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetDeclaration(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DVertexDeclaration9::GetDeclaration", r);
    RET((u32)r);
}

static void d9_IDirect3DVertexShader9_GetDevice(RcnCtx *ctx) {
    IDirect3DVertexShader9 *self = (IDirect3DVertexShader9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DVertexShader9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DVertexShader9_GetFunction(RcnCtx *ctx) {
    IDirect3DVertexShader9 *self = (IDirect3DVertexShader9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    void * a1 = (void *)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetFunction(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DVertexShader9::GetFunction", r);
    RET((u32)r);
}

static void d9_IDirect3DPixelShader9_GetDevice(RcnCtx *ctx) {
    IDirect3DPixelShader9 *self = (IDirect3DPixelShader9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DPixelShader9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DPixelShader9_GetFunction(RcnCtx *ctx) {
    IDirect3DPixelShader9 *self = (IDirect3DPixelShader9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    void * a1 = (void *)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetFunction(self, a1, a2);
    if (FAILED(r)) d9_failed("IDirect3DPixelShader9::GetFunction", r);
    RET((u32)r);
}

static void d9_IDirect3DStateBlock9_GetDevice(RcnCtx *ctx) {
    IDirect3DStateBlock9 *self = (IDirect3DStateBlock9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DStateBlock9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DStateBlock9_Capture(RcnCtx *ctx) {
    IDirect3DStateBlock9 *self = (IDirect3DStateBlock9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->Capture(self);
    if (FAILED(r)) d9_failed("IDirect3DStateBlock9::Capture", r);
    RET((u32)r);
}

static void d9_IDirect3DStateBlock9_Apply(RcnCtx *ctx) {
    IDirect3DStateBlock9 *self = (IDirect3DStateBlock9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->Apply(self);
    if (FAILED(r)) d9_failed("IDirect3DStateBlock9::Apply", r);
    RET((u32)r);
}

static void d9_IDirect3DQuery9_GetDevice(RcnCtx *ctx) {
    IDirect3DQuery9 *self = (IDirect3DQuery9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDirect3DDevice9 *t1 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) d9_out(ctx, g1, t1, 1);
    if (FAILED(r)) d9_failed("IDirect3DQuery9::GetDevice", r);
    RET((u32)r);
}

static void d9_IDirect3DQuery9_GetType(RcnCtx *ctx) {
    IDirect3DQuery9 *self = (IDirect3DQuery9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    D3DQUERYTYPE r = self->lpVtbl->GetType(self);
    RET((u32)r);
}

static void d9_IDirect3DQuery9_GetDataSize(RcnCtx *ctx) {
    IDirect3DQuery9 *self = (IDirect3DQuery9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD r = self->lpVtbl->GetDataSize(self);
    RET((u32)r);
}

static void d9_IDirect3DQuery9_Issue(RcnCtx *ctx) {
    IDirect3DQuery9 *self = (IDirect3DQuery9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    DWORD a1 = (DWORD)(u32)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->Issue(self, a1);
    if (FAILED(r)) d9_failed("IDirect3DQuery9::Issue", r);
    RET((u32)r);
}

static void d9_IDirect3DQuery9_GetData(RcnCtx *ctx) {
    IDirect3DQuery9 *self = (IDirect3DQuery9 *)d9_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u32)E_POINTER); return; }
    void * a1 = (void *)(uintptr_t)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)(u32)rcn_arg(ctx, 2);
    DWORD a3 = (DWORD)(u32)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetData(self, a1, a2, a3);
    if (FAILED(r)) d9_failed("IDirect3DQuery9::GetData", r);
    RET((u32)r);
}

static void d9_special_AddRef(RcnCtx *ctx);
static void d9_special_IDirect3D9_CreateDevice(RcnCtx *ctx);
static void d9_special_IDirect3D9_GetAdapterMonitor(RcnCtx *ctx);
static void d9_special_IDirect3DCubeTexture9_LockRect(RcnCtx *ctx);
static void d9_special_IDirect3DCubeTexture9_UnlockRect(RcnCtx *ctx);
static void d9_special_IDirect3DDevice9_CreateAdditionalSwapChain(RcnCtx *ctx);
static void d9_special_IDirect3DDevice9_GetCreationParameters(RcnCtx *ctx);
static void d9_special_IDirect3DDevice9_Present(RcnCtx *ctx);
static void d9_special_IDirect3DDevice9_Reset(RcnCtx *ctx);
static void d9_special_IDirect3DIndexBuffer9_Lock(RcnCtx *ctx);
static void d9_special_IDirect3DIndexBuffer9_Unlock(RcnCtx *ctx);
static void d9_special_IDirect3DSurface9_LockRect(RcnCtx *ctx);
static void d9_special_IDirect3DSurface9_UnlockRect(RcnCtx *ctx);
static void d9_special_IDirect3DSwapChain9_GetPresentParameters(RcnCtx *ctx);
static void d9_special_IDirect3DSwapChain9_Present(RcnCtx *ctx);
static void d9_special_IDirect3DTexture9_LockRect(RcnCtx *ctx);
static void d9_special_IDirect3DTexture9_UnlockRect(RcnCtx *ctx);
static void d9_special_IDirect3DVertexBuffer9_Lock(RcnCtx *ctx);
static void d9_special_IDirect3DVertexBuffer9_Unlock(RcnCtx *ctx);
static void d9_special_IDirect3DVolume9_LockBox(RcnCtx *ctx);
static void d9_special_IDirect3DVolume9_UnlockBox(RcnCtx *ctx);
static void d9_special_IDirect3DVolumeTexture9_LockBox(RcnCtx *ctx);
static void d9_special_IDirect3DVolumeTexture9_UnlockBox(RcnCtx *ctx);
static void d9_special_QueryInterface(RcnCtx *ctx);
static void d9_special_Release(RcnCtx *ctx);

typedef struct { const char *name; RcnFn fn; u32 pop; } D9Method;
static const D9Method d9_methods_IDirect3D9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"RegisterSoftwareDevice", d9_IDirect3D9_RegisterSoftwareDevice, 8},
    {"GetAdapterCount", d9_IDirect3D9_GetAdapterCount, 4},
    {"GetAdapterIdentifier", d9_IDirect3D9_GetAdapterIdentifier, 16},
    {"GetAdapterModeCount", d9_IDirect3D9_GetAdapterModeCount, 12},
    {"EnumAdapterModes", d9_IDirect3D9_EnumAdapterModes, 20},
    {"GetAdapterDisplayMode", d9_IDirect3D9_GetAdapterDisplayMode, 12},
    {"CheckDeviceType", d9_IDirect3D9_CheckDeviceType, 24},
    {"CheckDeviceFormat", d9_IDirect3D9_CheckDeviceFormat, 28},
    {"CheckDeviceMultiSampleType", d9_IDirect3D9_CheckDeviceMultiSampleType, 28},
    {"CheckDepthStencilMatch", d9_IDirect3D9_CheckDepthStencilMatch, 24},
    {"CheckDeviceFormatConversion", d9_IDirect3D9_CheckDeviceFormatConversion, 20},
    {"GetDeviceCaps", d9_IDirect3D9_GetDeviceCaps, 16},
    {"GetAdapterMonitor", d9_special_IDirect3D9_GetAdapterMonitor, 8},
    {"CreateDevice", d9_special_IDirect3D9_CreateDevice, 28},
};
static const D9Method d9_methods_IDirect3DDevice9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"TestCooperativeLevel", d9_IDirect3DDevice9_TestCooperativeLevel, 4},
    {"GetAvailableTextureMem", d9_IDirect3DDevice9_GetAvailableTextureMem, 4},
    {"EvictManagedResources", d9_IDirect3DDevice9_EvictManagedResources, 4},
    {"GetDirect3D", d9_IDirect3DDevice9_GetDirect3D, 8},
    {"GetDeviceCaps", d9_IDirect3DDevice9_GetDeviceCaps, 8},
    {"GetDisplayMode", d9_IDirect3DDevice9_GetDisplayMode, 12},
    {"GetCreationParameters", d9_special_IDirect3DDevice9_GetCreationParameters, 8},
    {"SetCursorProperties", d9_IDirect3DDevice9_SetCursorProperties, 16},
    {"SetCursorPosition", d9_IDirect3DDevice9_SetCursorPosition, 16},
    {"ShowCursor", d9_IDirect3DDevice9_ShowCursor, 8},
    {"CreateAdditionalSwapChain", d9_special_IDirect3DDevice9_CreateAdditionalSwapChain, 12},
    {"GetSwapChain", d9_IDirect3DDevice9_GetSwapChain, 12},
    {"GetNumberOfSwapChains", d9_IDirect3DDevice9_GetNumberOfSwapChains, 4},
    {"Reset", d9_special_IDirect3DDevice9_Reset, 8},
    {"Present", d9_special_IDirect3DDevice9_Present, 20},
    {"GetBackBuffer", d9_IDirect3DDevice9_GetBackBuffer, 20},
    {"GetRasterStatus", d9_IDirect3DDevice9_GetRasterStatus, 12},
    {"SetDialogBoxMode", d9_IDirect3DDevice9_SetDialogBoxMode, 8},
    {"SetGammaRamp", d9_IDirect3DDevice9_SetGammaRamp, 16},
    {"GetGammaRamp", d9_IDirect3DDevice9_GetGammaRamp, 12},
    {"CreateTexture", d9_IDirect3DDevice9_CreateTexture, 36},
    {"CreateVolumeTexture", d9_IDirect3DDevice9_CreateVolumeTexture, 40},
    {"CreateCubeTexture", d9_IDirect3DDevice9_CreateCubeTexture, 32},
    {"CreateVertexBuffer", d9_IDirect3DDevice9_CreateVertexBuffer, 28},
    {"CreateIndexBuffer", d9_IDirect3DDevice9_CreateIndexBuffer, 28},
    {"CreateRenderTarget", d9_IDirect3DDevice9_CreateRenderTarget, 36},
    {"CreateDepthStencilSurface", d9_IDirect3DDevice9_CreateDepthStencilSurface, 36},
    {"UpdateSurface", d9_IDirect3DDevice9_UpdateSurface, 20},
    {"UpdateTexture", d9_IDirect3DDevice9_UpdateTexture, 12},
    {"GetRenderTargetData", d9_IDirect3DDevice9_GetRenderTargetData, 12},
    {"GetFrontBufferData", d9_IDirect3DDevice9_GetFrontBufferData, 12},
    {"StretchRect", d9_IDirect3DDevice9_StretchRect, 24},
    {"ColorFill", d9_IDirect3DDevice9_ColorFill, 16},
    {"CreateOffscreenPlainSurface", d9_IDirect3DDevice9_CreateOffscreenPlainSurface, 28},
    {"SetRenderTarget", d9_IDirect3DDevice9_SetRenderTarget, 12},
    {"GetRenderTarget", d9_IDirect3DDevice9_GetRenderTarget, 12},
    {"SetDepthStencilSurface", d9_IDirect3DDevice9_SetDepthStencilSurface, 8},
    {"GetDepthStencilSurface", d9_IDirect3DDevice9_GetDepthStencilSurface, 8},
    {"BeginScene", d9_IDirect3DDevice9_BeginScene, 4},
    {"EndScene", d9_IDirect3DDevice9_EndScene, 4},
    {"Clear", d9_IDirect3DDevice9_Clear, 28},
    {"SetTransform", d9_IDirect3DDevice9_SetTransform, 12},
    {"GetTransform", d9_IDirect3DDevice9_GetTransform, 12},
    {"MultiplyTransform", d9_IDirect3DDevice9_MultiplyTransform, 12},
    {"SetViewport", d9_IDirect3DDevice9_SetViewport, 8},
    {"GetViewport", d9_IDirect3DDevice9_GetViewport, 8},
    {"SetMaterial", d9_IDirect3DDevice9_SetMaterial, 8},
    {"GetMaterial", d9_IDirect3DDevice9_GetMaterial, 8},
    {"SetLight", d9_IDirect3DDevice9_SetLight, 12},
    {"GetLight", d9_IDirect3DDevice9_GetLight, 12},
    {"LightEnable", d9_IDirect3DDevice9_LightEnable, 12},
    {"GetLightEnable", d9_IDirect3DDevice9_GetLightEnable, 12},
    {"SetClipPlane", d9_IDirect3DDevice9_SetClipPlane, 12},
    {"GetClipPlane", d9_IDirect3DDevice9_GetClipPlane, 12},
    {"SetRenderState", d9_IDirect3DDevice9_SetRenderState, 12},
    {"GetRenderState", d9_IDirect3DDevice9_GetRenderState, 12},
    {"CreateStateBlock", d9_IDirect3DDevice9_CreateStateBlock, 12},
    {"BeginStateBlock", d9_IDirect3DDevice9_BeginStateBlock, 4},
    {"EndStateBlock", d9_IDirect3DDevice9_EndStateBlock, 8},
    {"SetClipStatus", d9_IDirect3DDevice9_SetClipStatus, 8},
    {"GetClipStatus", d9_IDirect3DDevice9_GetClipStatus, 8},
    {"GetTexture", d9_IDirect3DDevice9_GetTexture, 12},
    {"SetTexture", d9_IDirect3DDevice9_SetTexture, 12},
    {"GetTextureStageState", d9_IDirect3DDevice9_GetTextureStageState, 16},
    {"SetTextureStageState", d9_IDirect3DDevice9_SetTextureStageState, 16},
    {"GetSamplerState", d9_IDirect3DDevice9_GetSamplerState, 16},
    {"SetSamplerState", d9_IDirect3DDevice9_SetSamplerState, 16},
    {"ValidateDevice", d9_IDirect3DDevice9_ValidateDevice, 8},
    {"SetPaletteEntries", d9_IDirect3DDevice9_SetPaletteEntries, 12},
    {"GetPaletteEntries", d9_IDirect3DDevice9_GetPaletteEntries, 12},
    {"SetCurrentTexturePalette", d9_IDirect3DDevice9_SetCurrentTexturePalette, 8},
    {"GetCurrentTexturePalette", d9_IDirect3DDevice9_GetCurrentTexturePalette, 8},
    {"SetScissorRect", d9_IDirect3DDevice9_SetScissorRect, 8},
    {"GetScissorRect", d9_IDirect3DDevice9_GetScissorRect, 8},
    {"SetSoftwareVertexProcessing", d9_IDirect3DDevice9_SetSoftwareVertexProcessing, 8},
    {"GetSoftwareVertexProcessing", d9_IDirect3DDevice9_GetSoftwareVertexProcessing, 4},
    {"SetNPatchMode", d9_IDirect3DDevice9_SetNPatchMode, 8},
    {"GetNPatchMode", d9_IDirect3DDevice9_GetNPatchMode, 4},
    {"DrawPrimitive", d9_IDirect3DDevice9_DrawPrimitive, 16},
    {"DrawIndexedPrimitive", d9_IDirect3DDevice9_DrawIndexedPrimitive, 28},
    {"DrawPrimitiveUP", d9_IDirect3DDevice9_DrawPrimitiveUP, 20},
    {"DrawIndexedPrimitiveUP", d9_IDirect3DDevice9_DrawIndexedPrimitiveUP, 36},
    {"ProcessVertices", d9_IDirect3DDevice9_ProcessVertices, 28},
    {"CreateVertexDeclaration", d9_IDirect3DDevice9_CreateVertexDeclaration, 12},
    {"SetVertexDeclaration", d9_IDirect3DDevice9_SetVertexDeclaration, 8},
    {"GetVertexDeclaration", d9_IDirect3DDevice9_GetVertexDeclaration, 8},
    {"SetFVF", d9_IDirect3DDevice9_SetFVF, 8},
    {"GetFVF", d9_IDirect3DDevice9_GetFVF, 8},
    {"CreateVertexShader", d9_IDirect3DDevice9_CreateVertexShader, 12},
    {"SetVertexShader", d9_IDirect3DDevice9_SetVertexShader, 8},
    {"GetVertexShader", d9_IDirect3DDevice9_GetVertexShader, 8},
    {"SetVertexShaderConstantF", d9_IDirect3DDevice9_SetVertexShaderConstantF, 16},
    {"GetVertexShaderConstantF", d9_IDirect3DDevice9_GetVertexShaderConstantF, 16},
    {"SetVertexShaderConstantI", d9_IDirect3DDevice9_SetVertexShaderConstantI, 16},
    {"GetVertexShaderConstantI", d9_IDirect3DDevice9_GetVertexShaderConstantI, 16},
    {"SetVertexShaderConstantB", d9_IDirect3DDevice9_SetVertexShaderConstantB, 16},
    {"GetVertexShaderConstantB", d9_IDirect3DDevice9_GetVertexShaderConstantB, 16},
    {"SetStreamSource", d9_IDirect3DDevice9_SetStreamSource, 20},
    {"GetStreamSource", d9_IDirect3DDevice9_GetStreamSource, 20},
    {"SetStreamSourceFreq", d9_IDirect3DDevice9_SetStreamSourceFreq, 12},
    {"GetStreamSourceFreq", d9_IDirect3DDevice9_GetStreamSourceFreq, 12},
    {"SetIndices", d9_IDirect3DDevice9_SetIndices, 8},
    {"GetIndices", d9_IDirect3DDevice9_GetIndices, 8},
    {"CreatePixelShader", d9_IDirect3DDevice9_CreatePixelShader, 12},
    {"SetPixelShader", d9_IDirect3DDevice9_SetPixelShader, 8},
    {"GetPixelShader", d9_IDirect3DDevice9_GetPixelShader, 8},
    {"SetPixelShaderConstantF", d9_IDirect3DDevice9_SetPixelShaderConstantF, 16},
    {"GetPixelShaderConstantF", d9_IDirect3DDevice9_GetPixelShaderConstantF, 16},
    {"SetPixelShaderConstantI", d9_IDirect3DDevice9_SetPixelShaderConstantI, 16},
    {"GetPixelShaderConstantI", d9_IDirect3DDevice9_GetPixelShaderConstantI, 16},
    {"SetPixelShaderConstantB", d9_IDirect3DDevice9_SetPixelShaderConstantB, 16},
    {"GetPixelShaderConstantB", d9_IDirect3DDevice9_GetPixelShaderConstantB, 16},
    {"DrawRectPatch", d9_IDirect3DDevice9_DrawRectPatch, 16},
    {"DrawTriPatch", d9_IDirect3DDevice9_DrawTriPatch, 16},
    {"DeletePatch", d9_IDirect3DDevice9_DeletePatch, 8},
    {"CreateQuery", d9_IDirect3DDevice9_CreateQuery, 12},
};
static const D9Method d9_methods_IDirect3DSwapChain9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"Present", d9_special_IDirect3DSwapChain9_Present, 24},
    {"GetFrontBufferData", d9_IDirect3DSwapChain9_GetFrontBufferData, 8},
    {"GetBackBuffer", d9_IDirect3DSwapChain9_GetBackBuffer, 16},
    {"GetRasterStatus", d9_IDirect3DSwapChain9_GetRasterStatus, 8},
    {"GetDisplayMode", d9_IDirect3DSwapChain9_GetDisplayMode, 8},
    {"GetDevice", d9_IDirect3DSwapChain9_GetDevice, 8},
    {"GetPresentParameters", d9_special_IDirect3DSwapChain9_GetPresentParameters, 8},
};
static const D9Method d9_methods_IDirect3DTexture9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DResource9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DResource9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DResource9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DResource9_FreePrivateData, 8},
    {"SetPriority", d9_IDirect3DResource9_SetPriority, 8},
    {"GetPriority", d9_IDirect3DResource9_GetPriority, 4},
    {"PreLoad", d9_IDirect3DResource9_PreLoad, 4},
    {"GetType", d9_IDirect3DResource9_GetType, 4},
    {"SetLOD", d9_IDirect3DBaseTexture9_SetLOD, 8},
    {"GetLOD", d9_IDirect3DBaseTexture9_GetLOD, 4},
    {"GetLevelCount", d9_IDirect3DBaseTexture9_GetLevelCount, 4},
    {"SetAutoGenFilterType", d9_IDirect3DBaseTexture9_SetAutoGenFilterType, 8},
    {"GetAutoGenFilterType", d9_IDirect3DBaseTexture9_GetAutoGenFilterType, 4},
    {"GenerateMipSubLevels", d9_IDirect3DBaseTexture9_GenerateMipSubLevels, 4},
    {"GetLevelDesc", d9_IDirect3DTexture9_GetLevelDesc, 12},
    {"GetSurfaceLevel", d9_IDirect3DTexture9_GetSurfaceLevel, 12},
    {"LockRect", d9_special_IDirect3DTexture9_LockRect, 20},
    {"UnlockRect", d9_special_IDirect3DTexture9_UnlockRect, 8},
    {"AddDirtyRect", d9_IDirect3DTexture9_AddDirtyRect, 8},
};
static const D9Method d9_methods_IDirect3DVolumeTexture9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DResource9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DResource9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DResource9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DResource9_FreePrivateData, 8},
    {"SetPriority", d9_IDirect3DResource9_SetPriority, 8},
    {"GetPriority", d9_IDirect3DResource9_GetPriority, 4},
    {"PreLoad", d9_IDirect3DResource9_PreLoad, 4},
    {"GetType", d9_IDirect3DResource9_GetType, 4},
    {"SetLOD", d9_IDirect3DBaseTexture9_SetLOD, 8},
    {"GetLOD", d9_IDirect3DBaseTexture9_GetLOD, 4},
    {"GetLevelCount", d9_IDirect3DBaseTexture9_GetLevelCount, 4},
    {"SetAutoGenFilterType", d9_IDirect3DBaseTexture9_SetAutoGenFilterType, 8},
    {"GetAutoGenFilterType", d9_IDirect3DBaseTexture9_GetAutoGenFilterType, 4},
    {"GenerateMipSubLevels", d9_IDirect3DBaseTexture9_GenerateMipSubLevels, 4},
    {"GetLevelDesc", d9_IDirect3DVolumeTexture9_GetLevelDesc, 12},
    {"GetVolumeLevel", d9_IDirect3DVolumeTexture9_GetVolumeLevel, 12},
    {"LockBox", d9_special_IDirect3DVolumeTexture9_LockBox, 20},
    {"UnlockBox", d9_special_IDirect3DVolumeTexture9_UnlockBox, 8},
    {"AddDirtyBox", d9_IDirect3DVolumeTexture9_AddDirtyBox, 8},
};
static const D9Method d9_methods_IDirect3DCubeTexture9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DResource9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DResource9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DResource9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DResource9_FreePrivateData, 8},
    {"SetPriority", d9_IDirect3DResource9_SetPriority, 8},
    {"GetPriority", d9_IDirect3DResource9_GetPriority, 4},
    {"PreLoad", d9_IDirect3DResource9_PreLoad, 4},
    {"GetType", d9_IDirect3DResource9_GetType, 4},
    {"SetLOD", d9_IDirect3DBaseTexture9_SetLOD, 8},
    {"GetLOD", d9_IDirect3DBaseTexture9_GetLOD, 4},
    {"GetLevelCount", d9_IDirect3DBaseTexture9_GetLevelCount, 4},
    {"SetAutoGenFilterType", d9_IDirect3DBaseTexture9_SetAutoGenFilterType, 8},
    {"GetAutoGenFilterType", d9_IDirect3DBaseTexture9_GetAutoGenFilterType, 4},
    {"GenerateMipSubLevels", d9_IDirect3DBaseTexture9_GenerateMipSubLevels, 4},
    {"GetLevelDesc", d9_IDirect3DCubeTexture9_GetLevelDesc, 12},
    {"GetCubeMapSurface", d9_IDirect3DCubeTexture9_GetCubeMapSurface, 16},
    {"LockRect", d9_special_IDirect3DCubeTexture9_LockRect, 24},
    {"UnlockRect", d9_special_IDirect3DCubeTexture9_UnlockRect, 12},
    {"AddDirtyRect", d9_IDirect3DCubeTexture9_AddDirtyRect, 12},
};
static const D9Method d9_methods_IDirect3DVertexBuffer9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DResource9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DResource9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DResource9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DResource9_FreePrivateData, 8},
    {"SetPriority", d9_IDirect3DResource9_SetPriority, 8},
    {"GetPriority", d9_IDirect3DResource9_GetPriority, 4},
    {"PreLoad", d9_IDirect3DResource9_PreLoad, 4},
    {"GetType", d9_IDirect3DResource9_GetType, 4},
    {"Lock", d9_special_IDirect3DVertexBuffer9_Lock, 20},
    {"Unlock", d9_special_IDirect3DVertexBuffer9_Unlock, 4},
    {"GetDesc", d9_IDirect3DVertexBuffer9_GetDesc, 8},
};
static const D9Method d9_methods_IDirect3DIndexBuffer9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DResource9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DResource9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DResource9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DResource9_FreePrivateData, 8},
    {"SetPriority", d9_IDirect3DResource9_SetPriority, 8},
    {"GetPriority", d9_IDirect3DResource9_GetPriority, 4},
    {"PreLoad", d9_IDirect3DResource9_PreLoad, 4},
    {"GetType", d9_IDirect3DResource9_GetType, 4},
    {"Lock", d9_special_IDirect3DIndexBuffer9_Lock, 20},
    {"Unlock", d9_special_IDirect3DIndexBuffer9_Unlock, 4},
    {"GetDesc", d9_IDirect3DIndexBuffer9_GetDesc, 8},
};
static const D9Method d9_methods_IDirect3DSurface9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DResource9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DResource9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DResource9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DResource9_FreePrivateData, 8},
    {"SetPriority", d9_IDirect3DResource9_SetPriority, 8},
    {"GetPriority", d9_IDirect3DResource9_GetPriority, 4},
    {"PreLoad", d9_IDirect3DResource9_PreLoad, 4},
    {"GetType", d9_IDirect3DResource9_GetType, 4},
    {"GetContainer", d9_IDirect3DSurface9_GetContainer, 12},
    {"GetDesc", d9_IDirect3DSurface9_GetDesc, 8},
    {"LockRect", d9_special_IDirect3DSurface9_LockRect, 16},
    {"UnlockRect", d9_special_IDirect3DSurface9_UnlockRect, 4},
    {"GetDC", d9_IDirect3DSurface9_GetDC, 8},
    {"ReleaseDC", d9_IDirect3DSurface9_ReleaseDC, 8},
};
static const D9Method d9_methods_IDirect3DVolume9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DVolume9_GetDevice, 8},
    {"SetPrivateData", d9_IDirect3DVolume9_SetPrivateData, 20},
    {"GetPrivateData", d9_IDirect3DVolume9_GetPrivateData, 16},
    {"FreePrivateData", d9_IDirect3DVolume9_FreePrivateData, 8},
    {"GetContainer", d9_IDirect3DVolume9_GetContainer, 12},
    {"GetDesc", d9_IDirect3DVolume9_GetDesc, 8},
    {"LockBox", d9_special_IDirect3DVolume9_LockBox, 16},
    {"UnlockBox", d9_special_IDirect3DVolume9_UnlockBox, 4},
};
static const D9Method d9_methods_IDirect3DVertexDeclaration9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DVertexDeclaration9_GetDevice, 8},
    {"GetDeclaration", d9_IDirect3DVertexDeclaration9_GetDeclaration, 12},
};
static const D9Method d9_methods_IDirect3DVertexShader9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DVertexShader9_GetDevice, 8},
    {"GetFunction", d9_IDirect3DVertexShader9_GetFunction, 12},
};
static const D9Method d9_methods_IDirect3DPixelShader9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DPixelShader9_GetDevice, 8},
    {"GetFunction", d9_IDirect3DPixelShader9_GetFunction, 12},
};
static const D9Method d9_methods_IDirect3DStateBlock9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DStateBlock9_GetDevice, 8},
    {"Capture", d9_IDirect3DStateBlock9_Capture, 4},
    {"Apply", d9_IDirect3DStateBlock9_Apply, 4},
};
static const D9Method d9_methods_IDirect3DQuery9[] = {
    {"QueryInterface", d9_special_QueryInterface, 12},
    {"AddRef", d9_special_AddRef, 4},
    {"Release", d9_special_Release, 4},
    {"GetDevice", d9_IDirect3DQuery9_GetDevice, 8},
    {"GetType", d9_IDirect3DQuery9_GetType, 4},
    {"GetDataSize", d9_IDirect3DQuery9_GetDataSize, 4},
    {"Issue", d9_IDirect3DQuery9_Issue, 8},
    {"GetData", d9_IDirect3DQuery9_GetData, 16},
};
static const D9Method *const d9_family_methods[D9_FAMILY_COUNT] = {d9_methods_IDirect3D9, d9_methods_IDirect3DDevice9, d9_methods_IDirect3DSwapChain9, d9_methods_IDirect3DTexture9, d9_methods_IDirect3DVolumeTexture9, d9_methods_IDirect3DCubeTexture9, d9_methods_IDirect3DVertexBuffer9, d9_methods_IDirect3DIndexBuffer9, d9_methods_IDirect3DSurface9, d9_methods_IDirect3DVolume9, d9_methods_IDirect3DVertexDeclaration9, d9_methods_IDirect3DVertexShader9, d9_methods_IDirect3DPixelShader9, d9_methods_IDirect3DStateBlock9, d9_methods_IDirect3DQuery9};
static const u32 d9_family_method_count[D9_FAMILY_COUNT] = {sizeof d9_methods_IDirect3D9 / sizeof d9_methods_IDirect3D9[0], sizeof d9_methods_IDirect3DDevice9 / sizeof d9_methods_IDirect3DDevice9[0], sizeof d9_methods_IDirect3DSwapChain9 / sizeof d9_methods_IDirect3DSwapChain9[0], sizeof d9_methods_IDirect3DTexture9 / sizeof d9_methods_IDirect3DTexture9[0], sizeof d9_methods_IDirect3DVolumeTexture9 / sizeof d9_methods_IDirect3DVolumeTexture9[0], sizeof d9_methods_IDirect3DCubeTexture9 / sizeof d9_methods_IDirect3DCubeTexture9[0], sizeof d9_methods_IDirect3DVertexBuffer9 / sizeof d9_methods_IDirect3DVertexBuffer9[0], sizeof d9_methods_IDirect3DIndexBuffer9 / sizeof d9_methods_IDirect3DIndexBuffer9[0], sizeof d9_methods_IDirect3DSurface9 / sizeof d9_methods_IDirect3DSurface9[0], sizeof d9_methods_IDirect3DVolume9 / sizeof d9_methods_IDirect3DVolume9[0], sizeof d9_methods_IDirect3DVertexDeclaration9 / sizeof d9_methods_IDirect3DVertexDeclaration9[0], sizeof d9_methods_IDirect3DVertexShader9 / sizeof d9_methods_IDirect3DVertexShader9[0], sizeof d9_methods_IDirect3DPixelShader9 / sizeof d9_methods_IDirect3DPixelShader9[0], sizeof d9_methods_IDirect3DStateBlock9 / sizeof d9_methods_IDirect3DStateBlock9[0], sizeof d9_methods_IDirect3DQuery9 / sizeof d9_methods_IDirect3DQuery9[0]};
