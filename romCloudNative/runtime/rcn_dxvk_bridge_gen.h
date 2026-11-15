// Généré par scripts/gen_dxvk_bridge.py depuis les en-têtes de DXVK : ne pas modifier.
// Inclus par rcn_dxvk_bridge.c (qui fournit br_unwrap, br_wrap, br_out, br_family_of_iid…).

enum { BR_FAMILY_COUNT = 38 };
static const IID *const br_family_iid[BR_FAMILY_COUNT] = {&IID_IDXGIFactory7, &IID_IDXGIAdapter4, &IID_IDXGIOutput6, &IID_IDXGISwapChain4, &IID_IDXGIDevice4, &IID_IDXGISurface2, &IID_IDXGIResource1, &IID_IDXGIKeyedMutex, &IID_ID3D11Device5, &IID_ID3D11DeviceContext4, &IID_ID3D11Buffer, &IID_ID3D11Texture1D, &IID_ID3D11Texture2D1, &IID_ID3D11Texture3D1, &IID_ID3D11ShaderResourceView1, &IID_ID3D11RenderTargetView1, &IID_ID3D11DepthStencilView, &IID_ID3D11UnorderedAccessView1, &IID_ID3D11VertexShader, &IID_ID3D11PixelShader, &IID_ID3D11GeometryShader, &IID_ID3D11HullShader, &IID_ID3D11DomainShader, &IID_ID3D11ComputeShader, &IID_ID3D11InputLayout, &IID_ID3D11BlendState1, &IID_ID3D11DepthStencilState, &IID_ID3D11RasterizerState2, &IID_ID3D11SamplerState, &IID_ID3D11Query1, &IID_ID3D11Predicate, &IID_ID3D11Counter, &IID_ID3D11CommandList, &IID_ID3D11ClassLinkage, &IID_ID3D11ClassInstance, &IID_ID3D11Fence, &IID_ID3D11Multithread, &IID_ID3DUserDefinedAnnotation};
static const char *const br_family_name[BR_FAMILY_COUNT] = {"IDXGIFactory7", "IDXGIAdapter4", "IDXGIOutput6", "IDXGISwapChain4", "IDXGIDevice4", "IDXGISurface2", "IDXGIResource1", "IDXGIKeyedMutex", "ID3D11Device5", "ID3D11DeviceContext4", "ID3D11Buffer", "ID3D11Texture1D", "ID3D11Texture2D1", "ID3D11Texture3D1", "ID3D11ShaderResourceView1", "ID3D11RenderTargetView1", "ID3D11DepthStencilView", "ID3D11UnorderedAccessView1", "ID3D11VertexShader", "ID3D11PixelShader", "ID3D11GeometryShader", "ID3D11HullShader", "ID3D11DomainShader", "ID3D11ComputeShader", "ID3D11InputLayout", "ID3D11BlendState1", "ID3D11DepthStencilState", "ID3D11RasterizerState2", "ID3D11SamplerState", "ID3D11Query1", "ID3D11Predicate", "ID3D11Counter", "ID3D11CommandList", "ID3D11ClassLinkage", "ID3D11ClassInstance", "ID3D11Fence", "ID3D11Multithread", "ID3DUserDefinedAnnotation"};
static const struct { const IID *iid; int family; } br_iid_family[] = {
    {&IID_ID3D11Asynchronous, 29},
    {&IID_ID3D11BlendState, 25},
    {&IID_ID3D11BlendState1, 25},
    {&IID_ID3D11Buffer, 10},
    {&IID_ID3D11ClassInstance, 34},
    {&IID_ID3D11ClassLinkage, 33},
    {&IID_ID3D11CommandList, 32},
    {&IID_ID3D11ComputeShader, 23},
    {&IID_ID3D11Counter, 31},
    {&IID_ID3D11DepthStencilState, 26},
    {&IID_ID3D11DepthStencilView, 16},
    {&IID_ID3D11Device, 8},
    {&IID_ID3D11Device1, 8},
    {&IID_ID3D11Device2, 8},
    {&IID_ID3D11Device3, 8},
    {&IID_ID3D11Device4, 8},
    {&IID_ID3D11Device5, 8},
    {&IID_ID3D11DeviceChild, 9},
    {&IID_ID3D11DeviceContext, 9},
    {&IID_ID3D11DeviceContext1, 9},
    {&IID_ID3D11DeviceContext2, 9},
    {&IID_ID3D11DeviceContext3, 9},
    {&IID_ID3D11DeviceContext4, 9},
    {&IID_ID3D11DomainShader, 22},
    {&IID_ID3D11Fence, 35},
    {&IID_ID3D11GeometryShader, 20},
    {&IID_ID3D11HullShader, 21},
    {&IID_ID3D11InputLayout, 24},
    {&IID_ID3D11Multithread, 36},
    {&IID_ID3D11PixelShader, 19},
    {&IID_ID3D11Predicate, 30},
    {&IID_ID3D11Query, 29},
    {&IID_ID3D11Query1, 29},
    {&IID_ID3D11RasterizerState, 27},
    {&IID_ID3D11RasterizerState1, 27},
    {&IID_ID3D11RasterizerState2, 27},
    {&IID_ID3D11RenderTargetView, 15},
    {&IID_ID3D11RenderTargetView1, 15},
    {&IID_ID3D11Resource, 10},
    {&IID_ID3D11SamplerState, 28},
    {&IID_ID3D11ShaderResourceView, 14},
    {&IID_ID3D11ShaderResourceView1, 14},
    {&IID_ID3D11Texture1D, 11},
    {&IID_ID3D11Texture2D, 12},
    {&IID_ID3D11Texture2D1, 12},
    {&IID_ID3D11Texture3D, 13},
    {&IID_ID3D11Texture3D1, 13},
    {&IID_ID3D11UnorderedAccessView, 17},
    {&IID_ID3D11UnorderedAccessView1, 17},
    {&IID_ID3D11VertexShader, 18},
    {&IID_ID3D11View, 14},
    {&IID_ID3DUserDefinedAnnotation, 37},
    {&IID_IDXGIAdapter, 1},
    {&IID_IDXGIAdapter1, 1},
    {&IID_IDXGIAdapter2, 1},
    {&IID_IDXGIAdapter3, 1},
    {&IID_IDXGIAdapter4, 1},
    {&IID_IDXGIDevice, 4},
    {&IID_IDXGIDevice1, 4},
    {&IID_IDXGIDevice2, 4},
    {&IID_IDXGIDevice3, 4},
    {&IID_IDXGIDevice4, 4},
    {&IID_IDXGIDeviceSubObject, 3},
    {&IID_IDXGIFactory, 0},
    {&IID_IDXGIFactory1, 0},
    {&IID_IDXGIFactory2, 0},
    {&IID_IDXGIFactory3, 0},
    {&IID_IDXGIFactory4, 0},
    {&IID_IDXGIFactory5, 0},
    {&IID_IDXGIFactory6, 0},
    {&IID_IDXGIFactory7, 0},
    {&IID_IDXGIKeyedMutex, 7},
    {&IID_IDXGIObject, 0},
    {&IID_IDXGIOutput, 2},
    {&IID_IDXGIOutput1, 2},
    {&IID_IDXGIOutput2, 2},
    {&IID_IDXGIOutput3, 2},
    {&IID_IDXGIOutput4, 2},
    {&IID_IDXGIOutput5, 2},
    {&IID_IDXGIOutput6, 2},
    {&IID_IDXGIResource, 6},
    {&IID_IDXGIResource1, 6},
    {&IID_IDXGISurface, 5},
    {&IID_IDXGISurface1, 5},
    {&IID_IDXGISurface2, 5},
    {&IID_IDXGISwapChain, 3},
    {&IID_IDXGISwapChain1, 3},
    {&IID_IDXGISwapChain2, 3},
    {&IID_IDXGISwapChain3, 3},
    {&IID_IDXGISwapChain4, 3},
};

static void br_IDXGIObject_SetPrivateData(RcnCtx *ctx) {
    IDXGIObject *self = (IDXGIObject *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const void * a3 = (const void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIObject::SetPrivateData", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIObject_SetPrivateDataInterface(RcnCtx *ctx) {
    IDXGIObject *self = (IDXGIObject *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    const IUnknown * a2 = (const IUnknown *)br_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->SetPrivateDataInterface(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIObject::SetPrivateDataInterface", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIObject_GetPrivateData(RcnCtx *ctx) {
    IDXGIObject *self = (IDXGIObject *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    void * a3 = (void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIObject::GetPrivateData", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIObject_GetParent(RcnCtx *ctx) {
    IDXGIObject *self = (IDXGIObject *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    HRESULT r = self->lpVtbl->GetParent(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out_iid(ctx, g2, t2, (const IID *)(uintptr_t)rcn_arg(ctx, 1), &r);
    if (FAILED(r)) br_failed("IDXGIObject::GetParent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory_EnumAdapters(RcnCtx *ctx) {
    IDXGIFactory *self = (IDXGIFactory *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDXGIAdapter *t2 = NULL;
    HRESULT r = self->lpVtbl->EnumAdapters(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 1);
    if (FAILED(r)) br_failed("IDXGIFactory::EnumAdapters", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory_MakeWindowAssociation(RcnCtx *ctx) {
    IDXGIFactory *self = (IDXGIFactory *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HWND a1 = (HWND)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->MakeWindowAssociation(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIFactory::MakeWindowAssociation", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory_GetWindowAssociation(RcnCtx *ctx) {
    IDXGIFactory *self = (IDXGIFactory *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HWND * a1 = (HWND *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetWindowAssociation(self, a1);
    if (FAILED(r)) br_failed("IDXGIFactory::GetWindowAssociation", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory_CreateSwapChain(RcnCtx *ctx) {
    IDXGIFactory *self = (IDXGIFactory *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    DXGI_SWAP_CHAIN_DESC * a2 = (DXGI_SWAP_CHAIN_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); IDXGISwapChain *t3 = NULL;
    br_before_present(ctx); // fenêtre d'Android donnée à DXVK avant sa surface
    HRESULT r = self->lpVtbl->CreateSwapChain(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 3);
    if (FAILED(r)) br_failed("IDXGIFactory::CreateSwapChain", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory_CreateSoftwareAdapter(RcnCtx *ctx) {
    IDXGIFactory *self = (IDXGIFactory *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HMODULE a1 = (HMODULE)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDXGIAdapter *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateSoftwareAdapter(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 1);
    if (FAILED(r)) br_failed("IDXGIFactory::CreateSoftwareAdapter", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory1_EnumAdapters1(RcnCtx *ctx) {
    IDXGIFactory1 *self = (IDXGIFactory1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDXGIAdapter1 *t2 = NULL;
    HRESULT r = self->lpVtbl->EnumAdapters1(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 1);
    if (FAILED(r)) br_failed("IDXGIFactory1::EnumAdapters1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory1_IsCurrent(RcnCtx *ctx) {
    IDXGIFactory1 *self = (IDXGIFactory1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->IsCurrent(self);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_IsWindowedStereoEnabled(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->IsWindowedStereoEnabled(self);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_CreateSwapChainForHwnd(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    HWND a2 = (HWND)(uintptr_t)rcn_arg(ctx, 2);
    const DXGI_SWAP_CHAIN_DESC1 * a3 = (const DXGI_SWAP_CHAIN_DESC1 *)(uintptr_t)rcn_arg(ctx, 3);
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC * a4 = (const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *)(uintptr_t)rcn_arg(ctx, 4);
    IDXGIOutput * a5 = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 5));
    u64 g6 = rcn_arg(ctx, 6); IDXGISwapChain1 *t6 = NULL;
    br_before_present(ctx); // fenêtre d'Android donnée à DXVK avant sa surface
    HRESULT r = self->lpVtbl->CreateSwapChainForHwnd(self, a1, a2, a3, a4, a5, g6 ? &t6 : NULL);
    if (g6) br_out(ctx, g6, t6, 3);
    if (FAILED(r)) br_failed("IDXGIFactory2::CreateSwapChainForHwnd", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_CreateSwapChainForCoreWindow(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    IUnknown * a2 = (IUnknown *)br_unwrap(rcn_arg(ctx, 2));
    const DXGI_SWAP_CHAIN_DESC1 * a3 = (const DXGI_SWAP_CHAIN_DESC1 *)(uintptr_t)rcn_arg(ctx, 3);
    IDXGIOutput * a4 = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 4));
    u64 g5 = rcn_arg(ctx, 5); IDXGISwapChain1 *t5 = NULL;
    br_before_present(ctx); // fenêtre d'Android donnée à DXVK avant sa surface
    HRESULT r = self->lpVtbl->CreateSwapChainForCoreWindow(self, a1, a2, a3, a4, g5 ? &t5 : NULL);
    if (g5) br_out(ctx, g5, t5, 3);
    if (FAILED(r)) br_failed("IDXGIFactory2::CreateSwapChainForCoreWindow", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_GetSharedResourceAdapterLuid(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    LUID * a2 = (LUID *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetSharedResourceAdapterLuid(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIFactory2::GetSharedResourceAdapterLuid", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_RegisterStereoStatusWindow(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HWND a1 = (HWND)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    DWORD * a3 = (DWORD *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->RegisterStereoStatusWindow(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIFactory2::RegisterStereoStatusWindow", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_RegisterStereoStatusEvent(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->RegisterStereoStatusEvent(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIFactory2::RegisterStereoStatusEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_UnregisterStereoStatus(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DWORD a1 = (DWORD)rcn_arg(ctx, 1);
    self->lpVtbl->UnregisterStereoStatus(self, a1);
}

static void br_IDXGIFactory2_RegisterOcclusionStatusWindow(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HWND a1 = (HWND)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    DWORD * a3 = (DWORD *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->RegisterOcclusionStatusWindow(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIFactory2::RegisterOcclusionStatusWindow", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_RegisterOcclusionStatusEvent(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->RegisterOcclusionStatusEvent(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIFactory2::RegisterOcclusionStatusEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory2_UnregisterOcclusionStatus(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DWORD a1 = (DWORD)rcn_arg(ctx, 1);
    self->lpVtbl->UnregisterOcclusionStatus(self, a1);
}

static void br_IDXGIFactory2_CreateSwapChainForComposition(RcnCtx *ctx) {
    IDXGIFactory2 *self = (IDXGIFactory2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    const DXGI_SWAP_CHAIN_DESC1 * a2 = (const DXGI_SWAP_CHAIN_DESC1 *)(uintptr_t)rcn_arg(ctx, 2);
    IDXGIOutput * a3 = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); IDXGISwapChain1 *t4 = NULL;
    br_before_present(ctx); // fenêtre d'Android donnée à DXVK avant sa surface
    HRESULT r = self->lpVtbl->CreateSwapChainForComposition(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 3);
    if (FAILED(r)) br_failed("IDXGIFactory2::CreateSwapChainForComposition", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory3_GetCreationFlags(RcnCtx *ctx) {
    IDXGIFactory3 *self = (IDXGIFactory3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetCreationFlags(self);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory4_EnumAdapterByLuid(RcnCtx *ctx) {
    IDXGIFactory4 *self = (IDXGIFactory4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    LUID a1; u64 v1 = rcn_arg(ctx, 1); memcpy(&a1, &v1, 8);
    REFIID a2 = (REFIID)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); void *t3 = NULL;
    HRESULT r = self->lpVtbl->EnumAdapterByLuid(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out_iid(ctx, g3, t3, (const IID *)(uintptr_t)rcn_arg(ctx, 2), &r);
    if (FAILED(r)) br_failed("IDXGIFactory4::EnumAdapterByLuid", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory4_EnumWarpAdapter(RcnCtx *ctx) {
    IDXGIFactory4 *self = (IDXGIFactory4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    HRESULT r = self->lpVtbl->EnumWarpAdapter(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out_iid(ctx, g2, t2, (const IID *)(uintptr_t)rcn_arg(ctx, 1), &r);
    if (FAILED(r)) br_failed("IDXGIFactory4::EnumWarpAdapter", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory5_CheckFeatureSupport(RcnCtx *ctx) {
    IDXGIFactory5 *self = (IDXGIFactory5 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FEATURE a1 = (DXGI_FEATURE)rcn_arg(ctx, 1);
    void * a2 = (void *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->CheckFeatureSupport(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIFactory5::CheckFeatureSupport", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory6_EnumAdapterByGpuPreference(RcnCtx *ctx) {
    IDXGIFactory6 *self = (IDXGIFactory6 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    DXGI_GPU_PREFERENCE a2 = (DXGI_GPU_PREFERENCE)rcn_arg(ctx, 2);
    REFIID a3 = (REFIID)(uintptr_t)rcn_arg(ctx, 3);
    u64 g4 = rcn_arg(ctx, 4); void *t4 = NULL;
    HRESULT r = self->lpVtbl->EnumAdapterByGpuPreference(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out_iid(ctx, g4, t4, (const IID *)(uintptr_t)rcn_arg(ctx, 3), &r);
    if (FAILED(r)) br_failed("IDXGIFactory6::EnumAdapterByGpuPreference", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory7_RegisterAdaptersChangedEvent(RcnCtx *ctx) {
    IDXGIFactory7 *self = (IDXGIFactory7 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->RegisterAdaptersChangedEvent(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIFactory7::RegisterAdaptersChangedEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIFactory7_UnregisterAdaptersChangedEvent(RcnCtx *ctx) {
    IDXGIFactory7 *self = (IDXGIFactory7 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DWORD a1 = (DWORD)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->UnregisterAdaptersChangedEvent(self, a1);
    if (FAILED(r)) br_failed("IDXGIFactory7::UnregisterAdaptersChangedEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter_EnumOutputs(RcnCtx *ctx) {
    IDXGIAdapter *self = (IDXGIAdapter *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDXGIOutput *t2 = NULL;
    HRESULT r = self->lpVtbl->EnumOutputs(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 2);
    if (FAILED(r)) br_failed("IDXGIAdapter::EnumOutputs", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter_CheckInterfaceSupport(RcnCtx *ctx) {
    IDXGIAdapter *self = (IDXGIAdapter *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    LARGE_INTEGER * a2 = (LARGE_INTEGER *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->CheckInterfaceSupport(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIAdapter::CheckInterfaceSupport", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter3_RegisterHardwareContentProtectionTeardownStatusEvent(RcnCtx *ctx) {
    IDXGIAdapter3 *self = (IDXGIAdapter3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->RegisterHardwareContentProtectionTeardownStatusEvent(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIAdapter3::RegisterHardwareContentProtectionTeardownStatusEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter3_UnregisterHardwareContentProtectionTeardownStatus(RcnCtx *ctx) {
    IDXGIAdapter3 *self = (IDXGIAdapter3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DWORD a1 = (DWORD)rcn_arg(ctx, 1);
    self->lpVtbl->UnregisterHardwareContentProtectionTeardownStatus(self, a1);
}

static void br_IDXGIAdapter3_QueryVideoMemoryInfo(RcnCtx *ctx) {
    IDXGIAdapter3 *self = (IDXGIAdapter3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    DXGI_MEMORY_SEGMENT_GROUP a2 = (DXGI_MEMORY_SEGMENT_GROUP)rcn_arg(ctx, 2);
    DXGI_QUERY_VIDEO_MEMORY_INFO * a3 = (DXGI_QUERY_VIDEO_MEMORY_INFO *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->QueryVideoMemoryInfo(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIAdapter3::QueryVideoMemoryInfo", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter3_SetVideoMemoryReservation(RcnCtx *ctx) {
    IDXGIAdapter3 *self = (IDXGIAdapter3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    DXGI_MEMORY_SEGMENT_GROUP a2 = (DXGI_MEMORY_SEGMENT_GROUP)rcn_arg(ctx, 2);
    UINT64 a3 = (UINT64)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetVideoMemoryReservation(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIAdapter3::SetVideoMemoryReservation", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter3_RegisterVideoMemoryBudgetChangeNotificationEvent(RcnCtx *ctx) {
    IDXGIAdapter3 *self = (IDXGIAdapter3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->RegisterVideoMemoryBudgetChangeNotificationEvent(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIAdapter3::RegisterVideoMemoryBudgetChangeNotificationEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIAdapter3_UnregisterVideoMemoryBudgetChangeNotification(RcnCtx *ctx) {
    IDXGIAdapter3 *self = (IDXGIAdapter3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DWORD a1 = (DWORD)rcn_arg(ctx, 1);
    self->lpVtbl->UnregisterVideoMemoryBudgetChangeNotification(self, a1);
}

static void br_IDXGIOutput_GetDisplayModeList(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    DXGI_MODE_DESC * a4 = (DXGI_MODE_DESC *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->GetDisplayModeList(self, a1, a2, a3, a4);
    if (FAILED(r)) br_failed("IDXGIOutput::GetDisplayModeList", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_FindClosestMatchingMode(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_MODE_DESC * a1 = (const DXGI_MODE_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    DXGI_MODE_DESC * a2 = (DXGI_MODE_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    IUnknown * a3 = (IUnknown *)br_unwrap(rcn_arg(ctx, 3));
    HRESULT r = self->lpVtbl->FindClosestMatchingMode(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIOutput::FindClosestMatchingMode", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_WaitForVBlank(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->WaitForVBlank(self);
    if (FAILED(r)) br_failed("IDXGIOutput::WaitForVBlank", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_TakeOwnership(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    WINBOOL a2 = (WINBOOL)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->TakeOwnership(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIOutput::TakeOwnership", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_ReleaseOwnership(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->ReleaseOwnership(self);
}

static void br_IDXGIOutput_GetGammaControlCapabilities(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_GAMMA_CONTROL_CAPABILITIES * a1 = (DXGI_GAMMA_CONTROL_CAPABILITIES *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetGammaControlCapabilities(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput::GetGammaControlCapabilities", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_SetGammaControl(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_GAMMA_CONTROL * a1 = (const DXGI_GAMMA_CONTROL *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetGammaControl(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput::SetGammaControl", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_GetGammaControl(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_GAMMA_CONTROL * a1 = (DXGI_GAMMA_CONTROL *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetGammaControl(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput::GetGammaControl", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_SetDisplaySurface(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IDXGISurface * a1 = (IDXGISurface *)br_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->SetDisplaySurface(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput::SetDisplaySurface", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput_GetDisplaySurfaceData(RcnCtx *ctx) {
    IDXGIOutput *self = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IDXGISurface * a1 = (IDXGISurface *)br_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->GetDisplaySurfaceData(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput::GetDisplaySurfaceData", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput1_GetDisplayModeList1(RcnCtx *ctx) {
    IDXGIOutput1 *self = (IDXGIOutput1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    DXGI_MODE_DESC1 * a4 = (DXGI_MODE_DESC1 *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->GetDisplayModeList1(self, a1, a2, a3, a4);
    if (FAILED(r)) br_failed("IDXGIOutput1::GetDisplayModeList1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput1_FindClosestMatchingMode1(RcnCtx *ctx) {
    IDXGIOutput1 *self = (IDXGIOutput1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_MODE_DESC1 * a1 = (const DXGI_MODE_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    DXGI_MODE_DESC1 * a2 = (DXGI_MODE_DESC1 *)(uintptr_t)rcn_arg(ctx, 2);
    IUnknown * a3 = (IUnknown *)br_unwrap(rcn_arg(ctx, 3));
    HRESULT r = self->lpVtbl->FindClosestMatchingMode1(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIOutput1::FindClosestMatchingMode1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput1_GetDisplaySurfaceData1(RcnCtx *ctx) {
    IDXGIOutput1 *self = (IDXGIOutput1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IDXGIResource * a1 = (IDXGIResource *)br_unwrap(rcn_arg(ctx, 1));
    HRESULT r = self->lpVtbl->GetDisplaySurfaceData1(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput1::GetDisplaySurfaceData1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput1_DuplicateOutput(RcnCtx *ctx) {
    IDXGIOutput1 *self = (IDXGIOutput1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); IDXGIOutputDuplication *t2 = NULL;
    HRESULT r = self->lpVtbl->DuplicateOutput(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, BR_FAM_DYNAMIC);
    if (FAILED(r)) br_failed("IDXGIOutput1::DuplicateOutput", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput2_SupportsOverlays(RcnCtx *ctx) {
    IDXGIOutput2 *self = (IDXGIOutput2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->SupportsOverlays(self);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput3_CheckOverlaySupport(RcnCtx *ctx) {
    IDXGIOutput3 *self = (IDXGIOutput3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    IUnknown * a2 = (IUnknown *)br_unwrap(rcn_arg(ctx, 2));
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->CheckOverlaySupport(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGIOutput3::CheckOverlaySupport", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput4_CheckOverlayColorSpaceSupport(RcnCtx *ctx) {
    IDXGIOutput4 *self = (IDXGIOutput4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    DXGI_COLOR_SPACE_TYPE a2 = (DXGI_COLOR_SPACE_TYPE)rcn_arg(ctx, 2);
    IUnknown * a3 = (IUnknown *)br_unwrap(rcn_arg(ctx, 3));
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->CheckOverlayColorSpaceSupport(self, a1, a2, a3, a4);
    if (FAILED(r)) br_failed("IDXGIOutput4::CheckOverlayColorSpaceSupport", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput5_DuplicateOutput1(RcnCtx *ctx) {
    IDXGIOutput5 *self = (IDXGIOutput5 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    IUnknown * a1 = (IUnknown *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    const DXGI_FORMAT * a4 = (const DXGI_FORMAT *)(uintptr_t)rcn_arg(ctx, 4);
    u64 g5 = rcn_arg(ctx, 5); IDXGIOutputDuplication *t5 = NULL;
    HRESULT r = self->lpVtbl->DuplicateOutput1(self, a1, a2, a3, a4, g5 ? &t5 : NULL);
    if (g5) br_out(ctx, g5, t5, BR_FAM_DYNAMIC);
    if (FAILED(r)) br_failed("IDXGIOutput5::DuplicateOutput1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIOutput6_CheckHardwareCompositionSupport(RcnCtx *ctx) {
    IDXGIOutput6 *self = (IDXGIOutput6 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->CheckHardwareCompositionSupport(self, a1);
    if (FAILED(r)) br_failed("IDXGIOutput6::CheckHardwareCompositionSupport", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDeviceSubObject_GetDevice(RcnCtx *ctx) {
    IDXGIDeviceSubObject *self = (IDXGIDeviceSubObject *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    HRESULT r = self->lpVtbl->GetDevice(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out_iid(ctx, g2, t2, (const IID *)(uintptr_t)rcn_arg(ctx, 1), &r);
    if (FAILED(r)) br_failed("IDXGIDeviceSubObject::GetDevice", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_Present(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    br_before_present(ctx);
    HRESULT r = self->lpVtbl->Present(self, a1, a2);
    br_after_present(ctx, (IDXGISwapChain *)self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain::Present", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_GetBuffer(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    REFIID a2 = (REFIID)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); void *t3 = NULL;
    HRESULT r = self->lpVtbl->GetBuffer(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out_iid(ctx, g3, t3, (const IID *)(uintptr_t)rcn_arg(ctx, 2), &r);
    if (FAILED(r)) br_failed("IDXGISwapChain::GetBuffer", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_SetFullscreenState(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)rcn_arg(ctx, 1);
    IDXGIOutput * a2 = (IDXGIOutput *)br_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->SetFullscreenState(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGISwapChain::SetFullscreenState", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_GetFullscreenState(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL * a1 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDXGIOutput *t2 = NULL;
    HRESULT r = self->lpVtbl->GetFullscreenState(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 2);
    if (FAILED(r)) br_failed("IDXGISwapChain::GetFullscreenState", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_GetDesc(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_SWAP_CHAIN_DESC * a1 = (DXGI_SWAP_CHAIN_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain::GetDesc", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_ResizeBuffers(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    DXGI_FORMAT a4 = (DXGI_FORMAT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    br_before_present(ctx); // fenêtre d'Android donnée à DXVK avant sa surface
    HRESULT r = self->lpVtbl->ResizeBuffers(self, a1, a2, a3, a4, a5);
    if (FAILED(r)) br_failed("IDXGISwapChain::ResizeBuffers", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_ResizeTarget(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_MODE_DESC * a1 = (const DXGI_MODE_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->ResizeTarget(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain::ResizeTarget", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_GetContainingOutput(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDXGIOutput *t1 = NULL;
    HRESULT r = self->lpVtbl->GetContainingOutput(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 2);
    if (FAILED(r)) br_failed("IDXGISwapChain::GetContainingOutput", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain_GetLastPresentCount(RcnCtx *ctx) {
    IDXGISwapChain *self = (IDXGISwapChain *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetLastPresentCount(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain::GetLastPresentCount", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetDesc1(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_SWAP_CHAIN_DESC1 * a1 = (DXGI_SWAP_CHAIN_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc1(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetDesc1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetFullscreenDesc(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC * a1 = (DXGI_SWAP_CHAIN_FULLSCREEN_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetFullscreenDesc(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetFullscreenDesc", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetHwnd(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HWND * a1 = (HWND *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetHwnd(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetHwnd", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetCoreWindow(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    HRESULT r = self->lpVtbl->GetCoreWindow(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out_iid(ctx, g2, t2, (const IID *)(uintptr_t)rcn_arg(ctx, 1), &r);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetCoreWindow", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_Present1(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const DXGI_PRESENT_PARAMETERS * a3 = (const DXGI_PRESENT_PARAMETERS *)(uintptr_t)rcn_arg(ctx, 3);
    br_before_present(ctx);
    HRESULT r = self->lpVtbl->Present1(self, a1, a2, a3);
    br_after_present(ctx, (IDXGISwapChain *)self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::Present1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_IsTemporaryMonoSupported(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->IsTemporaryMonoSupported(self);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetRestrictToOutput(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDXGIOutput *t1 = NULL;
    HRESULT r = self->lpVtbl->GetRestrictToOutput(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 2);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetRestrictToOutput", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_SetBackgroundColor(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_RGBA * a1 = (const DXGI_RGBA *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetBackgroundColor(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::SetBackgroundColor", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetBackgroundColor(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_RGBA * a1 = (DXGI_RGBA *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetBackgroundColor(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetBackgroundColor", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_SetRotation(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_MODE_ROTATION a1 = (DXGI_MODE_ROTATION)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetRotation(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::SetRotation", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain1_GetRotation(RcnCtx *ctx) {
    IDXGISwapChain1 *self = (IDXGISwapChain1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_MODE_ROTATION * a1 = (DXGI_MODE_ROTATION *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetRotation(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain1::GetRotation", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain2_SetSourceSize(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetSourceSize(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGISwapChain2::SetSourceSize", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain2_GetSourceSize(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetSourceSize(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGISwapChain2::GetSourceSize", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain2_SetMaximumFrameLatency(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetMaximumFrameLatency(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain2::SetMaximumFrameLatency", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain2_GetMaximumFrameLatency(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetMaximumFrameLatency(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain2::GetMaximumFrameLatency", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain2_GetFrameLatencyWaitableObject(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE r = self->lpVtbl->GetFrameLatencyWaitableObject(self);
    RET((u64)(uintptr_t)(r));
}

static void br_IDXGISwapChain2_SetMatrixTransform(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_MATRIX_3X2_F * a1 = (const DXGI_MATRIX_3X2_F *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetMatrixTransform(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain2::SetMatrixTransform", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain2_GetMatrixTransform(RcnCtx *ctx) {
    IDXGISwapChain2 *self = (IDXGISwapChain2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_MATRIX_3X2_F * a1 = (DXGI_MATRIX_3X2_F *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetMatrixTransform(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain2::GetMatrixTransform", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain3_GetCurrentBackBufferIndex(RcnCtx *ctx) {
    IDXGISwapChain3 *self = (IDXGISwapChain3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetCurrentBackBufferIndex(self);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain3_CheckColorSpaceSupport(RcnCtx *ctx) {
    IDXGISwapChain3 *self = (IDXGISwapChain3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_COLOR_SPACE_TYPE a1 = (DXGI_COLOR_SPACE_TYPE)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->CheckColorSpaceSupport(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGISwapChain3::CheckColorSpaceSupport", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain3_SetColorSpace1(RcnCtx *ctx) {
    IDXGISwapChain3 *self = (IDXGISwapChain3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_COLOR_SPACE_TYPE a1 = (DXGI_COLOR_SPACE_TYPE)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetColorSpace1(self, a1);
    if (FAILED(r)) br_failed("IDXGISwapChain3::SetColorSpace1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain3_ResizeBuffers1(RcnCtx *ctx) {
    IDXGISwapChain3 *self = (IDXGISwapChain3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    DXGI_FORMAT a4 = (DXGI_FORMAT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    const UINT * a6 = (const UINT *)(uintptr_t)rcn_arg(ctx, 6);
    u64 g7 = rcn_arg(ctx, 7); u32 n7 = (u32)rcn_arg(ctx, 1); if (n7 > BR_MAX_ARRAY) n7 = 0;
    IUnknown *t7[BR_MAX_ARRAY];
    for (u32 i = 0; i < n7; i++) t7[i] = g7 ? (IUnknown *)br_unwrap(rd64(g7 + 8ull * i)) : NULL;
    br_before_present(ctx); // fenêtre d'Android donnée à DXVK avant sa surface
    HRESULT r = self->lpVtbl->ResizeBuffers1(self, a1, a2, a3, a4, a5, a6, g7 ? t7 : NULL);
    if (FAILED(r)) br_failed("IDXGISwapChain3::ResizeBuffers1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISwapChain4_SetHDRMetaData(RcnCtx *ctx) {
    IDXGISwapChain4 *self = (IDXGISwapChain4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_HDR_METADATA_TYPE a1 = (DXGI_HDR_METADATA_TYPE)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    void * a3 = (void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetHDRMetaData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("IDXGISwapChain4::SetHDRMetaData", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice_GetAdapter(RcnCtx *ctx) {
    IDXGIDevice *self = (IDXGIDevice *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); IDXGIAdapter *t1 = NULL;
    HRESULT r = self->lpVtbl->GetAdapter(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 1);
    if (FAILED(r)) br_failed("IDXGIDevice::GetAdapter", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice_CreateSurface(RcnCtx *ctx) {
    IDXGIDevice *self = (IDXGIDevice *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const DXGI_SURFACE_DESC * a1 = (const DXGI_SURFACE_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    DXGI_USAGE a3 = (DXGI_USAGE)rcn_arg(ctx, 3);
    const DXGI_SHARED_RESOURCE * a4 = (const DXGI_SHARED_RESOURCE *)(uintptr_t)rcn_arg(ctx, 4);
    u64 g5 = rcn_arg(ctx, 5); IDXGISurface *t5 = NULL;
    HRESULT r = self->lpVtbl->CreateSurface(self, a1, a2, a3, a4, g5 ? &t5 : NULL);
    if (g5) br_out(ctx, g5, t5, 5);
    if (FAILED(r)) br_failed("IDXGIDevice::CreateSurface", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice_QueryResourceResidency(RcnCtx *ctx) {
    IDXGIDevice *self = (IDXGIDevice *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); u32 n1 = (u32)rcn_arg(ctx, 3); if (n1 > BR_MAX_ARRAY) n1 = 0;
    IUnknown *t1[BR_MAX_ARRAY];
    for (u32 i = 0; i < n1; i++) t1[i] = g1 ? (IUnknown *)br_unwrap(rd64(g1 + 8ull * i)) : NULL;
    DXGI_RESIDENCY * a2 = (DXGI_RESIDENCY *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->QueryResourceResidency(self, g1 ? t1 : NULL, a2, a3);
    if (FAILED(r)) br_failed("IDXGIDevice::QueryResourceResidency", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice_SetGPUThreadPriority(RcnCtx *ctx) {
    IDXGIDevice *self = (IDXGIDevice *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    INT a1 = (INT)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetGPUThreadPriority(self, a1);
    if (FAILED(r)) br_failed("IDXGIDevice::SetGPUThreadPriority", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice_GetGPUThreadPriority(RcnCtx *ctx) {
    IDXGIDevice *self = (IDXGIDevice *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    INT * a1 = (INT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetGPUThreadPriority(self, a1);
    if (FAILED(r)) br_failed("IDXGIDevice::GetGPUThreadPriority", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice1_SetMaximumFrameLatency(RcnCtx *ctx) {
    IDXGIDevice1 *self = (IDXGIDevice1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetMaximumFrameLatency(self, a1);
    if (FAILED(r)) br_failed("IDXGIDevice1::SetMaximumFrameLatency", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice1_GetMaximumFrameLatency(RcnCtx *ctx) {
    IDXGIDevice1 *self = (IDXGIDevice1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetMaximumFrameLatency(self, a1);
    if (FAILED(r)) br_failed("IDXGIDevice1::GetMaximumFrameLatency", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice2_OfferResources(RcnCtx *ctx) {
    IDXGIDevice2 *self = (IDXGIDevice2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    IDXGIResource *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (IDXGIResource *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    DXGI_OFFER_RESOURCE_PRIORITY a3 = (DXGI_OFFER_RESOURCE_PRIORITY)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->OfferResources(self, a1, g2 ? t2 : NULL, a3);
    if (FAILED(r)) br_failed("IDXGIDevice2::OfferResources", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice2_ReclaimResources(RcnCtx *ctx) {
    IDXGIDevice2 *self = (IDXGIDevice2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    IDXGIResource *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (IDXGIResource *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    WINBOOL * a3 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->ReclaimResources(self, a1, g2 ? t2 : NULL, a3);
    if (FAILED(r)) br_failed("IDXGIDevice2::ReclaimResources", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice2_EnqueueSetEvent(RcnCtx *ctx) {
    IDXGIDevice2 *self = (IDXGIDevice2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->EnqueueSetEvent(self, a1);
    if (FAILED(r)) br_failed("IDXGIDevice2::EnqueueSetEvent", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice3_Trim(RcnCtx *ctx) {
    IDXGIDevice3 *self = (IDXGIDevice3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->Trim(self);
}

static void br_IDXGIDevice4_OfferResources1(RcnCtx *ctx) {
    IDXGIDevice4 *self = (IDXGIDevice4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    IDXGIResource *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (IDXGIResource *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    DXGI_OFFER_RESOURCE_PRIORITY a3 = (DXGI_OFFER_RESOURCE_PRIORITY)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->OfferResources1(self, a1, g2 ? t2 : NULL, a3, a4);
    if (FAILED(r)) br_failed("IDXGIDevice4::OfferResources1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIDevice4_ReclaimResources1(RcnCtx *ctx) {
    IDXGIDevice4 *self = (IDXGIDevice4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    IDXGIResource *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (IDXGIResource *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    DXGI_RECLAIM_RESOURCE_RESULTS * a3 = (DXGI_RECLAIM_RESOURCE_RESULTS *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->ReclaimResources1(self, a1, g2 ? t2 : NULL, a3);
    if (FAILED(r)) br_failed("IDXGIDevice4::ReclaimResources1", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISurface_GetDesc(RcnCtx *ctx) {
    IDXGISurface *self = (IDXGISurface *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_SURFACE_DESC * a1 = (DXGI_SURFACE_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetDesc(self, a1);
    if (FAILED(r)) br_failed("IDXGISurface::GetDesc", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISurface_Map(RcnCtx *ctx) {
    IDXGISurface *self = (IDXGISurface *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_MAPPED_RECT * a1 = (DXGI_MAPPED_RECT *)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->Map(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGISurface::Map", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISurface_Unmap(RcnCtx *ctx) {
    IDXGISurface *self = (IDXGISurface *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->Unmap(self);
    if (FAILED(r)) br_failed("IDXGISurface::Unmap", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISurface1_GetDC(RcnCtx *ctx) {
    IDXGISurface1 *self = (IDXGISurface1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)rcn_arg(ctx, 1);
    HDC * a2 = (HDC *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->GetDC(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGISurface1::GetDC", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISurface1_ReleaseDC(RcnCtx *ctx) {
    IDXGISurface1 *self = (IDXGISurface1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    RECT * a1 = (RECT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->ReleaseDC(self, a1);
    if (FAILED(r)) br_failed("IDXGISurface1::ReleaseDC", r);
    RET((u64)(i64)(r));
}

static void br_IDXGISurface2_GetResource(RcnCtx *ctx) {
    IDXGISurface2 *self = (IDXGISurface2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFIID a1 = (REFIID)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); void *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetResource(self, a1, g2 ? &t2 : NULL, a3);
    if (g2) br_out_iid(ctx, g2, t2, (const IID *)(uintptr_t)rcn_arg(ctx, 1), &r);
    if (FAILED(r)) br_failed("IDXGISurface2::GetResource", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIResource_GetSharedHandle(RcnCtx *ctx) {
    IDXGIResource *self = (IDXGIResource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE * a1 = (HANDLE *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetSharedHandle(self, a1);
    if (FAILED(r)) br_failed("IDXGIResource::GetSharedHandle", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIResource_GetUsage(RcnCtx *ctx) {
    IDXGIResource *self = (IDXGIResource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_USAGE * a1 = (DXGI_USAGE *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetUsage(self, a1);
    if (FAILED(r)) br_failed("IDXGIResource::GetUsage", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIResource_SetEvictionPriority(RcnCtx *ctx) {
    IDXGIResource *self = (IDXGIResource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetEvictionPriority(self, a1);
    if (FAILED(r)) br_failed("IDXGIResource::SetEvictionPriority", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIResource_GetEvictionPriority(RcnCtx *ctx) {
    IDXGIResource *self = (IDXGIResource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->GetEvictionPriority(self, a1);
    if (FAILED(r)) br_failed("IDXGIResource::GetEvictionPriority", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIResource1_CreateSubresourceSurface(RcnCtx *ctx) {
    IDXGIResource1 *self = (IDXGIResource1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); IDXGISurface2 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateSubresourceSurface(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 5);
    if (FAILED(r)) br_failed("IDXGIResource1::CreateSubresourceSurface", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIResource1_CreateSharedHandle(RcnCtx *ctx) {
    br_unsupported(ctx, "IDXGIResource1::CreateSharedHandle");
}

static void br_IDXGIKeyedMutex_AcquireSync(RcnCtx *ctx) {
    IDXGIKeyedMutex *self = (IDXGIKeyedMutex *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT64 a1 = (UINT64)rcn_arg(ctx, 1);
    DWORD a2 = (DWORD)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->AcquireSync(self, a1, a2);
    if (FAILED(r)) br_failed("IDXGIKeyedMutex::AcquireSync", r);
    RET((u64)(i64)(r));
}

static void br_IDXGIKeyedMutex_ReleaseSync(RcnCtx *ctx) {
    IDXGIKeyedMutex *self = (IDXGIKeyedMutex *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT64 a1 = (UINT64)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->ReleaseSync(self, a1);
    if (FAILED(r)) br_failed("IDXGIKeyedMutex::ReleaseSync", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateBuffer(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_BUFFER_DESC * a1 = (const D3D11_BUFFER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    const D3D11_SUBRESOURCE_DATA * a2 = (const D3D11_SUBRESOURCE_DATA *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11Buffer *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateBuffer(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 10);
    if (FAILED(r)) br_failed("ID3D11Device::CreateBuffer", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateTexture1D(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_TEXTURE1D_DESC * a1 = (const D3D11_TEXTURE1D_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    const D3D11_SUBRESOURCE_DATA * a2 = (const D3D11_SUBRESOURCE_DATA *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11Texture1D *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateTexture1D(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 11);
    if (FAILED(r)) br_failed("ID3D11Device::CreateTexture1D", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateTexture2D(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_TEXTURE2D_DESC * a1 = (const D3D11_TEXTURE2D_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    const D3D11_SUBRESOURCE_DATA * a2 = (const D3D11_SUBRESOURCE_DATA *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11Texture2D *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateTexture2D(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 12);
    if (FAILED(r)) br_failed("ID3D11Device::CreateTexture2D", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateTexture3D(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_TEXTURE3D_DESC * a1 = (const D3D11_TEXTURE3D_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    const D3D11_SUBRESOURCE_DATA * a2 = (const D3D11_SUBRESOURCE_DATA *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11Texture3D *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateTexture3D(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 13);
    if (FAILED(r)) br_failed("ID3D11Device::CreateTexture3D", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateShaderResourceView(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_SHADER_RESOURCE_VIEW_DESC * a2 = (const D3D11_SHADER_RESOURCE_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11ShaderResourceView *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateShaderResourceView(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 14);
    if (FAILED(r)) br_failed("ID3D11Device::CreateShaderResourceView", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateUnorderedAccessView(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_UNORDERED_ACCESS_VIEW_DESC * a2 = (const D3D11_UNORDERED_ACCESS_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11UnorderedAccessView *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateUnorderedAccessView(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 17);
    if (FAILED(r)) br_failed("ID3D11Device::CreateUnorderedAccessView", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateRenderTargetView(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_RENDER_TARGET_VIEW_DESC * a2 = (const D3D11_RENDER_TARGET_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11RenderTargetView *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateRenderTargetView(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 15);
    if (FAILED(r)) br_failed("ID3D11Device::CreateRenderTargetView", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateDepthStencilView(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_DEPTH_STENCIL_VIEW_DESC * a2 = (const D3D11_DEPTH_STENCIL_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11DepthStencilView *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateDepthStencilView(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 16);
    if (FAILED(r)) br_failed("ID3D11Device::CreateDepthStencilView", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateInputLayout(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_INPUT_ELEMENT_DESC * a1 = (const D3D11_INPUT_ELEMENT_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const void * a3 = (const void *)(uintptr_t)rcn_arg(ctx, 3);
    SIZE_T a4 = (SIZE_T)rcn_arg(ctx, 4);
    u64 g5 = rcn_arg(ctx, 5); ID3D11InputLayout *t5 = NULL;
    HRESULT r = self->lpVtbl->CreateInputLayout(self, a1, a2, a3, a4, g5 ? &t5 : NULL);
    if (g5) br_out(ctx, g5, t5, 24);
    if (FAILED(r)) br_failed("ID3D11Device::CreateInputLayout", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateVertexShader(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    ID3D11ClassLinkage * a3 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); ID3D11VertexShader *t4 = NULL;
    HRESULT r = self->lpVtbl->CreateVertexShader(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 18);
    if (FAILED(r)) br_failed("ID3D11Device::CreateVertexShader", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateGeometryShader(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    ID3D11ClassLinkage * a3 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); ID3D11GeometryShader *t4 = NULL;
    HRESULT r = self->lpVtbl->CreateGeometryShader(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 20);
    if (FAILED(r)) br_failed("ID3D11Device::CreateGeometryShader", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateGeometryShaderWithStreamOutput(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    const D3D11_SO_DECLARATION_ENTRY * a3 = (const D3D11_SO_DECLARATION_ENTRY *)(uintptr_t)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    UINT a7 = (UINT)rcn_arg(ctx, 7);
    ID3D11ClassLinkage * a8 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 8));
    u64 g9 = rcn_arg(ctx, 9); ID3D11GeometryShader *t9 = NULL;
    HRESULT r = self->lpVtbl->CreateGeometryShaderWithStreamOutput(self, a1, a2, a3, a4, a5, a6, a7, a8, g9 ? &t9 : NULL);
    if (g9) br_out(ctx, g9, t9, 20);
    if (FAILED(r)) br_failed("ID3D11Device::CreateGeometryShaderWithStreamOutput", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreatePixelShader(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    ID3D11ClassLinkage * a3 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); ID3D11PixelShader *t4 = NULL;
    HRESULT r = self->lpVtbl->CreatePixelShader(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 19);
    if (FAILED(r)) br_failed("ID3D11Device::CreatePixelShader", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateHullShader(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    ID3D11ClassLinkage * a3 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); ID3D11HullShader *t4 = NULL;
    HRESULT r = self->lpVtbl->CreateHullShader(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 21);
    if (FAILED(r)) br_failed("ID3D11Device::CreateHullShader", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateDomainShader(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    ID3D11ClassLinkage * a3 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); ID3D11DomainShader *t4 = NULL;
    HRESULT r = self->lpVtbl->CreateDomainShader(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 22);
    if (FAILED(r)) br_failed("ID3D11Device::CreateDomainShader", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateComputeShader(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const void * a1 = (const void *)(uintptr_t)rcn_arg(ctx, 1);
    SIZE_T a2 = (SIZE_T)rcn_arg(ctx, 2);
    ID3D11ClassLinkage * a3 = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 3));
    u64 g4 = rcn_arg(ctx, 4); ID3D11ComputeShader *t4 = NULL;
    HRESULT r = self->lpVtbl->CreateComputeShader(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out(ctx, g4, t4, 23);
    if (FAILED(r)) br_failed("ID3D11Device::CreateComputeShader", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateClassLinkage(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11ClassLinkage *t1 = NULL;
    HRESULT r = self->lpVtbl->CreateClassLinkage(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 33);
    if (FAILED(r)) br_failed("ID3D11Device::CreateClassLinkage", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateBlendState(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_BLEND_DESC * a1 = (const D3D11_BLEND_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11BlendState *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateBlendState(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 25);
    if (FAILED(r)) br_failed("ID3D11Device::CreateBlendState", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateDepthStencilState(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_DEPTH_STENCIL_DESC * a1 = (const D3D11_DEPTH_STENCIL_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11DepthStencilState *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateDepthStencilState(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 26);
    if (FAILED(r)) br_failed("ID3D11Device::CreateDepthStencilState", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateRasterizerState(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_RASTERIZER_DESC * a1 = (const D3D11_RASTERIZER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11RasterizerState *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateRasterizerState(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 27);
    if (FAILED(r)) br_failed("ID3D11Device::CreateRasterizerState", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateSamplerState(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_SAMPLER_DESC * a1 = (const D3D11_SAMPLER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11SamplerState *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateSamplerState(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 28);
    if (FAILED(r)) br_failed("ID3D11Device::CreateSamplerState", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateQuery(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_QUERY_DESC * a1 = (const D3D11_QUERY_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11Query *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateQuery(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 29);
    if (FAILED(r)) br_failed("ID3D11Device::CreateQuery", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreatePredicate(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_QUERY_DESC * a1 = (const D3D11_QUERY_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11Predicate *t2 = NULL;
    HRESULT r = self->lpVtbl->CreatePredicate(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 30);
    if (FAILED(r)) br_failed("ID3D11Device::CreatePredicate", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateCounter(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_COUNTER_DESC * a1 = (const D3D11_COUNTER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11Counter *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateCounter(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 31);
    if (FAILED(r)) br_failed("ID3D11Device::CreateCounter", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CreateDeferredContext(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11DeviceContext *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateDeferredContext(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 9);
    if (FAILED(r)) br_failed("ID3D11Device::CreateDeferredContext", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_OpenSharedResource(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    REFIID a2 = (REFIID)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); void *t3 = NULL;
    HRESULT r = self->lpVtbl->OpenSharedResource(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out_iid(ctx, g3, t3, (const IID *)(uintptr_t)rcn_arg(ctx, 2), &r);
    if (FAILED(r)) br_failed("ID3D11Device::OpenSharedResource", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CheckFormatSupport(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->CheckFormatSupport(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11Device::CheckFormatSupport", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CheckMultisampleQualityLevels(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->CheckMultisampleQualityLevels(self, a1, a2, a3);
    if (FAILED(r)) br_failed("ID3D11Device::CheckMultisampleQualityLevels", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CheckCounterInfo(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_COUNTER_INFO * a1 = (D3D11_COUNTER_INFO *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->CheckCounterInfo(self, a1);
}

static void br_ID3D11Device_CheckCounter(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_COUNTER_DESC * a1 = (const D3D11_COUNTER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    D3D11_COUNTER_TYPE * a2 = (D3D11_COUNTER_TYPE *)(uintptr_t)rcn_arg(ctx, 2);
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    LPSTR a4 = (LPSTR)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    LPSTR a6 = (LPSTR)rcn_arg(ctx, 6);
    UINT * a7 = (UINT *)(uintptr_t)rcn_arg(ctx, 7);
    LPSTR a8 = (LPSTR)rcn_arg(ctx, 8);
    UINT * a9 = (UINT *)(uintptr_t)rcn_arg(ctx, 9);
    HRESULT r = self->lpVtbl->CheckCounter(self, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    if (FAILED(r)) br_failed("ID3D11Device::CheckCounter", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_CheckFeatureSupport(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_FEATURE a1 = (D3D11_FEATURE)rcn_arg(ctx, 1);
    void * a2 = (void *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->CheckFeatureSupport(self, a1, a2, a3);
    if (FAILED(r)) br_failed("ID3D11Device::CheckFeatureSupport", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_GetPrivateData(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    void * a3 = (void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("ID3D11Device::GetPrivateData", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_SetPrivateData(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const void * a3 = (const void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("ID3D11Device::SetPrivateData", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_SetPrivateDataInterface(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    const IUnknown * a2 = (const IUnknown *)br_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->SetPrivateDataInterface(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11Device::SetPrivateDataInterface", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_GetFeatureLevel(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D_FEATURE_LEVEL r = self->lpVtbl->GetFeatureLevel(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_GetCreationFlags(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetCreationFlags(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_GetDeviceRemovedReason(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HRESULT r = self->lpVtbl->GetDeviceRemovedReason(self);
    if (FAILED(r)) br_failed("ID3D11Device::GetDeviceRemovedReason", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_GetImmediateContext(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11DeviceContext *t1 = NULL;
    self->lpVtbl->GetImmediateContext(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 9);
}

static void br_ID3D11Device_SetExceptionMode(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    HRESULT r = self->lpVtbl->SetExceptionMode(self, a1);
    if (FAILED(r)) br_failed("ID3D11Device::SetExceptionMode", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device_GetExceptionMode(RcnCtx *ctx) {
    ID3D11Device *self = (ID3D11Device *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetExceptionMode(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device1_GetImmediateContext1(RcnCtx *ctx) {
    ID3D11Device1 *self = (ID3D11Device1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11DeviceContext1 *t1 = NULL;
    self->lpVtbl->GetImmediateContext1(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 9);
}

static void br_ID3D11Device1_CreateDeferredContext1(RcnCtx *ctx) {
    ID3D11Device1 *self = (ID3D11Device1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11DeviceContext1 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateDeferredContext1(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 9);
    if (FAILED(r)) br_failed("ID3D11Device1::CreateDeferredContext1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device1_CreateBlendState1(RcnCtx *ctx) {
    ID3D11Device1 *self = (ID3D11Device1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_BLEND_DESC1 * a1 = (const D3D11_BLEND_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11BlendState1 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateBlendState1(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 25);
    if (FAILED(r)) br_failed("ID3D11Device1::CreateBlendState1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device1_CreateRasterizerState1(RcnCtx *ctx) {
    ID3D11Device1 *self = (ID3D11Device1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_RASTERIZER_DESC1 * a1 = (const D3D11_RASTERIZER_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11RasterizerState1 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateRasterizerState1(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 27);
    if (FAILED(r)) br_failed("ID3D11Device1::CreateRasterizerState1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device1_CreateDeviceContextState(RcnCtx *ctx) {
    ID3D11Device1 *self = (ID3D11Device1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    const D3D_FEATURE_LEVEL * a2 = (const D3D_FEATURE_LEVEL *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    REFIID a5 = (REFIID)(uintptr_t)rcn_arg(ctx, 5);
    D3D_FEATURE_LEVEL * a6 = (D3D_FEATURE_LEVEL *)(uintptr_t)rcn_arg(ctx, 6);
    u64 g7 = rcn_arg(ctx, 7); ID3DDeviceContextState *t7 = NULL;
    HRESULT r = self->lpVtbl->CreateDeviceContextState(self, a1, a2, a3, a4, a5, a6, g7 ? &t7 : NULL);
    if (g7) br_out(ctx, g7, t7, BR_FAM_DYNAMIC);
    if (FAILED(r)) br_failed("ID3D11Device1::CreateDeviceContextState", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device1_OpenSharedResource1(RcnCtx *ctx) {
    ID3D11Device1 *self = (ID3D11Device1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    REFIID a2 = (REFIID)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); void *t3 = NULL;
    HRESULT r = self->lpVtbl->OpenSharedResource1(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out_iid(ctx, g3, t3, (const IID *)(uintptr_t)rcn_arg(ctx, 2), &r);
    if (FAILED(r)) br_failed("ID3D11Device1::OpenSharedResource1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device1_OpenSharedResourceByName(RcnCtx *ctx) {
    br_unsupported(ctx, "ID3D11Device1::OpenSharedResourceByName");
}

static void br_ID3D11Device2_GetImmediateContext2(RcnCtx *ctx) {
    ID3D11Device2 *self = (ID3D11Device2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11DeviceContext2 *t1 = NULL;
    self->lpVtbl->GetImmediateContext2(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 9);
}

static void br_ID3D11Device2_CreateDeferredContext2(RcnCtx *ctx) {
    ID3D11Device2 *self = (ID3D11Device2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11DeviceContext2 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateDeferredContext2(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 9);
    if (FAILED(r)) br_failed("ID3D11Device2::CreateDeferredContext2", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device2_GetResourceTiling(RcnCtx *ctx) {
    ID3D11Device2 *self = (ID3D11Device2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    D3D11_PACKED_MIP_DESC * a3 = (D3D11_PACKED_MIP_DESC *)(uintptr_t)rcn_arg(ctx, 3);
    D3D11_TILE_SHAPE * a4 = (D3D11_TILE_SHAPE *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    D3D11_SUBRESOURCE_TILING * a7 = (D3D11_SUBRESOURCE_TILING *)(uintptr_t)rcn_arg(ctx, 7);
    self->lpVtbl->GetResourceTiling(self, a1, a2, a3, a4, a5, a6, a7);
}

static void br_ID3D11Device2_CheckMultisampleQualityLevels1(RcnCtx *ctx) {
    ID3D11Device2 *self = (ID3D11Device2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DXGI_FORMAT a1 = (DXGI_FORMAT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->CheckMultisampleQualityLevels1(self, a1, a2, a3, a4);
    if (FAILED(r)) br_failed("ID3D11Device2::CheckMultisampleQualityLevels1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateTexture2D1(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_TEXTURE2D_DESC1 * a1 = (const D3D11_TEXTURE2D_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    const D3D11_SUBRESOURCE_DATA * a2 = (const D3D11_SUBRESOURCE_DATA *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11Texture2D1 *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateTexture2D1(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 12);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateTexture2D1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateTexture3D1(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_TEXTURE3D_DESC1 * a1 = (const D3D11_TEXTURE3D_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    const D3D11_SUBRESOURCE_DATA * a2 = (const D3D11_SUBRESOURCE_DATA *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11Texture3D1 *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateTexture3D1(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 13);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateTexture3D1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateRasterizerState2(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_RASTERIZER_DESC2 * a1 = (const D3D11_RASTERIZER_DESC2 *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11RasterizerState2 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateRasterizerState2(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 27);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateRasterizerState2", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateShaderResourceView1(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_SHADER_RESOURCE_VIEW_DESC1 * a2 = (const D3D11_SHADER_RESOURCE_VIEW_DESC1 *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11ShaderResourceView1 *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateShaderResourceView1(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 14);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateShaderResourceView1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateUnorderedAccessView1(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_UNORDERED_ACCESS_VIEW_DESC1 * a2 = (const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11UnorderedAccessView1 *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateUnorderedAccessView1(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 17);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateUnorderedAccessView1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateRenderTargetView1(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_RENDER_TARGET_VIEW_DESC1 * a2 = (const D3D11_RENDER_TARGET_VIEW_DESC1 *)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11RenderTargetView1 *t3 = NULL;
    HRESULT r = self->lpVtbl->CreateRenderTargetView1(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 15);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateRenderTargetView1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_CreateQuery1(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    const D3D11_QUERY_DESC1 * a1 = (const D3D11_QUERY_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11Query1 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateQuery1(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 29);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateQuery1", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_GetImmediateContext3(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11DeviceContext3 *t1 = NULL;
    self->lpVtbl->GetImmediateContext3(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 9);
}

static void br_ID3D11Device3_CreateDeferredContext3(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11DeviceContext3 *t2 = NULL;
    HRESULT r = self->lpVtbl->CreateDeferredContext3(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 9);
    if (FAILED(r)) br_failed("ID3D11Device3::CreateDeferredContext3", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device3_WriteToSubresource(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const D3D11_BOX * a3 = (const D3D11_BOX *)(uintptr_t)rcn_arg(ctx, 3);
    const void * a4 = (const void *)(uintptr_t)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    self->lpVtbl->WriteToSubresource(self, a1, a2, a3, a4, a5, a6);
}

static void br_ID3D11Device3_ReadFromSubresource(RcnCtx *ctx) {
    ID3D11Device3 *self = (ID3D11Device3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    void * a1 = (void *)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    ID3D11Resource * a4 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 4));
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    const D3D11_BOX * a6 = (const D3D11_BOX *)(uintptr_t)rcn_arg(ctx, 6);
    self->lpVtbl->ReadFromSubresource(self, a1, a2, a3, a4, a5, a6);
}

static void br_ID3D11Device4_RegisterDeviceRemovedEvent(RcnCtx *ctx) {
    ID3D11Device4 *self = (ID3D11Device4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    DWORD * a2 = (DWORD *)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->RegisterDeviceRemovedEvent(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11Device4::RegisterDeviceRemovedEvent", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device4_UnregisterDeviceRemoved(RcnCtx *ctx) {
    ID3D11Device4 *self = (ID3D11Device4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    DWORD a1 = (DWORD)rcn_arg(ctx, 1);
    self->lpVtbl->UnregisterDeviceRemoved(self, a1);
}

static void br_ID3D11Device5_OpenSharedFence(RcnCtx *ctx) {
    ID3D11Device5 *self = (ID3D11Device5 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    HANDLE a1 = (HANDLE)(uintptr_t)rcn_arg(ctx, 1);
    REFIID a2 = (REFIID)(uintptr_t)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); void *t3 = NULL;
    HRESULT r = self->lpVtbl->OpenSharedFence(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out_iid(ctx, g3, t3, (const IID *)(uintptr_t)rcn_arg(ctx, 2), &r);
    if (FAILED(r)) br_failed("ID3D11Device5::OpenSharedFence", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Device5_CreateFence(RcnCtx *ctx) {
    ID3D11Device5 *self = (ID3D11Device5 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT64 a1 = (UINT64)rcn_arg(ctx, 1);
    D3D11_FENCE_FLAG a2 = (D3D11_FENCE_FLAG)rcn_arg(ctx, 2);
    REFIID a3 = (REFIID)(uintptr_t)rcn_arg(ctx, 3);
    u64 g4 = rcn_arg(ctx, 4); void *t4 = NULL;
    HRESULT r = self->lpVtbl->CreateFence(self, a1, a2, a3, g4 ? &t4 : NULL);
    if (g4) br_out_iid(ctx, g4, t4, (const IID *)(uintptr_t)rcn_arg(ctx, 3), &r);
    if (FAILED(r)) br_failed("ID3D11Device5::CreateFence", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceChild_GetDevice(RcnCtx *ctx) {
    ID3D11DeviceChild *self = (ID3D11DeviceChild *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11Device *t1 = NULL;
    self->lpVtbl->GetDevice(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 8);
}

static void br_ID3D11DeviceChild_GetPrivateData(RcnCtx *ctx) {
    ID3D11DeviceChild *self = (ID3D11DeviceChild *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    void * a3 = (void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->GetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("ID3D11DeviceChild::GetPrivateData", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceChild_SetPrivateData(RcnCtx *ctx) {
    ID3D11DeviceChild *self = (ID3D11DeviceChild *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const void * a3 = (const void *)(uintptr_t)rcn_arg(ctx, 3);
    HRESULT r = self->lpVtbl->SetPrivateData(self, a1, a2, a3);
    if (FAILED(r)) br_failed("ID3D11DeviceChild::SetPrivateData", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceChild_SetPrivateDataInterface(RcnCtx *ctx) {
    ID3D11DeviceChild *self = (ID3D11DeviceChild *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    REFGUID a1 = (REFGUID)(uintptr_t)rcn_arg(ctx, 1);
    const IUnknown * a2 = (const IUnknown *)br_unwrap(rcn_arg(ctx, 2));
    HRESULT r = self->lpVtbl->SetPrivateDataInterface(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11DeviceChild::SetPrivateDataInterface", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext_VSSetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->VSSetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_PSSetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11ShaderResourceView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->PSSetShaderResources(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_PSSetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11PixelShader * a1 = (ID3D11PixelShader *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 3); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11ClassInstance *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11ClassInstance *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->PSSetShader(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_PSSetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11SamplerState *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->PSSetSamplers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_VSSetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11VertexShader * a1 = (ID3D11VertexShader *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 3); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11ClassInstance *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11ClassInstance *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->VSSetShader(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_DrawIndexed(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    INT a3 = (INT)rcn_arg(ctx, 3);
    self->lpVtbl->DrawIndexed(self, a1, a2, a3);
}

static void br_ID3D11DeviceContext_Draw(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    self->lpVtbl->Draw(self, a1, a2);
}

static void br_ID3D11DeviceContext_Map(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    D3D11_MAP a3 = (D3D11_MAP)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    D3D11_MAPPED_SUBRESOURCE * a5 = (D3D11_MAPPED_SUBRESOURCE *)(uintptr_t)rcn_arg(ctx, 5);
    HRESULT r = self->lpVtbl->Map(self, a1, a2, a3, a4, a5);
    if (FAILED(r)) br_failed("ID3D11DeviceContext::Map", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext_Unmap(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    self->lpVtbl->Unmap(self, a1, a2);
}

static void br_ID3D11DeviceContext_PSSetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->PSSetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_IASetInputLayout(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11InputLayout * a1 = (ID3D11InputLayout *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->IASetInputLayout(self, a1);
}

static void br_ID3D11DeviceContext_IASetVertexBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->IASetVertexBuffers(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext_IASetIndexBuffer(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Buffer * a1 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 1));
    DXGI_FORMAT a2 = (DXGI_FORMAT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->IASetIndexBuffer(self, a1, a2, a3);
}

static void br_ID3D11DeviceContext_DrawIndexedInstanced(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    INT a4 = (INT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    self->lpVtbl->DrawIndexedInstanced(self, a1, a2, a3, a4, a5);
}

static void br_ID3D11DeviceContext_DrawInstanced(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    self->lpVtbl->DrawInstanced(self, a1, a2, a3, a4);
}

static void br_ID3D11DeviceContext_GSSetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->GSSetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_GSSetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11GeometryShader * a1 = (ID3D11GeometryShader *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 3); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11ClassInstance *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11ClassInstance *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->GSSetShader(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_IASetPrimitiveTopology(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_PRIMITIVE_TOPOLOGY a1 = (D3D11_PRIMITIVE_TOPOLOGY)rcn_arg(ctx, 1);
    self->lpVtbl->IASetPrimitiveTopology(self, a1);
}

static void br_ID3D11DeviceContext_VSSetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11ShaderResourceView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->VSSetShaderResources(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_VSSetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11SamplerState *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->VSSetSamplers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_Begin(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Asynchronous * a1 = (ID3D11Asynchronous *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->Begin(self, a1);
}

static void br_ID3D11DeviceContext_End(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Asynchronous * a1 = (ID3D11Asynchronous *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->End(self, a1);
}

static void br_ID3D11DeviceContext_GetData(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Asynchronous * a1 = (ID3D11Asynchronous *)br_unwrap(rcn_arg(ctx, 1));
    void * a2 = (void *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    HRESULT r = self->lpVtbl->GetData(self, a1, a2, a3, a4);
    if (FAILED(r)) br_failed("ID3D11DeviceContext::GetData", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext_SetPredication(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Predicate * a1 = (ID3D11Predicate *)br_unwrap(rcn_arg(ctx, 1));
    WINBOOL a2 = (WINBOOL)rcn_arg(ctx, 2);
    self->lpVtbl->SetPredication(self, a1, a2);
}

static void br_ID3D11DeviceContext_GSSetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11ShaderResourceView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->GSSetShaderResources(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_GSSetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11SamplerState *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->GSSetSamplers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_OMSetRenderTargets(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11RenderTargetView *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11RenderTargetView *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    ID3D11DepthStencilView * a3 = (ID3D11DepthStencilView *)br_unwrap(rcn_arg(ctx, 3));
    self->lpVtbl->OMSetRenderTargets(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11RenderTargetView *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11RenderTargetView *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    ID3D11DepthStencilView * a3 = (ID3D11DepthStencilView *)br_unwrap(rcn_arg(ctx, 3));
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    u64 g6 = rcn_arg(ctx, 6); u32 n6 = (u32)rcn_arg(ctx, 5); if (n6 > BR_MAX_ARRAY) n6 = 0;
    ID3D11UnorderedAccessView *t6[BR_MAX_ARRAY];
    for (u32 i = 0; i < n6; i++) t6[i] = g6 ? (ID3D11UnorderedAccessView *)br_unwrap(rd64(g6 + 8ull * i)) : NULL;
    const UINT * a7 = (const UINT *)(uintptr_t)rcn_arg(ctx, 7);
    self->lpVtbl->OMSetRenderTargetsAndUnorderedAccessViews(self, a1, g2 ? t2 : NULL, a3, a4, a5, g6 ? t6 : NULL, a7);
}

static void br_ID3D11DeviceContext_OMSetBlendState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11BlendState * a1 = (ID3D11BlendState *)br_unwrap(rcn_arg(ctx, 1));
    const FLOAT * a2 = (const FLOAT *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->OMSetBlendState(self, a1, a2, a3);
}

static void br_ID3D11DeviceContext_OMSetDepthStencilState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11DepthStencilState * a1 = (ID3D11DepthStencilState *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    self->lpVtbl->OMSetDepthStencilState(self, a1, a2);
}

static void br_ID3D11DeviceContext_SOSetTargets(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11Buffer *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11Buffer *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    const UINT * a3 = (const UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->SOSetTargets(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_DrawAuto(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->DrawAuto(self);
}

static void br_ID3D11DeviceContext_DrawIndexedInstancedIndirect(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Buffer * a1 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    self->lpVtbl->DrawIndexedInstancedIndirect(self, a1, a2);
}

static void br_ID3D11DeviceContext_DrawInstancedIndirect(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Buffer * a1 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    self->lpVtbl->DrawInstancedIndirect(self, a1, a2);
}

static void br_ID3D11DeviceContext_Dispatch(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->Dispatch(self, a1, a2, a3);
}

static void br_ID3D11DeviceContext_DispatchIndirect(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Buffer * a1 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    self->lpVtbl->DispatchIndirect(self, a1, a2);
}

static void br_ID3D11DeviceContext_RSSetState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11RasterizerState * a1 = (ID3D11RasterizerState *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->RSSetState(self, a1);
}

static void br_ID3D11DeviceContext_RSSetViewports(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    const D3D11_VIEWPORT * a2 = (const D3D11_VIEWPORT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->RSSetViewports(self, a1, a2);
}

static void br_ID3D11DeviceContext_RSSetScissorRects(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    const D3D11_RECT * a2 = (const D3D11_RECT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->RSSetScissorRects(self, a1, a2);
}

static void br_ID3D11DeviceContext_CopySubresourceRegion(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    ID3D11Resource * a6 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 6));
    UINT a7 = (UINT)rcn_arg(ctx, 7);
    const D3D11_BOX * a8 = (const D3D11_BOX *)(uintptr_t)rcn_arg(ctx, 8);
    self->lpVtbl->CopySubresourceRegion(self, a1, a2, a3, a4, a5, a6, a7, a8);
}

static void br_ID3D11DeviceContext_CopyResource(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    ID3D11Resource * a2 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 2));
    self->lpVtbl->CopyResource(self, a1, a2);
}

static void br_ID3D11DeviceContext_UpdateSubresource(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const D3D11_BOX * a3 = (const D3D11_BOX *)(uintptr_t)rcn_arg(ctx, 3);
    const void * a4 = (const void *)(uintptr_t)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    self->lpVtbl->UpdateSubresource(self, a1, a2, a3, a4, a5, a6);
}

static void br_ID3D11DeviceContext_CopyStructureCount(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Buffer * a1 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    ID3D11UnorderedAccessView * a3 = (ID3D11UnorderedAccessView *)br_unwrap(rcn_arg(ctx, 3));
    self->lpVtbl->CopyStructureCount(self, a1, a2, a3);
}

static void br_ID3D11DeviceContext_ClearRenderTargetView(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11RenderTargetView * a1 = (ID3D11RenderTargetView *)br_unwrap(rcn_arg(ctx, 1));
    const FLOAT * a2 = (const FLOAT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->ClearRenderTargetView(self, a1, a2);
}

static void br_ID3D11DeviceContext_ClearUnorderedAccessViewUint(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11UnorderedAccessView * a1 = (ID3D11UnorderedAccessView *)br_unwrap(rcn_arg(ctx, 1));
    const UINT * a2 = (const UINT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->ClearUnorderedAccessViewUint(self, a1, a2);
}

static void br_ID3D11DeviceContext_ClearUnorderedAccessViewFloat(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11UnorderedAccessView * a1 = (ID3D11UnorderedAccessView *)br_unwrap(rcn_arg(ctx, 1));
    const FLOAT * a2 = (const FLOAT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->ClearUnorderedAccessViewFloat(self, a1, a2);
}

static void br_ID3D11DeviceContext_ClearDepthStencilView(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11DepthStencilView * a1 = (ID3D11DepthStencilView *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    FLOAT a3 = br_float_arg(ctx, 3);
    UINT8 a4 = (UINT8)rcn_arg(ctx, 4);
    self->lpVtbl->ClearDepthStencilView(self, a1, a2, a3, a4);
}

static void br_ID3D11DeviceContext_GenerateMips(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11ShaderResourceView * a1 = (ID3D11ShaderResourceView *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->GenerateMips(self, a1);
}

static void br_ID3D11DeviceContext_SetResourceMinLOD(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    FLOAT a2 = br_float_arg(ctx, 2);
    self->lpVtbl->SetResourceMinLOD(self, a1, a2);
}

static void br_ID3D11DeviceContext_GetResourceMinLOD(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    FLOAT r = self->lpVtbl->GetResourceMinLOD(self, a1);
    ctx->xmm[0].f[0] = r;
}

static void br_ID3D11DeviceContext_ResolveSubresource(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    ID3D11Resource * a3 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 3));
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    DXGI_FORMAT a5 = (DXGI_FORMAT)rcn_arg(ctx, 5);
    self->lpVtbl->ResolveSubresource(self, a1, a2, a3, a4, a5);
}

static void br_ID3D11DeviceContext_ExecuteCommandList(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11CommandList * a1 = (ID3D11CommandList *)br_unwrap(rcn_arg(ctx, 1));
    WINBOOL a2 = (WINBOOL)rcn_arg(ctx, 2);
    self->lpVtbl->ExecuteCommandList(self, a1, a2);
}

static void br_ID3D11DeviceContext_HSSetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11ShaderResourceView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->HSSetShaderResources(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_HSSetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11HullShader * a1 = (ID3D11HullShader *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 3); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11ClassInstance *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11ClassInstance *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->HSSetShader(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_HSSetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11SamplerState *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->HSSetSamplers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_HSSetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->HSSetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_DSSetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11ShaderResourceView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->DSSetShaderResources(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_DSSetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11DomainShader * a1 = (ID3D11DomainShader *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 3); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11ClassInstance *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11ClassInstance *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->DSSetShader(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_DSSetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11SamplerState *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->DSSetSamplers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_DSSetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->DSSetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_CSSetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11ShaderResourceView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->CSSetShaderResources(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_CSSetUnorderedAccessViews(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11UnorderedAccessView *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11UnorderedAccessView *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    self->lpVtbl->CSSetUnorderedAccessViews(self, a1, a2, g3 ? t3 : NULL, a4);
}

static void br_ID3D11DeviceContext_CSSetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11ComputeShader * a1 = (ID3D11ComputeShader *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 3); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11ClassInstance *t2[BR_MAX_ARRAY];
    for (u32 i = 0; i < n2; i++) t2[i] = g2 ? (ID3D11ClassInstance *)br_unwrap(rd64(g2 + 8ull * i)) : NULL;
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->CSSetShader(self, a1, g2 ? t2 : NULL, a3);
}

static void br_ID3D11DeviceContext_CSSetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11SamplerState *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->CSSetSamplers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_CSSetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    self->lpVtbl->CSSetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
}

static void br_ID3D11DeviceContext_VSGetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->VSGetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_PSGetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->PSGetShaderResources(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 14);
}

static void br_ID3D11DeviceContext_PSGetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11PixelShader *t1 = NULL;
    u64 g2 = rcn_arg(ctx, 2); ID3D11ClassInstance *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->PSGetShader(self, g1 ? &t1 : NULL, g2 ? &t2 : NULL, a3);
    if (g1) br_out(ctx, g1, t1, 19);
    if (g2) br_out(ctx, g2, t2, 34);
}

static void br_ID3D11DeviceContext_PSGetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->PSGetSamplers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 28);
}

static void br_ID3D11DeviceContext_VSGetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11VertexShader *t1 = NULL;
    u64 g2 = rcn_arg(ctx, 2); ID3D11ClassInstance *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->VSGetShader(self, g1 ? &t1 : NULL, g2 ? &t2 : NULL, a3);
    if (g1) br_out(ctx, g1, t1, 18);
    if (g2) br_out(ctx, g2, t2, 34);
}

static void br_ID3D11DeviceContext_PSGetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->PSGetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_IAGetInputLayout(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11InputLayout *t1 = NULL;
    self->lpVtbl->IAGetInputLayout(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 24);
}

static void br_ID3D11DeviceContext_IAGetVertexBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->IAGetVertexBuffers(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_IAGetIndexBuffer(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11Buffer *t1 = NULL;
    DXGI_FORMAT * a2 = (DXGI_FORMAT *)(uintptr_t)rcn_arg(ctx, 2);
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->IAGetIndexBuffer(self, g1 ? &t1 : NULL, a2, a3);
    if (g1) br_out(ctx, g1, t1, 10);
}

static void br_ID3D11DeviceContext_GSGetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->GSGetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_GSGetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11GeometryShader *t1 = NULL;
    u64 g2 = rcn_arg(ctx, 2); ID3D11ClassInstance *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->GSGetShader(self, g1 ? &t1 : NULL, g2 ? &t2 : NULL, a3);
    if (g1) br_out(ctx, g1, t1, 20);
    if (g2) br_out(ctx, g2, t2, 34);
}

static void br_ID3D11DeviceContext_IAGetPrimitiveTopology(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_PRIMITIVE_TOPOLOGY * a1 = (D3D11_PRIMITIVE_TOPOLOGY *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->IAGetPrimitiveTopology(self, a1);
}

static void br_ID3D11DeviceContext_VSGetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->VSGetShaderResources(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 14);
}

static void br_ID3D11DeviceContext_VSGetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->VSGetSamplers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 28);
}

static void br_ID3D11DeviceContext_GetPredication(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11Predicate *t1 = NULL;
    WINBOOL * a2 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->GetPredication(self, g1 ? &t1 : NULL, a2);
    if (g1) br_out(ctx, g1, t1, 30);
}

static void br_ID3D11DeviceContext_GSGetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->GSGetShaderResources(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 14);
}

static void br_ID3D11DeviceContext_GSGetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->GSGetSamplers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 28);
}

static void br_ID3D11DeviceContext_OMGetRenderTargets(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11RenderTargetView *t2[BR_MAX_ARRAY] = {0};
    u64 g3 = rcn_arg(ctx, 3); ID3D11DepthStencilView *t3 = NULL;
    self->lpVtbl->OMGetRenderTargets(self, a1, g2 ? t2 : NULL, g3 ? &t3 : NULL);
    for (u32 i = 0; g2 && i < n2; i++) br_out(ctx, g2 + 8ull * i, t2[i], 15);
    if (g3) br_out(ctx, g3, t3, 16);
}

static void br_ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11RenderTargetView *t2[BR_MAX_ARRAY] = {0};
    u64 g3 = rcn_arg(ctx, 3); ID3D11DepthStencilView *t3 = NULL;
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    u64 g6 = rcn_arg(ctx, 6); u32 n6 = (u32)rcn_arg(ctx, 5); if (n6 > BR_MAX_ARRAY) n6 = 0;
    ID3D11UnorderedAccessView *t6[BR_MAX_ARRAY] = {0};
    self->lpVtbl->OMGetRenderTargetsAndUnorderedAccessViews(self, a1, g2 ? t2 : NULL, g3 ? &t3 : NULL, a4, a5, g6 ? t6 : NULL);
    for (u32 i = 0; g2 && i < n2; i++) br_out(ctx, g2 + 8ull * i, t2[i], 15);
    if (g3) br_out(ctx, g3, t3, 16);
    for (u32 i = 0; g6 && i < n6; i++) br_out(ctx, g6 + 8ull * i, t6[i], 17);
}

static void br_ID3D11DeviceContext_OMGetBlendState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11BlendState *t1 = NULL;
    FLOAT * a2 = (FLOAT *)(uintptr_t)rcn_arg(ctx, 2);
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->OMGetBlendState(self, g1 ? &t1 : NULL, a2, a3);
    if (g1) br_out(ctx, g1, t1, 25);
}

static void br_ID3D11DeviceContext_OMGetDepthStencilState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11DepthStencilState *t1 = NULL;
    UINT * a2 = (UINT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->OMGetDepthStencilState(self, g1 ? &t1 : NULL, a2);
    if (g1) br_out(ctx, g1, t1, 26);
}

static void br_ID3D11DeviceContext_SOGetTargets(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); u32 n2 = (u32)rcn_arg(ctx, 1); if (n2 > BR_MAX_ARRAY) n2 = 0;
    ID3D11Buffer *t2[BR_MAX_ARRAY] = {0};
    self->lpVtbl->SOGetTargets(self, a1, g2 ? t2 : NULL);
    for (u32 i = 0; g2 && i < n2; i++) br_out(ctx, g2 + 8ull * i, t2[i], 10);
}

static void br_ID3D11DeviceContext_RSGetState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11RasterizerState *t1 = NULL;
    self->lpVtbl->RSGetState(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 27);
}

static void br_ID3D11DeviceContext_RSGetViewports(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    D3D11_VIEWPORT * a2 = (D3D11_VIEWPORT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->RSGetViewports(self, a1, a2);
}

static void br_ID3D11DeviceContext_RSGetScissorRects(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT * a1 = (UINT *)(uintptr_t)rcn_arg(ctx, 1);
    D3D11_RECT * a2 = (D3D11_RECT *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->RSGetScissorRects(self, a1, a2);
}

static void br_ID3D11DeviceContext_HSGetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->HSGetShaderResources(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 14);
}

static void br_ID3D11DeviceContext_HSGetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11HullShader *t1 = NULL;
    u64 g2 = rcn_arg(ctx, 2); ID3D11ClassInstance *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->HSGetShader(self, g1 ? &t1 : NULL, g2 ? &t2 : NULL, a3);
    if (g1) br_out(ctx, g1, t1, 21);
    if (g2) br_out(ctx, g2, t2, 34);
}

static void br_ID3D11DeviceContext_HSGetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->HSGetSamplers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 28);
}

static void br_ID3D11DeviceContext_HSGetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->HSGetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_DSGetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->DSGetShaderResources(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 14);
}

static void br_ID3D11DeviceContext_DSGetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11DomainShader *t1 = NULL;
    u64 g2 = rcn_arg(ctx, 2); ID3D11ClassInstance *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->DSGetShader(self, g1 ? &t1 : NULL, g2 ? &t2 : NULL, a3);
    if (g1) br_out(ctx, g1, t1, 22);
    if (g2) br_out(ctx, g2, t2, 34);
}

static void br_ID3D11DeviceContext_DSGetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->DSGetSamplers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 28);
}

static void br_ID3D11DeviceContext_DSGetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->DSGetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_CSGetShaderResources(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11ShaderResourceView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->CSGetShaderResources(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 14);
}

static void br_ID3D11DeviceContext_CSGetUnorderedAccessViews(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11UnorderedAccessView *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->CSGetUnorderedAccessViews(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 17);
}

static void br_ID3D11DeviceContext_CSGetShader(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11ComputeShader *t1 = NULL;
    u64 g2 = rcn_arg(ctx, 2); ID3D11ClassInstance *t2 = NULL;
    UINT * a3 = (UINT *)(uintptr_t)rcn_arg(ctx, 3);
    self->lpVtbl->CSGetShader(self, g1 ? &t1 : NULL, g2 ? &t2 : NULL, a3);
    if (g1) br_out(ctx, g1, t1, 23);
    if (g2) br_out(ctx, g2, t2, 34);
}

static void br_ID3D11DeviceContext_CSGetSamplers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11SamplerState *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->CSGetSamplers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 28);
}

static void br_ID3D11DeviceContext_CSGetConstantBuffers(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    self->lpVtbl->CSGetConstantBuffers(self, a1, a2, g3 ? t3 : NULL);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext_ClearState(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->ClearState(self);
}

static void br_ID3D11DeviceContext_Flush(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->Flush(self);
}

static void br_ID3D11DeviceContext_GetType(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_DEVICE_CONTEXT_TYPE r = self->lpVtbl->GetType(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext_GetContextFlags(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetContextFlags(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext_FinishCommandList(RcnCtx *ctx) {
    ID3D11DeviceContext *self = (ID3D11DeviceContext *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)rcn_arg(ctx, 1);
    u64 g2 = rcn_arg(ctx, 2); ID3D11CommandList *t2 = NULL;
    HRESULT r = self->lpVtbl->FinishCommandList(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, 32);
    if (FAILED(r)) br_failed("ID3D11DeviceContext::FinishCommandList", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext1_CopySubresourceRegion1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    ID3D11Resource * a6 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 6));
    UINT a7 = (UINT)rcn_arg(ctx, 7);
    const D3D11_BOX * a8 = (const D3D11_BOX *)(uintptr_t)rcn_arg(ctx, 8);
    UINT a9 = (UINT)rcn_arg(ctx, 9);
    self->lpVtbl->CopySubresourceRegion1(self, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

static void br_ID3D11DeviceContext1_UpdateSubresource1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const D3D11_BOX * a3 = (const D3D11_BOX *)(uintptr_t)rcn_arg(ctx, 3);
    const void * a4 = (const void *)(uintptr_t)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    UINT a7 = (UINT)rcn_arg(ctx, 7);
    self->lpVtbl->UpdateSubresource1(self, a1, a2, a3, a4, a5, a6, a7);
}

static void br_ID3D11DeviceContext1_DiscardResource(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->DiscardResource(self, a1);
}

static void br_ID3D11DeviceContext1_DiscardView(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11View * a1 = (ID3D11View *)br_unwrap(rcn_arg(ctx, 1));
    self->lpVtbl->DiscardView(self, a1);
}

static void br_ID3D11DeviceContext1_VSSetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->VSSetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext1_HSSetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->HSSetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext1_DSSetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->DSSetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext1_GSSetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->GSSetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext1_PSSetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->PSSetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext1_CSSetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY];
    for (u32 i = 0; i < n3; i++) t3[i] = g3 ? (ID3D11Buffer *)br_unwrap(rd64(g3 + 8ull * i)) : NULL;
    const UINT * a4 = (const UINT *)(uintptr_t)rcn_arg(ctx, 4);
    const UINT * a5 = (const UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->CSSetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
}

static void br_ID3D11DeviceContext1_VSGetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->VSGetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext1_HSGetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->HSGetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext1_DSGetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->DSGetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext1_GSGetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->GSGetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext1_PSGetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->PSGetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext1_CSGetConstantBuffers1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); u32 n3 = (u32)rcn_arg(ctx, 2); if (n3 > BR_MAX_ARRAY) n3 = 0;
    ID3D11Buffer *t3[BR_MAX_ARRAY] = {0};
    UINT * a4 = (UINT *)(uintptr_t)rcn_arg(ctx, 4);
    UINT * a5 = (UINT *)(uintptr_t)rcn_arg(ctx, 5);
    self->lpVtbl->CSGetConstantBuffers1(self, a1, a2, g3 ? t3 : NULL, a4, a5);
    for (u32 i = 0; g3 && i < n3; i++) br_out(ctx, g3 + 8ull * i, t3[i], 10);
}

static void br_ID3D11DeviceContext1_SwapDeviceContextState(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3DDeviceContextState * a1 = (ID3DDeviceContextState *)br_unwrap(rcn_arg(ctx, 1));
    u64 g2 = rcn_arg(ctx, 2); ID3DDeviceContextState *t2 = NULL;
    self->lpVtbl->SwapDeviceContextState(self, a1, g2 ? &t2 : NULL);
    if (g2) br_out(ctx, g2, t2, BR_FAM_DYNAMIC);
}

static void br_ID3D11DeviceContext1_ClearView(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11View * a1 = (ID3D11View *)br_unwrap(rcn_arg(ctx, 1));
    const FLOAT * a2 = (const FLOAT *)(uintptr_t)rcn_arg(ctx, 2);
    const D3D11_RECT * a3 = (const D3D11_RECT *)(uintptr_t)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    self->lpVtbl->ClearView(self, a1, a2, a3, a4);
}

static void br_ID3D11DeviceContext1_DiscardView1(RcnCtx *ctx) {
    ID3D11DeviceContext1 *self = (ID3D11DeviceContext1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11View * a1 = (ID3D11View *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_RECT * a2 = (const D3D11_RECT *)(uintptr_t)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    self->lpVtbl->DiscardView1(self, a1, a2, a3);
}

static void br_ID3D11DeviceContext2_UpdateTileMappings(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    const D3D11_TILED_RESOURCE_COORDINATE * a3 = (const D3D11_TILED_RESOURCE_COORDINATE *)(uintptr_t)rcn_arg(ctx, 3);
    const D3D11_TILE_REGION_SIZE * a4 = (const D3D11_TILE_REGION_SIZE *)(uintptr_t)rcn_arg(ctx, 4);
    ID3D11Buffer * a5 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 5));
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    const UINT * a7 = (const UINT *)(uintptr_t)rcn_arg(ctx, 7);
    const UINT * a8 = (const UINT *)(uintptr_t)rcn_arg(ctx, 8);
    const UINT * a9 = (const UINT *)(uintptr_t)rcn_arg(ctx, 9);
    UINT a10 = (UINT)rcn_arg(ctx, 10);
    HRESULT r = self->lpVtbl->UpdateTileMappings(self, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    if (FAILED(r)) br_failed("ID3D11DeviceContext2::UpdateTileMappings", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext2_CopyTileMappings(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_TILED_RESOURCE_COORDINATE * a2 = (const D3D11_TILED_RESOURCE_COORDINATE *)(uintptr_t)rcn_arg(ctx, 2);
    ID3D11Resource * a3 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 3));
    const D3D11_TILED_RESOURCE_COORDINATE * a4 = (const D3D11_TILED_RESOURCE_COORDINATE *)(uintptr_t)rcn_arg(ctx, 4);
    const D3D11_TILE_REGION_SIZE * a5 = (const D3D11_TILE_REGION_SIZE *)(uintptr_t)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    HRESULT r = self->lpVtbl->CopyTileMappings(self, a1, a2, a3, a4, a5, a6);
    if (FAILED(r)) br_failed("ID3D11DeviceContext2::CopyTileMappings", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext2_CopyTiles(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_TILED_RESOURCE_COORDINATE * a2 = (const D3D11_TILED_RESOURCE_COORDINATE *)(uintptr_t)rcn_arg(ctx, 2);
    const D3D11_TILE_REGION_SIZE * a3 = (const D3D11_TILE_REGION_SIZE *)(uintptr_t)rcn_arg(ctx, 3);
    ID3D11Buffer * a4 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 4));
    UINT64 a5 = (UINT64)rcn_arg(ctx, 5);
    UINT a6 = (UINT)rcn_arg(ctx, 6);
    self->lpVtbl->CopyTiles(self, a1, a2, a3, a4, a5, a6);
}

static void br_ID3D11DeviceContext2_UpdateTiles(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Resource * a1 = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 1));
    const D3D11_TILED_RESOURCE_COORDINATE * a2 = (const D3D11_TILED_RESOURCE_COORDINATE *)(uintptr_t)rcn_arg(ctx, 2);
    const D3D11_TILE_REGION_SIZE * a3 = (const D3D11_TILE_REGION_SIZE *)(uintptr_t)rcn_arg(ctx, 3);
    const void * a4 = (const void *)(uintptr_t)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    self->lpVtbl->UpdateTiles(self, a1, a2, a3, a4, a5);
}

static void br_ID3D11DeviceContext2_ResizeTilePool(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Buffer * a1 = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 1));
    UINT64 a2 = (UINT64)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->ResizeTilePool(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11DeviceContext2::ResizeTilePool", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext2_TiledResourceBarrier(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11DeviceChild * a1 = (ID3D11DeviceChild *)br_unwrap(rcn_arg(ctx, 1));
    ID3D11DeviceChild * a2 = (ID3D11DeviceChild *)br_unwrap(rcn_arg(ctx, 2));
    self->lpVtbl->TiledResourceBarrier(self, a1, a2);
}

static void br_ID3D11DeviceContext2_IsAnnotationEnabled(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->IsAnnotationEnabled(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext2_SetMarkerInt(RcnCtx *ctx) {
    br_unsupported(ctx, "ID3D11DeviceContext2::SetMarkerInt");
}

static void br_ID3D11DeviceContext2_BeginEventInt(RcnCtx *ctx) {
    br_unsupported(ctx, "ID3D11DeviceContext2::BeginEventInt");
}

static void br_ID3D11DeviceContext2_EndEvent(RcnCtx *ctx) {
    ID3D11DeviceContext2 *self = (ID3D11DeviceContext2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->EndEvent(self);
}

static void br_ID3D11DeviceContext3_Flush1(RcnCtx *ctx) {
    ID3D11DeviceContext3 *self = (ID3D11DeviceContext3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_CONTEXT_TYPE a1 = (D3D11_CONTEXT_TYPE)rcn_arg(ctx, 1);
    HANDLE a2 = (HANDLE)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->Flush1(self, a1, a2);
}

static void br_ID3D11DeviceContext3_SetHardwareProtectionState(RcnCtx *ctx) {
    ID3D11DeviceContext3 *self = (ID3D11DeviceContext3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)rcn_arg(ctx, 1);
    self->lpVtbl->SetHardwareProtectionState(self, a1);
}

static void br_ID3D11DeviceContext3_GetHardwareProtectionState(RcnCtx *ctx) {
    ID3D11DeviceContext3 *self = (ID3D11DeviceContext3 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL * a1 = (WINBOOL *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetHardwareProtectionState(self, a1);
}

static void br_ID3D11DeviceContext4_Signal(RcnCtx *ctx) {
    ID3D11DeviceContext4 *self = (ID3D11DeviceContext4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Fence * a1 = (ID3D11Fence *)br_unwrap(rcn_arg(ctx, 1));
    UINT64 a2 = (UINT64)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->Signal(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11DeviceContext4::Signal", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11DeviceContext4_Wait(RcnCtx *ctx) {
    ID3D11DeviceContext4 *self = (ID3D11DeviceContext4 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    ID3D11Fence * a1 = (ID3D11Fence *)br_unwrap(rcn_arg(ctx, 1));
    UINT64 a2 = (UINT64)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->Wait(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11DeviceContext4::Wait", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Resource_GetType(RcnCtx *ctx) {
    ID3D11Resource *self = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_RESOURCE_DIMENSION * a1 = (D3D11_RESOURCE_DIMENSION *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetType(self, a1);
}

static void br_ID3D11Resource_SetEvictionPriority(RcnCtx *ctx) {
    ID3D11Resource *self = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT a1 = (UINT)rcn_arg(ctx, 1);
    self->lpVtbl->SetEvictionPriority(self, a1);
}

static void br_ID3D11Resource_GetEvictionPriority(RcnCtx *ctx) {
    ID3D11Resource *self = (ID3D11Resource *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetEvictionPriority(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11Buffer_GetDesc(RcnCtx *ctx) {
    ID3D11Buffer *self = (ID3D11Buffer *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_BUFFER_DESC * a1 = (D3D11_BUFFER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11Texture1D_GetDesc(RcnCtx *ctx) {
    ID3D11Texture1D *self = (ID3D11Texture1D *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_TEXTURE1D_DESC * a1 = (D3D11_TEXTURE1D_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11Texture2D_GetDesc(RcnCtx *ctx) {
    ID3D11Texture2D *self = (ID3D11Texture2D *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_TEXTURE2D_DESC * a1 = (D3D11_TEXTURE2D_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11Texture2D1_GetDesc1(RcnCtx *ctx) {
    ID3D11Texture2D1 *self = (ID3D11Texture2D1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_TEXTURE2D_DESC1 * a1 = (D3D11_TEXTURE2D_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11Texture3D_GetDesc(RcnCtx *ctx) {
    ID3D11Texture3D *self = (ID3D11Texture3D *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_TEXTURE3D_DESC * a1 = (D3D11_TEXTURE3D_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11Texture3D1_GetDesc1(RcnCtx *ctx) {
    ID3D11Texture3D1 *self = (ID3D11Texture3D1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_TEXTURE3D_DESC1 * a1 = (D3D11_TEXTURE3D_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11View_GetResource(RcnCtx *ctx) {
    ID3D11View *self = (ID3D11View *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11Resource *t1 = NULL;
    self->lpVtbl->GetResource(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 10);
}

static void br_ID3D11ShaderResourceView_GetDesc(RcnCtx *ctx) {
    ID3D11ShaderResourceView *self = (ID3D11ShaderResourceView *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_SHADER_RESOURCE_VIEW_DESC * a1 = (D3D11_SHADER_RESOURCE_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11ShaderResourceView1_GetDesc1(RcnCtx *ctx) {
    ID3D11ShaderResourceView1 *self = (ID3D11ShaderResourceView1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_SHADER_RESOURCE_VIEW_DESC1 * a1 = (D3D11_SHADER_RESOURCE_VIEW_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11RenderTargetView_GetDesc(RcnCtx *ctx) {
    ID3D11RenderTargetView *self = (ID3D11RenderTargetView *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_RENDER_TARGET_VIEW_DESC * a1 = (D3D11_RENDER_TARGET_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11RenderTargetView1_GetDesc1(RcnCtx *ctx) {
    ID3D11RenderTargetView1 *self = (ID3D11RenderTargetView1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_RENDER_TARGET_VIEW_DESC1 * a1 = (D3D11_RENDER_TARGET_VIEW_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11DepthStencilView_GetDesc(RcnCtx *ctx) {
    ID3D11DepthStencilView *self = (ID3D11DepthStencilView *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_DEPTH_STENCIL_VIEW_DESC * a1 = (D3D11_DEPTH_STENCIL_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11UnorderedAccessView_GetDesc(RcnCtx *ctx) {
    ID3D11UnorderedAccessView *self = (ID3D11UnorderedAccessView *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC * a1 = (D3D11_UNORDERED_ACCESS_VIEW_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11UnorderedAccessView1_GetDesc1(RcnCtx *ctx) {
    ID3D11UnorderedAccessView1 *self = (ID3D11UnorderedAccessView1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 * a1 = (D3D11_UNORDERED_ACCESS_VIEW_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11BlendState_GetDesc(RcnCtx *ctx) {
    ID3D11BlendState *self = (ID3D11BlendState *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_BLEND_DESC * a1 = (D3D11_BLEND_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11BlendState1_GetDesc1(RcnCtx *ctx) {
    ID3D11BlendState1 *self = (ID3D11BlendState1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_BLEND_DESC1 * a1 = (D3D11_BLEND_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11DepthStencilState_GetDesc(RcnCtx *ctx) {
    ID3D11DepthStencilState *self = (ID3D11DepthStencilState *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_DEPTH_STENCIL_DESC * a1 = (D3D11_DEPTH_STENCIL_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11RasterizerState_GetDesc(RcnCtx *ctx) {
    ID3D11RasterizerState *self = (ID3D11RasterizerState *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_RASTERIZER_DESC * a1 = (D3D11_RASTERIZER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11RasterizerState1_GetDesc1(RcnCtx *ctx) {
    ID3D11RasterizerState1 *self = (ID3D11RasterizerState1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_RASTERIZER_DESC1 * a1 = (D3D11_RASTERIZER_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11RasterizerState2_GetDesc2(RcnCtx *ctx) {
    ID3D11RasterizerState2 *self = (ID3D11RasterizerState2 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_RASTERIZER_DESC2 * a1 = (D3D11_RASTERIZER_DESC2 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc2(self, a1);
}

static void br_ID3D11SamplerState_GetDesc(RcnCtx *ctx) {
    ID3D11SamplerState *self = (ID3D11SamplerState *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_SAMPLER_DESC * a1 = (D3D11_SAMPLER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11Asynchronous_GetDataSize(RcnCtx *ctx) {
    ID3D11Asynchronous *self = (ID3D11Asynchronous *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetDataSize(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11Query_GetDesc(RcnCtx *ctx) {
    ID3D11Query *self = (ID3D11Query *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_QUERY_DESC * a1 = (D3D11_QUERY_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11Query1_GetDesc1(RcnCtx *ctx) {
    ID3D11Query1 *self = (ID3D11Query1 *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_QUERY_DESC1 * a1 = (D3D11_QUERY_DESC1 *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc1(self, a1);
}

static void br_ID3D11Counter_GetDesc(RcnCtx *ctx) {
    ID3D11Counter *self = (ID3D11Counter *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_COUNTER_DESC * a1 = (D3D11_COUNTER_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11CommandList_GetContextFlags(RcnCtx *ctx) {
    ID3D11CommandList *self = (ID3D11CommandList *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT r = self->lpVtbl->GetContextFlags(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11ClassLinkage_GetClassInstance(RcnCtx *ctx) {
    ID3D11ClassLinkage *self = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    LPCSTR a1 = (LPCSTR)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    u64 g3 = rcn_arg(ctx, 3); ID3D11ClassInstance *t3 = NULL;
    HRESULT r = self->lpVtbl->GetClassInstance(self, a1, a2, g3 ? &t3 : NULL);
    if (g3) br_out(ctx, g3, t3, 34);
    if (FAILED(r)) br_failed("ID3D11ClassLinkage::GetClassInstance", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11ClassLinkage_CreateClassInstance(RcnCtx *ctx) {
    ID3D11ClassLinkage *self = (ID3D11ClassLinkage *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    LPCSTR a1 = (LPCSTR)rcn_arg(ctx, 1);
    UINT a2 = (UINT)rcn_arg(ctx, 2);
    UINT a3 = (UINT)rcn_arg(ctx, 3);
    UINT a4 = (UINT)rcn_arg(ctx, 4);
    UINT a5 = (UINT)rcn_arg(ctx, 5);
    u64 g6 = rcn_arg(ctx, 6); ID3D11ClassInstance *t6 = NULL;
    HRESULT r = self->lpVtbl->CreateClassInstance(self, a1, a2, a3, a4, a5, g6 ? &t6 : NULL);
    if (g6) br_out(ctx, g6, t6, 34);
    if (FAILED(r)) br_failed("ID3D11ClassLinkage::CreateClassInstance", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11ClassInstance_GetClassLinkage(RcnCtx *ctx) {
    ID3D11ClassInstance *self = (ID3D11ClassInstance *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    u64 g1 = rcn_arg(ctx, 1); ID3D11ClassLinkage *t1 = NULL;
    self->lpVtbl->GetClassLinkage(self, g1 ? &t1 : NULL);
    if (g1) br_out(ctx, g1, t1, 33);
}

static void br_ID3D11ClassInstance_GetDesc(RcnCtx *ctx) {
    ID3D11ClassInstance *self = (ID3D11ClassInstance *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    D3D11_CLASS_INSTANCE_DESC * a1 = (D3D11_CLASS_INSTANCE_DESC *)(uintptr_t)rcn_arg(ctx, 1);
    self->lpVtbl->GetDesc(self, a1);
}

static void br_ID3D11ClassInstance_GetInstanceName(RcnCtx *ctx) {
    ID3D11ClassInstance *self = (ID3D11ClassInstance *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    LPSTR a1 = (LPSTR)rcn_arg(ctx, 1);
    SIZE_T * a2 = (SIZE_T *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->GetInstanceName(self, a1, a2);
}

static void br_ID3D11ClassInstance_GetTypeName(RcnCtx *ctx) {
    ID3D11ClassInstance *self = (ID3D11ClassInstance *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    LPSTR a1 = (LPSTR)rcn_arg(ctx, 1);
    SIZE_T * a2 = (SIZE_T *)(uintptr_t)rcn_arg(ctx, 2);
    self->lpVtbl->GetTypeName(self, a1, a2);
}

static void br_ID3D11Fence_CreateSharedHandle(RcnCtx *ctx) {
    br_unsupported(ctx, "ID3D11Fence::CreateSharedHandle");
}

static void br_ID3D11Fence_GetCompletedValue(RcnCtx *ctx) {
    ID3D11Fence *self = (ID3D11Fence *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT64 r = self->lpVtbl->GetCompletedValue(self);
    RET((u64)(i64)(r));
}

static void br_ID3D11Fence_SetEventOnCompletion(RcnCtx *ctx) {
    ID3D11Fence *self = (ID3D11Fence *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    UINT64 a1 = (UINT64)rcn_arg(ctx, 1);
    HANDLE a2 = (HANDLE)(uintptr_t)rcn_arg(ctx, 2);
    HRESULT r = self->lpVtbl->SetEventOnCompletion(self, a1, a2);
    if (FAILED(r)) br_failed("ID3D11Fence::SetEventOnCompletion", r);
    RET((u64)(i64)(r));
}

static void br_ID3D11Multithread_Enter(RcnCtx *ctx) {
    ID3D11Multithread *self = (ID3D11Multithread *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->Enter(self);
}

static void br_ID3D11Multithread_Leave(RcnCtx *ctx) {
    ID3D11Multithread *self = (ID3D11Multithread *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    self->lpVtbl->Leave(self);
}

static void br_ID3D11Multithread_SetMultithreadProtected(RcnCtx *ctx) {
    ID3D11Multithread *self = (ID3D11Multithread *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL a1 = (WINBOOL)rcn_arg(ctx, 1);
    WINBOOL r = self->lpVtbl->SetMultithreadProtected(self, a1);
    RET((u64)(i64)(r));
}

static void br_ID3D11Multithread_GetMultithreadProtected(RcnCtx *ctx) {
    ID3D11Multithread *self = (ID3D11Multithread *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->GetMultithreadProtected(self);
    RET((u64)(i64)(r));
}

static void br_ID3DUserDefinedAnnotation_EndEvent(RcnCtx *ctx) {
    ID3DUserDefinedAnnotation *self = (ID3DUserDefinedAnnotation *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    INT r = self->lpVtbl->EndEvent(self);
    RET((u64)(i64)(r));
}

static void br_ID3DUserDefinedAnnotation_GetStatus(RcnCtx *ctx) {
    ID3DUserDefinedAnnotation *self = (ID3DUserDefinedAnnotation *)br_unwrap(rcn_arg(ctx, 0));
    if (!self) { RET((u64)(i64)(i32)E_POINTER); return; }
    WINBOOL r = self->lpVtbl->GetStatus(self);
    RET((u64)(i64)(r));
}

static void br_special_AddRef(RcnCtx *ctx);
static void br_special_ID3DUserDefinedAnnotation_BeginEvent(RcnCtx *ctx);
static void br_special_ID3DUserDefinedAnnotation_SetMarker(RcnCtx *ctx);
static void br_special_IDXGIAdapter1_GetDesc1(RcnCtx *ctx);
static void br_special_IDXGIAdapter2_GetDesc2(RcnCtx *ctx);
static void br_special_IDXGIAdapter4_GetDesc3(RcnCtx *ctx);
static void br_special_IDXGIAdapter_GetDesc(RcnCtx *ctx);
static void br_special_IDXGIOutput6_GetDesc1(RcnCtx *ctx);
static void br_special_IDXGIOutput_GetDesc(RcnCtx *ctx);
static void br_special_IDXGIOutput_GetFrameStatistics(RcnCtx *ctx);
static void br_special_IDXGISwapChain_GetFrameStatistics(RcnCtx *ctx);
static void br_special_QueryInterface(RcnCtx *ctx);
static void br_special_Release(RcnCtx *ctx);
static const BrMethod br_methods_IDXGIFactory7[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"EnumAdapters", br_IDXGIFactory_EnumAdapters},
    {"MakeWindowAssociation", br_IDXGIFactory_MakeWindowAssociation},
    {"GetWindowAssociation", br_IDXGIFactory_GetWindowAssociation},
    {"CreateSwapChain", br_IDXGIFactory_CreateSwapChain},
    {"CreateSoftwareAdapter", br_IDXGIFactory_CreateSoftwareAdapter},
    {"EnumAdapters1", br_IDXGIFactory1_EnumAdapters1},
    {"IsCurrent", br_IDXGIFactory1_IsCurrent},
    {"IsWindowedStereoEnabled", br_IDXGIFactory2_IsWindowedStereoEnabled},
    {"CreateSwapChainForHwnd", br_IDXGIFactory2_CreateSwapChainForHwnd},
    {"CreateSwapChainForCoreWindow", br_IDXGIFactory2_CreateSwapChainForCoreWindow},
    {"GetSharedResourceAdapterLuid", br_IDXGIFactory2_GetSharedResourceAdapterLuid},
    {"RegisterStereoStatusWindow", br_IDXGIFactory2_RegisterStereoStatusWindow},
    {"RegisterStereoStatusEvent", br_IDXGIFactory2_RegisterStereoStatusEvent},
    {"UnregisterStereoStatus", br_IDXGIFactory2_UnregisterStereoStatus},
    {"RegisterOcclusionStatusWindow", br_IDXGIFactory2_RegisterOcclusionStatusWindow},
    {"RegisterOcclusionStatusEvent", br_IDXGIFactory2_RegisterOcclusionStatusEvent},
    {"UnregisterOcclusionStatus", br_IDXGIFactory2_UnregisterOcclusionStatus},
    {"CreateSwapChainForComposition", br_IDXGIFactory2_CreateSwapChainForComposition},
    {"GetCreationFlags", br_IDXGIFactory3_GetCreationFlags},
    {"EnumAdapterByLuid", br_IDXGIFactory4_EnumAdapterByLuid},
    {"EnumWarpAdapter", br_IDXGIFactory4_EnumWarpAdapter},
    {"CheckFeatureSupport", br_IDXGIFactory5_CheckFeatureSupport},
    {"EnumAdapterByGpuPreference", br_IDXGIFactory6_EnumAdapterByGpuPreference},
    {"RegisterAdaptersChangedEvent", br_IDXGIFactory7_RegisterAdaptersChangedEvent},
    {"UnregisterAdaptersChangedEvent", br_IDXGIFactory7_UnregisterAdaptersChangedEvent},
};
static const BrMethod br_methods_IDXGIAdapter4[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"EnumOutputs", br_IDXGIAdapter_EnumOutputs},
    {"GetDesc", br_special_IDXGIAdapter_GetDesc},
    {"CheckInterfaceSupport", br_IDXGIAdapter_CheckInterfaceSupport},
    {"GetDesc1", br_special_IDXGIAdapter1_GetDesc1},
    {"GetDesc2", br_special_IDXGIAdapter2_GetDesc2},
    {"RegisterHardwareContentProtectionTeardownStatusEvent", br_IDXGIAdapter3_RegisterHardwareContentProtectionTeardownStatusEvent},
    {"UnregisterHardwareContentProtectionTeardownStatus", br_IDXGIAdapter3_UnregisterHardwareContentProtectionTeardownStatus},
    {"QueryVideoMemoryInfo", br_IDXGIAdapter3_QueryVideoMemoryInfo},
    {"SetVideoMemoryReservation", br_IDXGIAdapter3_SetVideoMemoryReservation},
    {"RegisterVideoMemoryBudgetChangeNotificationEvent", br_IDXGIAdapter3_RegisterVideoMemoryBudgetChangeNotificationEvent},
    {"UnregisterVideoMemoryBudgetChangeNotification", br_IDXGIAdapter3_UnregisterVideoMemoryBudgetChangeNotification},
    {"GetDesc3", br_special_IDXGIAdapter4_GetDesc3},
};
static const BrMethod br_methods_IDXGIOutput6[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"GetDesc", br_special_IDXGIOutput_GetDesc},
    {"GetDisplayModeList", br_IDXGIOutput_GetDisplayModeList},
    {"FindClosestMatchingMode", br_IDXGIOutput_FindClosestMatchingMode},
    {"WaitForVBlank", br_IDXGIOutput_WaitForVBlank},
    {"TakeOwnership", br_IDXGIOutput_TakeOwnership},
    {"ReleaseOwnership", br_IDXGIOutput_ReleaseOwnership},
    {"GetGammaControlCapabilities", br_IDXGIOutput_GetGammaControlCapabilities},
    {"SetGammaControl", br_IDXGIOutput_SetGammaControl},
    {"GetGammaControl", br_IDXGIOutput_GetGammaControl},
    {"SetDisplaySurface", br_IDXGIOutput_SetDisplaySurface},
    {"GetDisplaySurfaceData", br_IDXGIOutput_GetDisplaySurfaceData},
    {"GetFrameStatistics", br_special_IDXGIOutput_GetFrameStatistics},
    {"GetDisplayModeList1", br_IDXGIOutput1_GetDisplayModeList1},
    {"FindClosestMatchingMode1", br_IDXGIOutput1_FindClosestMatchingMode1},
    {"GetDisplaySurfaceData1", br_IDXGIOutput1_GetDisplaySurfaceData1},
    {"DuplicateOutput", br_IDXGIOutput1_DuplicateOutput},
    {"SupportsOverlays", br_IDXGIOutput2_SupportsOverlays},
    {"CheckOverlaySupport", br_IDXGIOutput3_CheckOverlaySupport},
    {"CheckOverlayColorSpaceSupport", br_IDXGIOutput4_CheckOverlayColorSpaceSupport},
    {"DuplicateOutput1", br_IDXGIOutput5_DuplicateOutput1},
    {"GetDesc1", br_special_IDXGIOutput6_GetDesc1},
    {"CheckHardwareCompositionSupport", br_IDXGIOutput6_CheckHardwareCompositionSupport},
};
static const BrMethod br_methods_IDXGISwapChain4[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"GetDevice", br_IDXGIDeviceSubObject_GetDevice},
    {"Present", br_IDXGISwapChain_Present},
    {"GetBuffer", br_IDXGISwapChain_GetBuffer},
    {"SetFullscreenState", br_IDXGISwapChain_SetFullscreenState},
    {"GetFullscreenState", br_IDXGISwapChain_GetFullscreenState},
    {"GetDesc", br_IDXGISwapChain_GetDesc},
    {"ResizeBuffers", br_IDXGISwapChain_ResizeBuffers},
    {"ResizeTarget", br_IDXGISwapChain_ResizeTarget},
    {"GetContainingOutput", br_IDXGISwapChain_GetContainingOutput},
    {"GetFrameStatistics", br_special_IDXGISwapChain_GetFrameStatistics},
    {"GetLastPresentCount", br_IDXGISwapChain_GetLastPresentCount},
    {"GetDesc1", br_IDXGISwapChain1_GetDesc1},
    {"GetFullscreenDesc", br_IDXGISwapChain1_GetFullscreenDesc},
    {"GetHwnd", br_IDXGISwapChain1_GetHwnd},
    {"GetCoreWindow", br_IDXGISwapChain1_GetCoreWindow},
    {"Present1", br_IDXGISwapChain1_Present1},
    {"IsTemporaryMonoSupported", br_IDXGISwapChain1_IsTemporaryMonoSupported},
    {"GetRestrictToOutput", br_IDXGISwapChain1_GetRestrictToOutput},
    {"SetBackgroundColor", br_IDXGISwapChain1_SetBackgroundColor},
    {"GetBackgroundColor", br_IDXGISwapChain1_GetBackgroundColor},
    {"SetRotation", br_IDXGISwapChain1_SetRotation},
    {"GetRotation", br_IDXGISwapChain1_GetRotation},
    {"SetSourceSize", br_IDXGISwapChain2_SetSourceSize},
    {"GetSourceSize", br_IDXGISwapChain2_GetSourceSize},
    {"SetMaximumFrameLatency", br_IDXGISwapChain2_SetMaximumFrameLatency},
    {"GetMaximumFrameLatency", br_IDXGISwapChain2_GetMaximumFrameLatency},
    {"GetFrameLatencyWaitableObject", br_IDXGISwapChain2_GetFrameLatencyWaitableObject},
    {"SetMatrixTransform", br_IDXGISwapChain2_SetMatrixTransform},
    {"GetMatrixTransform", br_IDXGISwapChain2_GetMatrixTransform},
    {"GetCurrentBackBufferIndex", br_IDXGISwapChain3_GetCurrentBackBufferIndex},
    {"CheckColorSpaceSupport", br_IDXGISwapChain3_CheckColorSpaceSupport},
    {"SetColorSpace1", br_IDXGISwapChain3_SetColorSpace1},
    {"ResizeBuffers1", br_IDXGISwapChain3_ResizeBuffers1},
    {"SetHDRMetaData", br_IDXGISwapChain4_SetHDRMetaData},
};
static const BrMethod br_methods_IDXGIDevice4[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"GetAdapter", br_IDXGIDevice_GetAdapter},
    {"CreateSurface", br_IDXGIDevice_CreateSurface},
    {"QueryResourceResidency", br_IDXGIDevice_QueryResourceResidency},
    {"SetGPUThreadPriority", br_IDXGIDevice_SetGPUThreadPriority},
    {"GetGPUThreadPriority", br_IDXGIDevice_GetGPUThreadPriority},
    {"SetMaximumFrameLatency", br_IDXGIDevice1_SetMaximumFrameLatency},
    {"GetMaximumFrameLatency", br_IDXGIDevice1_GetMaximumFrameLatency},
    {"OfferResources", br_IDXGIDevice2_OfferResources},
    {"ReclaimResources", br_IDXGIDevice2_ReclaimResources},
    {"EnqueueSetEvent", br_IDXGIDevice2_EnqueueSetEvent},
    {"Trim", br_IDXGIDevice3_Trim},
    {"OfferResources1", br_IDXGIDevice4_OfferResources1},
    {"ReclaimResources1", br_IDXGIDevice4_ReclaimResources1},
};
static const BrMethod br_methods_IDXGISurface2[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"GetDevice", br_IDXGIDeviceSubObject_GetDevice},
    {"GetDesc", br_IDXGISurface_GetDesc},
    {"Map", br_IDXGISurface_Map},
    {"Unmap", br_IDXGISurface_Unmap},
    {"GetDC", br_IDXGISurface1_GetDC},
    {"ReleaseDC", br_IDXGISurface1_ReleaseDC},
    {"GetResource", br_IDXGISurface2_GetResource},
};
static const BrMethod br_methods_IDXGIResource1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"GetDevice", br_IDXGIDeviceSubObject_GetDevice},
    {"GetSharedHandle", br_IDXGIResource_GetSharedHandle},
    {"GetUsage", br_IDXGIResource_GetUsage},
    {"SetEvictionPriority", br_IDXGIResource_SetEvictionPriority},
    {"GetEvictionPriority", br_IDXGIResource_GetEvictionPriority},
    {"CreateSubresourceSurface", br_IDXGIResource1_CreateSubresourceSurface},
    {"CreateSharedHandle", br_IDXGIResource1_CreateSharedHandle},
};
static const BrMethod br_methods_IDXGIKeyedMutex[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"SetPrivateData", br_IDXGIObject_SetPrivateData},
    {"SetPrivateDataInterface", br_IDXGIObject_SetPrivateDataInterface},
    {"GetPrivateData", br_IDXGIObject_GetPrivateData},
    {"GetParent", br_IDXGIObject_GetParent},
    {"GetDevice", br_IDXGIDeviceSubObject_GetDevice},
    {"AcquireSync", br_IDXGIKeyedMutex_AcquireSync},
    {"ReleaseSync", br_IDXGIKeyedMutex_ReleaseSync},
};
static const BrMethod br_methods_ID3D11Device5[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"CreateBuffer", br_ID3D11Device_CreateBuffer},
    {"CreateTexture1D", br_ID3D11Device_CreateTexture1D},
    {"CreateTexture2D", br_ID3D11Device_CreateTexture2D},
    {"CreateTexture3D", br_ID3D11Device_CreateTexture3D},
    {"CreateShaderResourceView", br_ID3D11Device_CreateShaderResourceView},
    {"CreateUnorderedAccessView", br_ID3D11Device_CreateUnorderedAccessView},
    {"CreateRenderTargetView", br_ID3D11Device_CreateRenderTargetView},
    {"CreateDepthStencilView", br_ID3D11Device_CreateDepthStencilView},
    {"CreateInputLayout", br_ID3D11Device_CreateInputLayout},
    {"CreateVertexShader", br_ID3D11Device_CreateVertexShader},
    {"CreateGeometryShader", br_ID3D11Device_CreateGeometryShader},
    {"CreateGeometryShaderWithStreamOutput", br_ID3D11Device_CreateGeometryShaderWithStreamOutput},
    {"CreatePixelShader", br_ID3D11Device_CreatePixelShader},
    {"CreateHullShader", br_ID3D11Device_CreateHullShader},
    {"CreateDomainShader", br_ID3D11Device_CreateDomainShader},
    {"CreateComputeShader", br_ID3D11Device_CreateComputeShader},
    {"CreateClassLinkage", br_ID3D11Device_CreateClassLinkage},
    {"CreateBlendState", br_ID3D11Device_CreateBlendState},
    {"CreateDepthStencilState", br_ID3D11Device_CreateDepthStencilState},
    {"CreateRasterizerState", br_ID3D11Device_CreateRasterizerState},
    {"CreateSamplerState", br_ID3D11Device_CreateSamplerState},
    {"CreateQuery", br_ID3D11Device_CreateQuery},
    {"CreatePredicate", br_ID3D11Device_CreatePredicate},
    {"CreateCounter", br_ID3D11Device_CreateCounter},
    {"CreateDeferredContext", br_ID3D11Device_CreateDeferredContext},
    {"OpenSharedResource", br_ID3D11Device_OpenSharedResource},
    {"CheckFormatSupport", br_ID3D11Device_CheckFormatSupport},
    {"CheckMultisampleQualityLevels", br_ID3D11Device_CheckMultisampleQualityLevels},
    {"CheckCounterInfo", br_ID3D11Device_CheckCounterInfo},
    {"CheckCounter", br_ID3D11Device_CheckCounter},
    {"CheckFeatureSupport", br_ID3D11Device_CheckFeatureSupport},
    {"GetPrivateData", br_ID3D11Device_GetPrivateData},
    {"SetPrivateData", br_ID3D11Device_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11Device_SetPrivateDataInterface},
    {"GetFeatureLevel", br_ID3D11Device_GetFeatureLevel},
    {"GetCreationFlags", br_ID3D11Device_GetCreationFlags},
    {"GetDeviceRemovedReason", br_ID3D11Device_GetDeviceRemovedReason},
    {"GetImmediateContext", br_ID3D11Device_GetImmediateContext},
    {"SetExceptionMode", br_ID3D11Device_SetExceptionMode},
    {"GetExceptionMode", br_ID3D11Device_GetExceptionMode},
    {"GetImmediateContext1", br_ID3D11Device1_GetImmediateContext1},
    {"CreateDeferredContext1", br_ID3D11Device1_CreateDeferredContext1},
    {"CreateBlendState1", br_ID3D11Device1_CreateBlendState1},
    {"CreateRasterizerState1", br_ID3D11Device1_CreateRasterizerState1},
    {"CreateDeviceContextState", br_ID3D11Device1_CreateDeviceContextState},
    {"OpenSharedResource1", br_ID3D11Device1_OpenSharedResource1},
    {"OpenSharedResourceByName", br_ID3D11Device1_OpenSharedResourceByName},
    {"GetImmediateContext2", br_ID3D11Device2_GetImmediateContext2},
    {"CreateDeferredContext2", br_ID3D11Device2_CreateDeferredContext2},
    {"GetResourceTiling", br_ID3D11Device2_GetResourceTiling},
    {"CheckMultisampleQualityLevels1", br_ID3D11Device2_CheckMultisampleQualityLevels1},
    {"CreateTexture2D1", br_ID3D11Device3_CreateTexture2D1},
    {"CreateTexture3D1", br_ID3D11Device3_CreateTexture3D1},
    {"CreateRasterizerState2", br_ID3D11Device3_CreateRasterizerState2},
    {"CreateShaderResourceView1", br_ID3D11Device3_CreateShaderResourceView1},
    {"CreateUnorderedAccessView1", br_ID3D11Device3_CreateUnorderedAccessView1},
    {"CreateRenderTargetView1", br_ID3D11Device3_CreateRenderTargetView1},
    {"CreateQuery1", br_ID3D11Device3_CreateQuery1},
    {"GetImmediateContext3", br_ID3D11Device3_GetImmediateContext3},
    {"CreateDeferredContext3", br_ID3D11Device3_CreateDeferredContext3},
    {"WriteToSubresource", br_ID3D11Device3_WriteToSubresource},
    {"ReadFromSubresource", br_ID3D11Device3_ReadFromSubresource},
    {"RegisterDeviceRemovedEvent", br_ID3D11Device4_RegisterDeviceRemovedEvent},
    {"UnregisterDeviceRemoved", br_ID3D11Device4_UnregisterDeviceRemoved},
    {"OpenSharedFence", br_ID3D11Device5_OpenSharedFence},
    {"CreateFence", br_ID3D11Device5_CreateFence},
};
static const BrMethod br_methods_ID3D11DeviceContext4[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"VSSetConstantBuffers", br_ID3D11DeviceContext_VSSetConstantBuffers},
    {"PSSetShaderResources", br_ID3D11DeviceContext_PSSetShaderResources},
    {"PSSetShader", br_ID3D11DeviceContext_PSSetShader},
    {"PSSetSamplers", br_ID3D11DeviceContext_PSSetSamplers},
    {"VSSetShader", br_ID3D11DeviceContext_VSSetShader},
    {"DrawIndexed", br_ID3D11DeviceContext_DrawIndexed},
    {"Draw", br_ID3D11DeviceContext_Draw},
    {"Map", br_ID3D11DeviceContext_Map},
    {"Unmap", br_ID3D11DeviceContext_Unmap},
    {"PSSetConstantBuffers", br_ID3D11DeviceContext_PSSetConstantBuffers},
    {"IASetInputLayout", br_ID3D11DeviceContext_IASetInputLayout},
    {"IASetVertexBuffers", br_ID3D11DeviceContext_IASetVertexBuffers},
    {"IASetIndexBuffer", br_ID3D11DeviceContext_IASetIndexBuffer},
    {"DrawIndexedInstanced", br_ID3D11DeviceContext_DrawIndexedInstanced},
    {"DrawInstanced", br_ID3D11DeviceContext_DrawInstanced},
    {"GSSetConstantBuffers", br_ID3D11DeviceContext_GSSetConstantBuffers},
    {"GSSetShader", br_ID3D11DeviceContext_GSSetShader},
    {"IASetPrimitiveTopology", br_ID3D11DeviceContext_IASetPrimitiveTopology},
    {"VSSetShaderResources", br_ID3D11DeviceContext_VSSetShaderResources},
    {"VSSetSamplers", br_ID3D11DeviceContext_VSSetSamplers},
    {"Begin", br_ID3D11DeviceContext_Begin},
    {"End", br_ID3D11DeviceContext_End},
    {"GetData", br_ID3D11DeviceContext_GetData},
    {"SetPredication", br_ID3D11DeviceContext_SetPredication},
    {"GSSetShaderResources", br_ID3D11DeviceContext_GSSetShaderResources},
    {"GSSetSamplers", br_ID3D11DeviceContext_GSSetSamplers},
    {"OMSetRenderTargets", br_ID3D11DeviceContext_OMSetRenderTargets},
    {"OMSetRenderTargetsAndUnorderedAccessViews", br_ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews},
    {"OMSetBlendState", br_ID3D11DeviceContext_OMSetBlendState},
    {"OMSetDepthStencilState", br_ID3D11DeviceContext_OMSetDepthStencilState},
    {"SOSetTargets", br_ID3D11DeviceContext_SOSetTargets},
    {"DrawAuto", br_ID3D11DeviceContext_DrawAuto},
    {"DrawIndexedInstancedIndirect", br_ID3D11DeviceContext_DrawIndexedInstancedIndirect},
    {"DrawInstancedIndirect", br_ID3D11DeviceContext_DrawInstancedIndirect},
    {"Dispatch", br_ID3D11DeviceContext_Dispatch},
    {"DispatchIndirect", br_ID3D11DeviceContext_DispatchIndirect},
    {"RSSetState", br_ID3D11DeviceContext_RSSetState},
    {"RSSetViewports", br_ID3D11DeviceContext_RSSetViewports},
    {"RSSetScissorRects", br_ID3D11DeviceContext_RSSetScissorRects},
    {"CopySubresourceRegion", br_ID3D11DeviceContext_CopySubresourceRegion},
    {"CopyResource", br_ID3D11DeviceContext_CopyResource},
    {"UpdateSubresource", br_ID3D11DeviceContext_UpdateSubresource},
    {"CopyStructureCount", br_ID3D11DeviceContext_CopyStructureCount},
    {"ClearRenderTargetView", br_ID3D11DeviceContext_ClearRenderTargetView},
    {"ClearUnorderedAccessViewUint", br_ID3D11DeviceContext_ClearUnorderedAccessViewUint},
    {"ClearUnorderedAccessViewFloat", br_ID3D11DeviceContext_ClearUnorderedAccessViewFloat},
    {"ClearDepthStencilView", br_ID3D11DeviceContext_ClearDepthStencilView},
    {"GenerateMips", br_ID3D11DeviceContext_GenerateMips},
    {"SetResourceMinLOD", br_ID3D11DeviceContext_SetResourceMinLOD},
    {"GetResourceMinLOD", br_ID3D11DeviceContext_GetResourceMinLOD},
    {"ResolveSubresource", br_ID3D11DeviceContext_ResolveSubresource},
    {"ExecuteCommandList", br_ID3D11DeviceContext_ExecuteCommandList},
    {"HSSetShaderResources", br_ID3D11DeviceContext_HSSetShaderResources},
    {"HSSetShader", br_ID3D11DeviceContext_HSSetShader},
    {"HSSetSamplers", br_ID3D11DeviceContext_HSSetSamplers},
    {"HSSetConstantBuffers", br_ID3D11DeviceContext_HSSetConstantBuffers},
    {"DSSetShaderResources", br_ID3D11DeviceContext_DSSetShaderResources},
    {"DSSetShader", br_ID3D11DeviceContext_DSSetShader},
    {"DSSetSamplers", br_ID3D11DeviceContext_DSSetSamplers},
    {"DSSetConstantBuffers", br_ID3D11DeviceContext_DSSetConstantBuffers},
    {"CSSetShaderResources", br_ID3D11DeviceContext_CSSetShaderResources},
    {"CSSetUnorderedAccessViews", br_ID3D11DeviceContext_CSSetUnorderedAccessViews},
    {"CSSetShader", br_ID3D11DeviceContext_CSSetShader},
    {"CSSetSamplers", br_ID3D11DeviceContext_CSSetSamplers},
    {"CSSetConstantBuffers", br_ID3D11DeviceContext_CSSetConstantBuffers},
    {"VSGetConstantBuffers", br_ID3D11DeviceContext_VSGetConstantBuffers},
    {"PSGetShaderResources", br_ID3D11DeviceContext_PSGetShaderResources},
    {"PSGetShader", br_ID3D11DeviceContext_PSGetShader},
    {"PSGetSamplers", br_ID3D11DeviceContext_PSGetSamplers},
    {"VSGetShader", br_ID3D11DeviceContext_VSGetShader},
    {"PSGetConstantBuffers", br_ID3D11DeviceContext_PSGetConstantBuffers},
    {"IAGetInputLayout", br_ID3D11DeviceContext_IAGetInputLayout},
    {"IAGetVertexBuffers", br_ID3D11DeviceContext_IAGetVertexBuffers},
    {"IAGetIndexBuffer", br_ID3D11DeviceContext_IAGetIndexBuffer},
    {"GSGetConstantBuffers", br_ID3D11DeviceContext_GSGetConstantBuffers},
    {"GSGetShader", br_ID3D11DeviceContext_GSGetShader},
    {"IAGetPrimitiveTopology", br_ID3D11DeviceContext_IAGetPrimitiveTopology},
    {"VSGetShaderResources", br_ID3D11DeviceContext_VSGetShaderResources},
    {"VSGetSamplers", br_ID3D11DeviceContext_VSGetSamplers},
    {"GetPredication", br_ID3D11DeviceContext_GetPredication},
    {"GSGetShaderResources", br_ID3D11DeviceContext_GSGetShaderResources},
    {"GSGetSamplers", br_ID3D11DeviceContext_GSGetSamplers},
    {"OMGetRenderTargets", br_ID3D11DeviceContext_OMGetRenderTargets},
    {"OMGetRenderTargetsAndUnorderedAccessViews", br_ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews},
    {"OMGetBlendState", br_ID3D11DeviceContext_OMGetBlendState},
    {"OMGetDepthStencilState", br_ID3D11DeviceContext_OMGetDepthStencilState},
    {"SOGetTargets", br_ID3D11DeviceContext_SOGetTargets},
    {"RSGetState", br_ID3D11DeviceContext_RSGetState},
    {"RSGetViewports", br_ID3D11DeviceContext_RSGetViewports},
    {"RSGetScissorRects", br_ID3D11DeviceContext_RSGetScissorRects},
    {"HSGetShaderResources", br_ID3D11DeviceContext_HSGetShaderResources},
    {"HSGetShader", br_ID3D11DeviceContext_HSGetShader},
    {"HSGetSamplers", br_ID3D11DeviceContext_HSGetSamplers},
    {"HSGetConstantBuffers", br_ID3D11DeviceContext_HSGetConstantBuffers},
    {"DSGetShaderResources", br_ID3D11DeviceContext_DSGetShaderResources},
    {"DSGetShader", br_ID3D11DeviceContext_DSGetShader},
    {"DSGetSamplers", br_ID3D11DeviceContext_DSGetSamplers},
    {"DSGetConstantBuffers", br_ID3D11DeviceContext_DSGetConstantBuffers},
    {"CSGetShaderResources", br_ID3D11DeviceContext_CSGetShaderResources},
    {"CSGetUnorderedAccessViews", br_ID3D11DeviceContext_CSGetUnorderedAccessViews},
    {"CSGetShader", br_ID3D11DeviceContext_CSGetShader},
    {"CSGetSamplers", br_ID3D11DeviceContext_CSGetSamplers},
    {"CSGetConstantBuffers", br_ID3D11DeviceContext_CSGetConstantBuffers},
    {"ClearState", br_ID3D11DeviceContext_ClearState},
    {"Flush", br_ID3D11DeviceContext_Flush},
    {"GetType", br_ID3D11DeviceContext_GetType},
    {"GetContextFlags", br_ID3D11DeviceContext_GetContextFlags},
    {"FinishCommandList", br_ID3D11DeviceContext_FinishCommandList},
    {"CopySubresourceRegion1", br_ID3D11DeviceContext1_CopySubresourceRegion1},
    {"UpdateSubresource1", br_ID3D11DeviceContext1_UpdateSubresource1},
    {"DiscardResource", br_ID3D11DeviceContext1_DiscardResource},
    {"DiscardView", br_ID3D11DeviceContext1_DiscardView},
    {"VSSetConstantBuffers1", br_ID3D11DeviceContext1_VSSetConstantBuffers1},
    {"HSSetConstantBuffers1", br_ID3D11DeviceContext1_HSSetConstantBuffers1},
    {"DSSetConstantBuffers1", br_ID3D11DeviceContext1_DSSetConstantBuffers1},
    {"GSSetConstantBuffers1", br_ID3D11DeviceContext1_GSSetConstantBuffers1},
    {"PSSetConstantBuffers1", br_ID3D11DeviceContext1_PSSetConstantBuffers1},
    {"CSSetConstantBuffers1", br_ID3D11DeviceContext1_CSSetConstantBuffers1},
    {"VSGetConstantBuffers1", br_ID3D11DeviceContext1_VSGetConstantBuffers1},
    {"HSGetConstantBuffers1", br_ID3D11DeviceContext1_HSGetConstantBuffers1},
    {"DSGetConstantBuffers1", br_ID3D11DeviceContext1_DSGetConstantBuffers1},
    {"GSGetConstantBuffers1", br_ID3D11DeviceContext1_GSGetConstantBuffers1},
    {"PSGetConstantBuffers1", br_ID3D11DeviceContext1_PSGetConstantBuffers1},
    {"CSGetConstantBuffers1", br_ID3D11DeviceContext1_CSGetConstantBuffers1},
    {"SwapDeviceContextState", br_ID3D11DeviceContext1_SwapDeviceContextState},
    {"ClearView", br_ID3D11DeviceContext1_ClearView},
    {"DiscardView1", br_ID3D11DeviceContext1_DiscardView1},
    {"UpdateTileMappings", br_ID3D11DeviceContext2_UpdateTileMappings},
    {"CopyTileMappings", br_ID3D11DeviceContext2_CopyTileMappings},
    {"CopyTiles", br_ID3D11DeviceContext2_CopyTiles},
    {"UpdateTiles", br_ID3D11DeviceContext2_UpdateTiles},
    {"ResizeTilePool", br_ID3D11DeviceContext2_ResizeTilePool},
    {"TiledResourceBarrier", br_ID3D11DeviceContext2_TiledResourceBarrier},
    {"IsAnnotationEnabled", br_ID3D11DeviceContext2_IsAnnotationEnabled},
    {"SetMarkerInt", br_ID3D11DeviceContext2_SetMarkerInt},
    {"BeginEventInt", br_ID3D11DeviceContext2_BeginEventInt},
    {"EndEvent", br_ID3D11DeviceContext2_EndEvent},
    {"Flush1", br_ID3D11DeviceContext3_Flush1},
    {"SetHardwareProtectionState", br_ID3D11DeviceContext3_SetHardwareProtectionState},
    {"GetHardwareProtectionState", br_ID3D11DeviceContext3_GetHardwareProtectionState},
    {"Signal", br_ID3D11DeviceContext4_Signal},
    {"Wait", br_ID3D11DeviceContext4_Wait},
};
static const BrMethod br_methods_ID3D11Buffer[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetType", br_ID3D11Resource_GetType},
    {"SetEvictionPriority", br_ID3D11Resource_SetEvictionPriority},
    {"GetEvictionPriority", br_ID3D11Resource_GetEvictionPriority},
    {"GetDesc", br_ID3D11Buffer_GetDesc},
};
static const BrMethod br_methods_ID3D11Texture1D[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetType", br_ID3D11Resource_GetType},
    {"SetEvictionPriority", br_ID3D11Resource_SetEvictionPriority},
    {"GetEvictionPriority", br_ID3D11Resource_GetEvictionPriority},
    {"GetDesc", br_ID3D11Texture1D_GetDesc},
};
static const BrMethod br_methods_ID3D11Texture2D1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetType", br_ID3D11Resource_GetType},
    {"SetEvictionPriority", br_ID3D11Resource_SetEvictionPriority},
    {"GetEvictionPriority", br_ID3D11Resource_GetEvictionPriority},
    {"GetDesc", br_ID3D11Texture2D_GetDesc},
    {"GetDesc1", br_ID3D11Texture2D1_GetDesc1},
};
static const BrMethod br_methods_ID3D11Texture3D1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetType", br_ID3D11Resource_GetType},
    {"SetEvictionPriority", br_ID3D11Resource_SetEvictionPriority},
    {"GetEvictionPriority", br_ID3D11Resource_GetEvictionPriority},
    {"GetDesc", br_ID3D11Texture3D_GetDesc},
    {"GetDesc1", br_ID3D11Texture3D1_GetDesc1},
};
static const BrMethod br_methods_ID3D11ShaderResourceView1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetResource", br_ID3D11View_GetResource},
    {"GetDesc", br_ID3D11ShaderResourceView_GetDesc},
    {"GetDesc1", br_ID3D11ShaderResourceView1_GetDesc1},
};
static const BrMethod br_methods_ID3D11RenderTargetView1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetResource", br_ID3D11View_GetResource},
    {"GetDesc", br_ID3D11RenderTargetView_GetDesc},
    {"GetDesc1", br_ID3D11RenderTargetView1_GetDesc1},
};
static const BrMethod br_methods_ID3D11DepthStencilView[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetResource", br_ID3D11View_GetResource},
    {"GetDesc", br_ID3D11DepthStencilView_GetDesc},
};
static const BrMethod br_methods_ID3D11UnorderedAccessView1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetResource", br_ID3D11View_GetResource},
    {"GetDesc", br_ID3D11UnorderedAccessView_GetDesc},
    {"GetDesc1", br_ID3D11UnorderedAccessView1_GetDesc1},
};
static const BrMethod br_methods_ID3D11VertexShader[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11PixelShader[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11GeometryShader[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11HullShader[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11DomainShader[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11ComputeShader[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11InputLayout[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
};
static const BrMethod br_methods_ID3D11BlendState1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDesc", br_ID3D11BlendState_GetDesc},
    {"GetDesc1", br_ID3D11BlendState1_GetDesc1},
};
static const BrMethod br_methods_ID3D11DepthStencilState[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDesc", br_ID3D11DepthStencilState_GetDesc},
};
static const BrMethod br_methods_ID3D11RasterizerState2[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDesc", br_ID3D11RasterizerState_GetDesc},
    {"GetDesc1", br_ID3D11RasterizerState1_GetDesc1},
    {"GetDesc2", br_ID3D11RasterizerState2_GetDesc2},
};
static const BrMethod br_methods_ID3D11SamplerState[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDesc", br_ID3D11SamplerState_GetDesc},
};
static const BrMethod br_methods_ID3D11Query1[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDataSize", br_ID3D11Asynchronous_GetDataSize},
    {"GetDesc", br_ID3D11Query_GetDesc},
    {"GetDesc1", br_ID3D11Query1_GetDesc1},
};
static const BrMethod br_methods_ID3D11Predicate[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDataSize", br_ID3D11Asynchronous_GetDataSize},
    {"GetDesc", br_ID3D11Query_GetDesc},
};
static const BrMethod br_methods_ID3D11Counter[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetDataSize", br_ID3D11Asynchronous_GetDataSize},
    {"GetDesc", br_ID3D11Counter_GetDesc},
};
static const BrMethod br_methods_ID3D11CommandList[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetContextFlags", br_ID3D11CommandList_GetContextFlags},
};
static const BrMethod br_methods_ID3D11ClassLinkage[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetClassInstance", br_ID3D11ClassLinkage_GetClassInstance},
    {"CreateClassInstance", br_ID3D11ClassLinkage_CreateClassInstance},
};
static const BrMethod br_methods_ID3D11ClassInstance[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"GetClassLinkage", br_ID3D11ClassInstance_GetClassLinkage},
    {"GetDesc", br_ID3D11ClassInstance_GetDesc},
    {"GetInstanceName", br_ID3D11ClassInstance_GetInstanceName},
    {"GetTypeName", br_ID3D11ClassInstance_GetTypeName},
};
static const BrMethod br_methods_ID3D11Fence[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"GetDevice", br_ID3D11DeviceChild_GetDevice},
    {"GetPrivateData", br_ID3D11DeviceChild_GetPrivateData},
    {"SetPrivateData", br_ID3D11DeviceChild_SetPrivateData},
    {"SetPrivateDataInterface", br_ID3D11DeviceChild_SetPrivateDataInterface},
    {"CreateSharedHandle", br_ID3D11Fence_CreateSharedHandle},
    {"GetCompletedValue", br_ID3D11Fence_GetCompletedValue},
    {"SetEventOnCompletion", br_ID3D11Fence_SetEventOnCompletion},
};
static const BrMethod br_methods_ID3D11Multithread[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"Enter", br_ID3D11Multithread_Enter},
    {"Leave", br_ID3D11Multithread_Leave},
    {"SetMultithreadProtected", br_ID3D11Multithread_SetMultithreadProtected},
    {"GetMultithreadProtected", br_ID3D11Multithread_GetMultithreadProtected},
};
static const BrMethod br_methods_ID3DUserDefinedAnnotation[] = {
    {"QueryInterface", br_special_QueryInterface},
    {"AddRef", br_special_AddRef},
    {"Release", br_special_Release},
    {"BeginEvent", br_special_ID3DUserDefinedAnnotation_BeginEvent},
    {"EndEvent", br_ID3DUserDefinedAnnotation_EndEvent},
    {"SetMarker", br_special_ID3DUserDefinedAnnotation_SetMarker},
    {"GetStatus", br_ID3DUserDefinedAnnotation_GetStatus},
};
static const BrMethod *const br_family_methods[BR_FAMILY_COUNT] = {br_methods_IDXGIFactory7, br_methods_IDXGIAdapter4, br_methods_IDXGIOutput6, br_methods_IDXGISwapChain4, br_methods_IDXGIDevice4, br_methods_IDXGISurface2, br_methods_IDXGIResource1, br_methods_IDXGIKeyedMutex, br_methods_ID3D11Device5, br_methods_ID3D11DeviceContext4, br_methods_ID3D11Buffer, br_methods_ID3D11Texture1D, br_methods_ID3D11Texture2D1, br_methods_ID3D11Texture3D1, br_methods_ID3D11ShaderResourceView1, br_methods_ID3D11RenderTargetView1, br_methods_ID3D11DepthStencilView, br_methods_ID3D11UnorderedAccessView1, br_methods_ID3D11VertexShader, br_methods_ID3D11PixelShader, br_methods_ID3D11GeometryShader, br_methods_ID3D11HullShader, br_methods_ID3D11DomainShader, br_methods_ID3D11ComputeShader, br_methods_ID3D11InputLayout, br_methods_ID3D11BlendState1, br_methods_ID3D11DepthStencilState, br_methods_ID3D11RasterizerState2, br_methods_ID3D11SamplerState, br_methods_ID3D11Query1, br_methods_ID3D11Predicate, br_methods_ID3D11Counter, br_methods_ID3D11CommandList, br_methods_ID3D11ClassLinkage, br_methods_ID3D11ClassInstance, br_methods_ID3D11Fence, br_methods_ID3D11Multithread, br_methods_ID3DUserDefinedAnnotation};
static const u32 br_family_method_count[BR_FAMILY_COUNT] = {sizeof br_methods_IDXGIFactory7 / sizeof br_methods_IDXGIFactory7[0], sizeof br_methods_IDXGIAdapter4 / sizeof br_methods_IDXGIAdapter4[0], sizeof br_methods_IDXGIOutput6 / sizeof br_methods_IDXGIOutput6[0], sizeof br_methods_IDXGISwapChain4 / sizeof br_methods_IDXGISwapChain4[0], sizeof br_methods_IDXGIDevice4 / sizeof br_methods_IDXGIDevice4[0], sizeof br_methods_IDXGISurface2 / sizeof br_methods_IDXGISurface2[0], sizeof br_methods_IDXGIResource1 / sizeof br_methods_IDXGIResource1[0], sizeof br_methods_IDXGIKeyedMutex / sizeof br_methods_IDXGIKeyedMutex[0], sizeof br_methods_ID3D11Device5 / sizeof br_methods_ID3D11Device5[0], sizeof br_methods_ID3D11DeviceContext4 / sizeof br_methods_ID3D11DeviceContext4[0], sizeof br_methods_ID3D11Buffer / sizeof br_methods_ID3D11Buffer[0], sizeof br_methods_ID3D11Texture1D / sizeof br_methods_ID3D11Texture1D[0], sizeof br_methods_ID3D11Texture2D1 / sizeof br_methods_ID3D11Texture2D1[0], sizeof br_methods_ID3D11Texture3D1 / sizeof br_methods_ID3D11Texture3D1[0], sizeof br_methods_ID3D11ShaderResourceView1 / sizeof br_methods_ID3D11ShaderResourceView1[0], sizeof br_methods_ID3D11RenderTargetView1 / sizeof br_methods_ID3D11RenderTargetView1[0], sizeof br_methods_ID3D11DepthStencilView / sizeof br_methods_ID3D11DepthStencilView[0], sizeof br_methods_ID3D11UnorderedAccessView1 / sizeof br_methods_ID3D11UnorderedAccessView1[0], sizeof br_methods_ID3D11VertexShader / sizeof br_methods_ID3D11VertexShader[0], sizeof br_methods_ID3D11PixelShader / sizeof br_methods_ID3D11PixelShader[0], sizeof br_methods_ID3D11GeometryShader / sizeof br_methods_ID3D11GeometryShader[0], sizeof br_methods_ID3D11HullShader / sizeof br_methods_ID3D11HullShader[0], sizeof br_methods_ID3D11DomainShader / sizeof br_methods_ID3D11DomainShader[0], sizeof br_methods_ID3D11ComputeShader / sizeof br_methods_ID3D11ComputeShader[0], sizeof br_methods_ID3D11InputLayout / sizeof br_methods_ID3D11InputLayout[0], sizeof br_methods_ID3D11BlendState1 / sizeof br_methods_ID3D11BlendState1[0], sizeof br_methods_ID3D11DepthStencilState / sizeof br_methods_ID3D11DepthStencilState[0], sizeof br_methods_ID3D11RasterizerState2 / sizeof br_methods_ID3D11RasterizerState2[0], sizeof br_methods_ID3D11SamplerState / sizeof br_methods_ID3D11SamplerState[0], sizeof br_methods_ID3D11Query1 / sizeof br_methods_ID3D11Query1[0], sizeof br_methods_ID3D11Predicate / sizeof br_methods_ID3D11Predicate[0], sizeof br_methods_ID3D11Counter / sizeof br_methods_ID3D11Counter[0], sizeof br_methods_ID3D11CommandList / sizeof br_methods_ID3D11CommandList[0], sizeof br_methods_ID3D11ClassLinkage / sizeof br_methods_ID3D11ClassLinkage[0], sizeof br_methods_ID3D11ClassInstance / sizeof br_methods_ID3D11ClassInstance[0], sizeof br_methods_ID3D11Fence / sizeof br_methods_ID3D11Fence[0], sizeof br_methods_ID3D11Multithread / sizeof br_methods_ID3D11Multithread[0], sizeof br_methods_ID3DUserDefinedAnnotation / sizeof br_methods_ID3DUserDefinedAnnotation[0]};
