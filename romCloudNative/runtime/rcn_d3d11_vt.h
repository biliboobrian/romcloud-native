// Généré par scripts/gen_d3d_vtables.py depuis les en-têtes de llvm-mingw : ne pas modifier.
// Ordre des méthodes de chaque interface (IUnknown compris).
#ifndef RCN_D3D11_VT_H
#define RCN_D3D11_VT_H

static const char *const vt_IDXGIFactory7[] = {
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData",
    "GetParent", "EnumAdapters", "MakeWindowAssociation", "GetWindowAssociation", "CreateSwapChain", "CreateSoftwareAdapter",
    "EnumAdapters1", "IsCurrent", "IsWindowedStereoEnabled", "CreateSwapChainForHwnd", "CreateSwapChainForCoreWindow", "GetSharedResourceAdapterLuid",
    "RegisterStereoStatusWindow", "RegisterStereoStatusEvent", "UnregisterStereoStatus", "RegisterOcclusionStatusWindow", "RegisterOcclusionStatusEvent", "UnregisterOcclusionStatus",
    "CreateSwapChainForComposition", "GetCreationFlags", "EnumAdapterByLuid", "EnumWarpAdapter", "CheckFeatureSupport", "EnumAdapterByGpuPreference",
    "RegisterAdaptersChangedEvent", "UnregisterAdaptersChangedEvent",
};
#define VT_IDXGIFactory7_COUNT 32

static const char *const vt_IDXGIAdapter4[] = {
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData",
    "GetParent", "EnumOutputs", "GetDesc", "CheckInterfaceSupport", "GetDesc1", "GetDesc2",
    "RegisterHardwareContentProtectionTeardownStatusEvent", "UnregisterHardwareContentProtectionTeardownStatus", "QueryVideoMemoryInfo", "SetVideoMemoryReservation", "RegisterVideoMemoryBudgetChangeNotificationEvent", "UnregisterVideoMemoryBudgetChangeNotification",
    "GetDesc3",
};
#define VT_IDXGIAdapter4_COUNT 19

static const char *const vt_IDXGIOutput6[] = {
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData",
    "GetParent", "GetDesc", "GetDisplayModeList", "FindClosestMatchingMode", "WaitForVBlank", "TakeOwnership",
    "ReleaseOwnership", "GetGammaControlCapabilities", "SetGammaControl", "GetGammaControl", "SetDisplaySurface", "GetDisplaySurfaceData",
    "GetFrameStatistics", "GetDisplayModeList1", "FindClosestMatchingMode1", "GetDisplaySurfaceData1", "DuplicateOutput", "SupportsOverlays",
    "CheckOverlaySupport", "CheckOverlayColorSpaceSupport", "DuplicateOutput1", "GetDesc1", "CheckHardwareCompositionSupport",
};
#define VT_IDXGIOutput6_COUNT 29

static const char *const vt_IDXGISwapChain4[] = {
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData",
    "GetParent", "GetDevice", "Present", "GetBuffer", "SetFullscreenState", "GetFullscreenState",
    "GetDesc", "ResizeBuffers", "ResizeTarget", "GetContainingOutput", "GetFrameStatistics", "GetLastPresentCount",
    "GetDesc1", "GetFullscreenDesc", "GetHwnd", "GetCoreWindow", "Present1", "IsTemporaryMonoSupported",
    "GetRestrictToOutput", "SetBackgroundColor", "GetBackgroundColor", "SetRotation", "GetRotation", "SetSourceSize",
    "GetSourceSize", "SetMaximumFrameLatency", "GetMaximumFrameLatency", "GetFrameLatencyWaitableObject", "SetMatrixTransform", "GetMatrixTransform",
    "GetCurrentBackBufferIndex", "CheckColorSpaceSupport", "SetColorSpace1", "ResizeBuffers1", "SetHDRMetaData",
};
#define VT_IDXGISwapChain4_COUNT 41

static const char *const vt_IDXGIDevice4[] = {
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData",
    "GetParent", "GetAdapter", "CreateSurface", "QueryResourceResidency", "SetGPUThreadPriority", "GetGPUThreadPriority",
    "SetMaximumFrameLatency", "GetMaximumFrameLatency", "OfferResources", "ReclaimResources", "EnqueueSetEvent", "Trim",
    "OfferResources1", "ReclaimResources1",
};
#define VT_IDXGIDevice4_COUNT 20

static const char *const vt_IDXGISurface2[] = {
    "QueryInterface", "AddRef", "Release", "SetPrivateData", "SetPrivateDataInterface", "GetPrivateData",
    "GetParent", "GetDevice", "GetDesc", "Map", "Unmap", "GetDC",
    "ReleaseDC", "GetResource",
};
#define VT_IDXGISurface2_COUNT 14

static const char *const vt_ID3D11Device5[] = {
    "QueryInterface", "AddRef", "Release", "CreateBuffer", "CreateTexture1D", "CreateTexture2D",
    "CreateTexture3D", "CreateShaderResourceView", "CreateUnorderedAccessView", "CreateRenderTargetView", "CreateDepthStencilView", "CreateInputLayout",
    "CreateVertexShader", "CreateGeometryShader", "CreateGeometryShaderWithStreamOutput", "CreatePixelShader", "CreateHullShader", "CreateDomainShader",
    "CreateComputeShader", "CreateClassLinkage", "CreateBlendState", "CreateDepthStencilState", "CreateRasterizerState", "CreateSamplerState",
    "CreateQuery", "CreatePredicate", "CreateCounter", "CreateDeferredContext", "OpenSharedResource", "CheckFormatSupport",
    "CheckMultisampleQualityLevels", "CheckCounterInfo", "CheckCounter", "CheckFeatureSupport", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetFeatureLevel", "GetCreationFlags", "GetDeviceRemovedReason", "GetImmediateContext", "SetExceptionMode",
    "GetExceptionMode", "GetImmediateContext1", "CreateDeferredContext1", "CreateBlendState1", "CreateRasterizerState1", "CreateDeviceContextState",
    "OpenSharedResource1", "OpenSharedResourceByName", "GetImmediateContext2", "CreateDeferredContext2", "GetResourceTiling", "CheckMultisampleQualityLevels1",
    "CreateTexture2D1", "CreateTexture3D1", "CreateRasterizerState2", "CreateShaderResourceView1", "CreateUnorderedAccessView1", "CreateRenderTargetView1",
    "CreateQuery1", "GetImmediateContext3", "CreateDeferredContext3", "WriteToSubresource", "ReadFromSubresource", "RegisterDeviceRemovedEvent",
    "UnregisterDeviceRemoved", "OpenSharedFence", "CreateFence",
};
#define VT_ID3D11Device5_COUNT 69

static const char *const vt_ID3D11DeviceContext4[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "VSSetConstantBuffers", "PSSetShaderResources", "PSSetShader", "PSSetSamplers", "VSSetShader",
    "DrawIndexed", "Draw", "Map", "Unmap", "PSSetConstantBuffers", "IASetInputLayout",
    "IASetVertexBuffers", "IASetIndexBuffer", "DrawIndexedInstanced", "DrawInstanced", "GSSetConstantBuffers", "GSSetShader",
    "IASetPrimitiveTopology", "VSSetShaderResources", "VSSetSamplers", "Begin", "End", "GetData",
    "SetPredication", "GSSetShaderResources", "GSSetSamplers", "OMSetRenderTargets", "OMSetRenderTargetsAndUnorderedAccessViews", "OMSetBlendState",
    "OMSetDepthStencilState", "SOSetTargets", "DrawAuto", "DrawIndexedInstancedIndirect", "DrawInstancedIndirect", "Dispatch",
    "DispatchIndirect", "RSSetState", "RSSetViewports", "RSSetScissorRects", "CopySubresourceRegion", "CopyResource",
    "UpdateSubresource", "CopyStructureCount", "ClearRenderTargetView", "ClearUnorderedAccessViewUint", "ClearUnorderedAccessViewFloat", "ClearDepthStencilView",
    "GenerateMips", "SetResourceMinLOD", "GetResourceMinLOD", "ResolveSubresource", "ExecuteCommandList", "HSSetShaderResources",
    "HSSetShader", "HSSetSamplers", "HSSetConstantBuffers", "DSSetShaderResources", "DSSetShader", "DSSetSamplers",
    "DSSetConstantBuffers", "CSSetShaderResources", "CSSetUnorderedAccessViews", "CSSetShader", "CSSetSamplers", "CSSetConstantBuffers",
    "VSGetConstantBuffers", "PSGetShaderResources", "PSGetShader", "PSGetSamplers", "VSGetShader", "PSGetConstantBuffers",
    "IAGetInputLayout", "IAGetVertexBuffers", "IAGetIndexBuffer", "GSGetConstantBuffers", "GSGetShader", "IAGetPrimitiveTopology",
    "VSGetShaderResources", "VSGetSamplers", "GetPredication", "GSGetShaderResources", "GSGetSamplers", "OMGetRenderTargets",
    "OMGetRenderTargetsAndUnorderedAccessViews", "OMGetBlendState", "OMGetDepthStencilState", "SOGetTargets", "RSGetState", "RSGetViewports",
    "RSGetScissorRects", "HSGetShaderResources", "HSGetShader", "HSGetSamplers", "HSGetConstantBuffers", "DSGetShaderResources",
    "DSGetShader", "DSGetSamplers", "DSGetConstantBuffers", "CSGetShaderResources", "CSGetUnorderedAccessViews", "CSGetShader",
    "CSGetSamplers", "CSGetConstantBuffers", "ClearState", "Flush", "GetType", "GetContextFlags",
    "FinishCommandList", "CopySubresourceRegion1", "UpdateSubresource1", "DiscardResource", "DiscardView", "VSSetConstantBuffers1",
    "HSSetConstantBuffers1", "DSSetConstantBuffers1", "GSSetConstantBuffers1", "PSSetConstantBuffers1", "CSSetConstantBuffers1", "VSGetConstantBuffers1",
    "HSGetConstantBuffers1", "DSGetConstantBuffers1", "GSGetConstantBuffers1", "PSGetConstantBuffers1", "CSGetConstantBuffers1", "SwapDeviceContextState",
    "ClearView", "DiscardView1", "UpdateTileMappings", "CopyTileMappings", "CopyTiles", "UpdateTiles",
    "ResizeTilePool", "TiledResourceBarrier", "IsAnnotationEnabled", "SetMarkerInt", "BeginEventInt", "EndEvent",
    "Flush1", "SetHardwareProtectionState", "GetHardwareProtectionState", "Signal", "Wait",
};
#define VT_ID3D11DeviceContext4_COUNT 149

static const char *const vt_ID3D11Buffer[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetType", "SetEvictionPriority", "GetEvictionPriority", "GetDesc",
};
#define VT_ID3D11Buffer_COUNT 11

static const char *const vt_ID3D11Texture1D[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetType", "SetEvictionPriority", "GetEvictionPriority", "GetDesc",
};
#define VT_ID3D11Texture1D_COUNT 11

static const char *const vt_ID3D11Texture2D1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetType", "SetEvictionPriority", "GetEvictionPriority", "GetDesc", "GetDesc1",
};
#define VT_ID3D11Texture2D1_COUNT 12

static const char *const vt_ID3D11Texture3D1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetType", "SetEvictionPriority", "GetEvictionPriority", "GetDesc", "GetDesc1",
};
#define VT_ID3D11Texture3D1_COUNT 12

static const char *const vt_ID3D11ShaderResourceView1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetResource", "GetDesc", "GetDesc1",
};
#define VT_ID3D11ShaderResourceView1_COUNT 10

static const char *const vt_ID3D11RenderTargetView1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetResource", "GetDesc", "GetDesc1",
};
#define VT_ID3D11RenderTargetView1_COUNT 10

static const char *const vt_ID3D11DepthStencilView[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetResource", "GetDesc",
};
#define VT_ID3D11DepthStencilView_COUNT 9

static const char *const vt_ID3D11UnorderedAccessView1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetResource", "GetDesc", "GetDesc1",
};
#define VT_ID3D11UnorderedAccessView1_COUNT 10

static const char *const vt_ID3D11VertexShader[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11VertexShader_COUNT 7

static const char *const vt_ID3D11PixelShader[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11PixelShader_COUNT 7

static const char *const vt_ID3D11GeometryShader[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11GeometryShader_COUNT 7

static const char *const vt_ID3D11HullShader[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11HullShader_COUNT 7

static const char *const vt_ID3D11DomainShader[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11DomainShader_COUNT 7

static const char *const vt_ID3D11ComputeShader[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11ComputeShader_COUNT 7

static const char *const vt_ID3D11InputLayout[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface",
};
#define VT_ID3D11InputLayout_COUNT 7

static const char *const vt_ID3D11BlendState1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetDesc", "GetDesc1",
};
#define VT_ID3D11BlendState1_COUNT 9

static const char *const vt_ID3D11DepthStencilState[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetDesc",
};
#define VT_ID3D11DepthStencilState_COUNT 8

static const char *const vt_ID3D11RasterizerState2[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetDesc", "GetDesc1", "GetDesc2",
};
#define VT_ID3D11RasterizerState2_COUNT 10

static const char *const vt_ID3D11SamplerState[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetDesc",
};
#define VT_ID3D11SamplerState_COUNT 8

static const char *const vt_ID3D11Query1[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetDataSize", "GetDesc", "GetDesc1",
};
#define VT_ID3D11Query1_COUNT 10

static const char *const vt_ID3D11Multithread[] = {
    "QueryInterface", "AddRef", "Release", "Enter", "Leave", "SetMultithreadProtected",
    "GetMultithreadProtected",
};
#define VT_ID3D11Multithread_COUNT 7

static const char *const vt_ID3DUserDefinedAnnotation[] = {
    "QueryInterface", "AddRef", "Release", "BeginEvent", "EndEvent", "SetMarker",
    "GetStatus",
};
#define VT_ID3DUserDefinedAnnotation_COUNT 7

static const char *const vt_ID3D11ClassLinkage[] = {
    "QueryInterface", "AddRef", "Release", "GetDevice", "GetPrivateData", "SetPrivateData",
    "SetPrivateDataInterface", "GetClassInstance", "CreateClassInstance",
};
#define VT_ID3D11ClassLinkage_COUNT 9

static const char *const chain_IDXGIFactory7[] = {"IDXGIFactory7", "IDXGIFactory6", "IDXGIFactory5", "IDXGIFactory4", "IDXGIFactory3", "IDXGIFactory2", "IDXGIFactory1", "IDXGIFactory", "IDXGIObject", NULL};
static const char *const chain_IDXGIAdapter4[] = {"IDXGIAdapter4", "IDXGIAdapter3", "IDXGIAdapter2", "IDXGIAdapter1", "IDXGIAdapter", "IDXGIObject", NULL};
static const char *const chain_IDXGIOutput6[] = {"IDXGIOutput6", "IDXGIOutput5", "IDXGIOutput4", "IDXGIOutput3", "IDXGIOutput2", "IDXGIOutput1", "IDXGIOutput", "IDXGIObject", NULL};
static const char *const chain_IDXGISwapChain4[] = {"IDXGISwapChain4", "IDXGISwapChain3", "IDXGISwapChain2", "IDXGISwapChain1", "IDXGISwapChain", "IDXGIDeviceSubObject", "IDXGIObject", NULL};
static const char *const chain_IDXGIDevice4[] = {"IDXGIDevice4", "IDXGIDevice3", "IDXGIDevice2", "IDXGIDevice1", "IDXGIDevice", "IDXGIObject", NULL};
static const char *const chain_IDXGISurface2[] = {"IDXGISurface2", "IDXGISurface1", "IDXGISurface", "IDXGIDeviceSubObject", "IDXGIObject", NULL};
static const char *const chain_ID3D11Device5[] = {"ID3D11Device5", "ID3D11Device4", "ID3D11Device3", "ID3D11Device2", "ID3D11Device1", "ID3D11Device", NULL};
static const char *const chain_ID3D11DeviceContext4[] = {"ID3D11DeviceContext4", "ID3D11DeviceContext3", "ID3D11DeviceContext2", "ID3D11DeviceContext1", "ID3D11DeviceContext", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11Buffer[] = {"ID3D11Buffer", "ID3D11Resource", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11Texture1D[] = {"ID3D11Texture1D", "ID3D11Resource", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11Texture2D1[] = {"ID3D11Texture2D1", "ID3D11Texture2D", "ID3D11Resource", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11Texture3D1[] = {"ID3D11Texture3D1", "ID3D11Texture3D", "ID3D11Resource", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11ShaderResourceView1[] = {"ID3D11ShaderResourceView1", "ID3D11ShaderResourceView", "ID3D11View", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11RenderTargetView1[] = {"ID3D11RenderTargetView1", "ID3D11RenderTargetView", "ID3D11View", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11DepthStencilView[] = {"ID3D11DepthStencilView", "ID3D11View", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11UnorderedAccessView1[] = {"ID3D11UnorderedAccessView1", "ID3D11UnorderedAccessView", "ID3D11View", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11VertexShader[] = {"ID3D11VertexShader", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11PixelShader[] = {"ID3D11PixelShader", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11GeometryShader[] = {"ID3D11GeometryShader", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11HullShader[] = {"ID3D11HullShader", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11DomainShader[] = {"ID3D11DomainShader", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11ComputeShader[] = {"ID3D11ComputeShader", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11InputLayout[] = {"ID3D11InputLayout", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11BlendState1[] = {"ID3D11BlendState1", "ID3D11BlendState", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11DepthStencilState[] = {"ID3D11DepthStencilState", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11RasterizerState2[] = {"ID3D11RasterizerState2", "ID3D11RasterizerState1", "ID3D11RasterizerState", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11SamplerState[] = {"ID3D11SamplerState", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11Query1[] = {"ID3D11Query1", "ID3D11Query", "ID3D11Asynchronous", "ID3D11DeviceChild", NULL};
static const char *const chain_ID3D11Multithread[] = {"ID3D11Multithread", NULL};
static const char *const chain_ID3DUserDefinedAnnotation[] = {"ID3DUserDefinedAnnotation", NULL};
static const char *const chain_ID3D11ClassLinkage[] = {"ID3D11ClassLinkage", "ID3D11DeviceChild", NULL};

typedef struct { u32 a; u16 b, c; u8 d[8]; const char *name; } RcnIid;
static const RcnIid rcn_d3d_iids[] = {
    {0xaec22fb8, 0x76f3, 0x4639, {0x9b, 0xe0, 0x28, 0xeb, 0x43, 0xa6, 0x7a, 0x2e}, "IDXGIObject"},
    {0x3d3e0379, 0xf9de, 0x4d58, {0xbb, 0x6c, 0x18, 0xd6, 0x29, 0x92, 0xf1, 0xa6}, "IDXGIDeviceSubObject"},
    {0x035f3ab4, 0x482e, 0x4e50, {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}, "IDXGIResource"},
    {0x9d8e1289, 0xd7b3, 0x465f, {0x81, 0x26, 0x25, 0x0e, 0x34, 0x9a, 0xf8, 0x5d}, "IDXGIKeyedMutex"},
    {0xcafcb56c, 0x6ac3, 0x4889, {0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec}, "IDXGISurface"},
    {0x4ae63092, 0x6327, 0x4c1b, {0x80, 0xae, 0xbf, 0xe1, 0x2e, 0xa3, 0x2b, 0x86}, "IDXGISurface1"},
    {0xae02eedb, 0xc735, 0x4690, {0x8d, 0x52, 0x5a, 0x8d, 0xc2, 0x02, 0x13, 0xaa}, "IDXGIOutput"},
    {0x2411e7e1, 0x12ac, 0x4ccf, {0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0}, "IDXGIAdapter"},
    {0x310d36a0, 0xd2e7, 0x4c0a, {0xaa, 0x04, 0x6a, 0x9d, 0x23, 0xb8, 0x88, 0x6a}, "IDXGISwapChain"},
    {0x7b7166ec, 0x21c7, 0x44ae, {0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69}, "IDXGIFactory"},
    {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}, "IDXGIDevice"},
    {0x29038f61, 0x3839, 0x4626, {0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05}, "IDXGIAdapter1"},
    {0x77db970f, 0x6276, 0x48ba, {0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c}, "IDXGIDevice1"},
    {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}, "IDXGIFactory1"},
    {0x191cfac3, 0xa341, 0x470d, {0xb2, 0x6e, 0xa8, 0x64, 0xf4, 0x28, 0x31, 0x9c}, "IDXGIOutputDuplication"},
    {0xaba496dd, 0xb617, 0x4cb8, {0xa8, 0x66, 0xbc, 0x44, 0xd7, 0xeb, 0x1f, 0xa2}, "IDXGISurface2"},
    {0x30961379, 0x4609, 0x4a41, {0x99, 0x8e, 0x54, 0xfe, 0x56, 0x7e, 0xe0, 0xc1}, "IDXGIResource1"},
    {0xea9dbf1a, 0xc88e, 0x4486, {0x85, 0x4a, 0x98, 0xaa, 0x01, 0x38, 0xf3, 0x0c}, "IDXGIDisplayControl"},
    {0x05008617, 0xfbfd, 0x4051, {0xa7, 0x90, 0x14, 0x48, 0x84, 0xb4, 0xf6, 0xa9}, "IDXGIDevice2"},
    {0x790a45f7, 0x0d42, 0x4876, {0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa}, "IDXGISwapChain1"},
    {0x50c83a1c, 0xe072, 0x4c48, {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}, "IDXGIFactory2"},
    {0x0aa1ae0a, 0xfa0e, 0x4b84, {0x86, 0x44, 0xe0, 0x5f, 0xf8, 0xe5, 0xac, 0xb5}, "IDXGIAdapter2"},
    {0x00cddea8, 0x939b, 0x4b83, {0xa3, 0x40, 0xa6, 0x85, 0x22, 0x66, 0x66, 0xcc}, "IDXGIOutput1"},
    {0x6007896c, 0x3244, 0x4afd, {0xbf, 0x18, 0xa6, 0xd3, 0xbe, 0xda, 0x50, 0x23}, "IDXGIDevice3"},
    {0xa8be2ac4, 0x199f, 0x4946, {0xb3, 0x31, 0x79, 0x59, 0x9f, 0xb9, 0x8d, 0xe7}, "IDXGISwapChain2"},
    {0x595e39d1, 0x2724, 0x4663, {0x99, 0xb1, 0xda, 0x96, 0x9d, 0xe2, 0x83, 0x64}, "IDXGIOutput2"},
    {0x25483823, 0xcd46, 0x4c7d, {0x86, 0xca, 0x47, 0xaa, 0x95, 0xb8, 0x37, 0xbd}, "IDXGIFactory3"},
    {0x2633066b, 0x4514, 0x4c7a, {0x8f, 0xd8, 0x12, 0xea, 0x98, 0x05, 0x9d, 0x18}, "IDXGIDecodeSwapChain"},
    {0x41e7d1f2, 0xa591, 0x4f7b, {0xa2, 0xe5, 0xfa, 0x9c, 0x84, 0x3e, 0x1c, 0x12}, "IDXGIFactoryMedia"},
    {0xdd95b90b, 0xf05f, 0x4f6a, {0xbd, 0x65, 0x25, 0xbf, 0xb2, 0x64, 0xbd, 0x84}, "IDXGISwapChainMedia"},
    {0x8a6bb301, 0x7e7e, 0x41f4, {0xa8, 0xe0, 0x5b, 0x32, 0xf7, 0xf9, 0x9b, 0x18}, "IDXGIOutput3"},
    {0x94d99bdb, 0xf1f8, 0x4ab0, {0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1}, "IDXGISwapChain3"},
    {0xdc7dca35, 0x2196, 0x414d, {0x9f, 0x53, 0x61, 0x78, 0x84, 0x03, 0x2a, 0x60}, "IDXGIOutput4"},
    {0x1bc6ea02, 0xef36, 0x464f, {0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a}, "IDXGIFactory4"},
    {0x645967a4, 0x1392, 0x4310, {0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd}, "IDXGIAdapter3"},
    {0x80a07424, 0xab52, 0x42eb, {0x83, 0x3c, 0x0c, 0x42, 0xfd, 0x28, 0x2d, 0x98}, "IDXGIOutput5"},
    {0x3d585d5a, 0xbd4a, 0x489e, {0xb1, 0xf4, 0x3d, 0xbc, 0xb6, 0x45, 0x2f, 0xfb}, "IDXGISwapChain4"},
    {0x95b4f95f, 0xd8da, 0x4ca4, {0x9e, 0xe6, 0x3b, 0x76, 0xd5, 0x96, 0x8a, 0x10}, "IDXGIDevice4"},
    {0x7632e1f5, 0xee65, 0x4dca, {0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d}, "IDXGIFactory5"},
    {0x3c8d99d1, 0x4fbf, 0x4181, {0xa8, 0x2c, 0xaf, 0x66, 0xbf, 0x7b, 0xd2, 0x4e}, "IDXGIAdapter4"},
    {0x068346e8, 0xaaec, 0x4b84, {0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1}, "IDXGIOutput6"},
    {0xc1b6694f, 0xff09, 0x44a9, {0xb0, 0x3c, 0x77, 0x90, 0x0a, 0x0a, 0x1d, 0x17}, "IDXGIFactory6"},
    {0xa4966eed, 0x76db, 0x44da, {0x84, 0xc1, 0xee, 0x9a, 0x7a, 0xfb, 0x20, 0xa8}, "IDXGIFactory7"},
    {0x1841e5c8, 0x16b0, 0x489b, {0xbc, 0xc8, 0x44, 0xcf, 0xb0, 0xd5, 0xde, 0xae}, "ID3D11DeviceChild"},
    {0x4b35d0cd, 0x1e15, 0x4258, {0x9c, 0x98, 0x1b, 0x13, 0x33, 0xf6, 0xdd, 0x3b}, "ID3D11Asynchronous"},
    {0xd6c00747, 0x87b7, 0x425e, {0xb8, 0x4d, 0x44, 0xd1, 0x08, 0x56, 0x0a, 0xfd}, "ID3D11Query"},
    {0xdc8e63f3, 0xd12b, 0x4952, {0xb4, 0x7b, 0x5e, 0x45, 0x02, 0x6a, 0x86, 0x2d}, "ID3D11Resource"},
    {0x839d1216, 0xbb2e, 0x412b, {0xb7, 0xf4, 0xa9, 0xdb, 0xeb, 0xe0, 0x8e, 0xd1}, "ID3D11View"},
    {0x75b68faa, 0x347d, 0x4159, {0x8f, 0x45, 0xa0, 0x64, 0x0f, 0x01, 0xcd, 0x9a}, "ID3D11BlendState"},
    {0x48570b85, 0xd1ee, 0x4fcd, {0xa2, 0x50, 0xeb, 0x35, 0x07, 0x22, 0xb0, 0x37}, "ID3D11Buffer"},
    {0xa6cd7faa, 0xb0b7, 0x4a2f, {0x94, 0x36, 0x86, 0x62, 0xa6, 0x57, 0x97, 0xcb}, "ID3D11ClassInstance"},
    {0xddf57cba, 0x9543, 0x46e4, {0xa1, 0x2b, 0xf2, 0x07, 0xa0, 0xfe, 0x7f, 0xed}, "ID3D11ClassLinkage"},
    {0xa24bc4d1, 0x769e, 0x43f7, {0x80, 0x13, 0x98, 0xff, 0x56, 0x6c, 0x18, 0xe2}, "ID3D11CommandList"},
    {0x4f5b196e, 0xc2bd, 0x495e, {0xbd, 0x01, 0x1f, 0xde, 0xd3, 0x8e, 0x49, 0x69}, "ID3D11ComputeShader"},
    {0x6e8c49fb, 0xa371, 0x4770, {0xb4, 0x40, 0x29, 0x08, 0x60, 0x22, 0xb7, 0x41}, "ID3D11Counter"},
    {0x03823efb, 0x8d8f, 0x4e1c, {0x9a, 0xa2, 0xf6, 0x4b, 0xb2, 0xcb, 0xfd, 0xf1}, "ID3D11DepthStencilState"},
    {0x9fdac92a, 0x1876, 0x48c3, {0xaf, 0xad, 0x25, 0xb9, 0x4f, 0x84, 0xa9, 0xb6}, "ID3D11DepthStencilView"},
    {0xf582c508, 0x0f36, 0x490c, {0x99, 0x77, 0x31, 0xee, 0xce, 0x26, 0x8c, 0xfa}, "ID3D11DomainShader"},
    {0x38325b96, 0xeffb, 0x4022, {0xba, 0x02, 0x2e, 0x79, 0x5b, 0x70, 0x27, 0x5c}, "ID3D11GeometryShader"},
    {0x8e5c6061, 0x628a, 0x4c8e, {0x82, 0x64, 0xbb, 0xe4, 0x5c, 0xb3, 0xd5, 0xdd}, "ID3D11HullShader"},
    {0xe4819ddc, 0x4cf0, 0x4025, {0xbd, 0x26, 0x5d, 0xe8, 0x2a, 0x3e, 0x07, 0xb7}, "ID3D11InputLayout"},
    {0xea82e40d, 0x51dc, 0x4f33, {0x93, 0xd4, 0xdb, 0x7c, 0x91, 0x25, 0xae, 0x8c}, "ID3D11PixelShader"},
    {0x9eb576dd, 0x9f77, 0x4d86, {0x81, 0xaa, 0x8b, 0xab, 0x5f, 0xe4, 0x90, 0xe2}, "ID3D11Predicate"},
    {0x9bb4ab81, 0xab1a, 0x4d8f, {0xb5, 0x06, 0xfc, 0x04, 0x20, 0x0b, 0x6e, 0xe7}, "ID3D11RasterizerState"},
    {0xdfdba067, 0x0b8d, 0x4865, {0x87, 0x5b, 0xd7, 0xb4, 0x51, 0x6c, 0xc1, 0x64}, "ID3D11RenderTargetView"},
    {0xda6fea51, 0x564c, 0x4487, {0x98, 0x10, 0xf0, 0xd0, 0xf9, 0xb4, 0xe3, 0xa5}, "ID3D11SamplerState"},
    {0xb0e06fe0, 0x8192, 0x4e1a, {0xb1, 0xca, 0x36, 0xd7, 0x41, 0x47, 0x10, 0xb2}, "ID3D11ShaderResourceView"},
    {0xf8fb5c27, 0xc6b3, 0x4f75, {0xa4, 0xc8, 0x43, 0x9a, 0xf2, 0xef, 0x56, 0x4c}, "ID3D11Texture1D"},
    {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}, "ID3D11Texture2D"},
    {0x037e866e, 0xf56d, 0x4357, {0xa8, 0xaf, 0x9d, 0xab, 0xbe, 0x6e, 0x25, 0x0e}, "ID3D11Texture3D"},
    {0x28acf509, 0x7f5c, 0x48f6, {0x86, 0x11, 0xf3, 0x16, 0x01, 0x0a, 0x63, 0x80}, "ID3D11UnorderedAccessView"},
    {0x3b301d64, 0xd678, 0x4289, {0x88, 0x97, 0x22, 0xf8, 0x92, 0x8b, 0x72, 0xf3}, "ID3D11VertexShader"},
    {0xc0bfa96c, 0xe089, 0x44fb, {0x8e, 0xaf, 0x26, 0xf8, 0x79, 0x61, 0x90, 0xda}, "ID3D11DeviceContext"},
    {0x3015a308, 0xdcbd, 0x47aa, {0xa7, 0x47, 0x19, 0x24, 0x86, 0xd1, 0x4d, 0x4a}, "ID3D11AuthenticatedChannel"},
    {0x9b32f9ad, 0xbdcc, 0x40a6, {0xa3, 0x9d, 0xd5, 0xc8, 0x65, 0x84, 0x57, 0x20}, "ID3D11CryptoSession"},
    {0x3c9c5b51, 0x995d, 0x48d1, {0x9b, 0x8d, 0xfa, 0x5c, 0xae, 0xde, 0xd6, 0x5c}, "ID3D11VideoDecoder"},
    {0x31627037, 0x53ab, 0x4200, {0x90, 0x61, 0x05, 0xfa, 0xa9, 0xab, 0x45, 0xf9}, "ID3D11VideoProcessorEnumerator"},
    {0x1d7b0652, 0x185f, 0x41c6, {0x85, 0xce, 0x0c, 0x5b, 0xe3, 0xd4, 0xae, 0x6c}, "ID3D11VideoProcessor"},
    {0xc2931aea, 0x2a85, 0x4f20, {0x86, 0x0f, 0xfb, 0xa1, 0xfd, 0x25, 0x6e, 0x18}, "ID3D11VideoDecoderOutputView"},
    {0x11ec5a5f, 0x51dc, 0x4945, {0xab, 0x34, 0x6e, 0x8c, 0x21, 0x30, 0x0e, 0xa5}, "ID3D11VideoProcessorInputView"},
    {0xa048285e, 0x25a9, 0x4527, {0xbd, 0x93, 0xd6, 0x8b, 0x68, 0xc4, 0x42, 0x54}, "ID3D11VideoProcessorOutputView"},
    {0x10ec4d5b, 0x975a, 0x4689, {0xb9, 0xe4, 0xd0, 0xaa, 0xc3, 0x0f, 0xe3, 0x33}, "ID3D11VideoDevice"},
    {0x61f21c45, 0x3c0e, 0x4a74, {0x9c, 0xea, 0x67, 0x10, 0x0d, 0x9a, 0xd5, 0xe4}, "ID3D11VideoContext"},
    {0xdb6f6ddb, 0xac77, 0x4e88, {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}, "ID3D11Device"},
    {0xcc86fabe, 0xda55, 0x401d, {0x85, 0xe7, 0xe3, 0xc9, 0xde, 0x28, 0x77, 0xe9}, "ID3D11BlendState1"},
    {0x5c1e0d8a, 0x7c23, 0x48f9, {0x8c, 0x59, 0xa9, 0x29, 0x58, 0xce, 0xff, 0x11}, "ID3DDeviceContextState"},
    {0xbb2c6faa, 0xb5fb, 0x4082, {0x8e, 0x6b, 0x38, 0x8b, 0x8c, 0xfa, 0x90, 0xe1}, "ID3D11DeviceContext1"},
    {0xa7f026da, 0xa5f8, 0x4487, {0xa5, 0x64, 0x15, 0xe3, 0x43, 0x57, 0x65, 0x1e}, "ID3D11VideoContext1"},
    {0x29da1d51, 0x1321, 0x4454, {0x80, 0x4b, 0xf5, 0xfc, 0x9f, 0x86, 0x1f, 0x0f}, "ID3D11VideoDevice1"},
    {0x465217f2, 0x5568, 0x43cf, {0xb5, 0xb9, 0xf6, 0x1d, 0x54, 0x53, 0x1c, 0xa1}, "ID3D11VideoProcessorEnumerator1"},
    {0xb2daad8b, 0x03d4, 0x4dbf, {0x95, 0xeb, 0x32, 0xab, 0x4b, 0x63, 0xd0, 0xab}, "ID3DUserDefinedAnnotation"},
    {0x1217d7a6, 0x5039, 0x418c, {0xb0, 0x42, 0x9c, 0xbe, 0x25, 0x6a, 0xfd, 0x6e}, "ID3D11RasterizerState1"},
    {0xa04bfb29, 0x08ef, 0x43d6, {0xa4, 0x9c, 0xa9, 0xbd, 0xbd, 0xcb, 0xe6, 0x86}, "ID3D11Device1"},
    {0x420d5b32, 0xb90c, 0x4da4, {0xbe, 0xf0, 0x35, 0x9f, 0x6a, 0x24, 0xa8, 0x3a}, "ID3D11DeviceContext2"},
    {0x9d06dffa, 0xd1e5, 0x4d07, {0x83, 0xa8, 0x1b, 0xb1, 0x23, 0xf2, 0xf8, 0x41}, "ID3D11Device2"},
    {0x51218251, 0x1e33, 0x4617, {0x9c, 0xcb, 0x4d, 0x3a, 0x43, 0x67, 0xe7, 0xbb}, "ID3D11Texture2D1"},
    {0x0c711683, 0x2853, 0x4846, {0x9b, 0xb0, 0xf3, 0xe6, 0x06, 0x39, 0xe4, 0x6a}, "ID3D11Texture3D1"},
    {0x6fbd02fb, 0x209f, 0x46c4, {0xb0, 0x59, 0x2e, 0xd1, 0x55, 0x86, 0xa6, 0xac}, "ID3D11RasterizerState2"},
    {0x91308b87, 0x9040, 0x411d, {0x8c, 0x67, 0xc3, 0x92, 0x53, 0xce, 0x38, 0x02}, "ID3D11ShaderResourceView1"},
    {0xffbe2e23, 0xf011, 0x418a, {0xac, 0x56, 0x5c, 0xee, 0xd7, 0xc5, 0xb9, 0x4b}, "ID3D11RenderTargetView1"},
    {0x7b3b6153, 0xa886, 0x4544, {0xab, 0x37, 0x65, 0x37, 0xc8, 0x50, 0x04, 0x03}, "ID3D11UnorderedAccessView1"},
    {0x631b4766, 0x36dc, 0x461d, {0x8d, 0xb6, 0xc4, 0x7e, 0x13, 0xe6, 0x09, 0x16}, "ID3D11Query1"},
    {0xb4e3c01d, 0xe79e, 0x4637, {0x91, 0xb2, 0x51, 0x0e, 0x9f, 0x4c, 0x9b, 0x8f}, "ID3D11DeviceContext3"},
    {0xaffde9d1, 0x1df7, 0x4bb7, {0x8a, 0x34, 0x0f, 0x46, 0x25, 0x1d, 0xab, 0x80}, "ID3D11Fence"},
    {0x917600da, 0xf58c, 0x4c33, {0x98, 0xd8, 0x3e, 0x15, 0xb3, 0x90, 0xfa, 0x24}, "ID3D11DeviceContext4"},
    {0xa05c8c37, 0xd2c6, 0x4732, {0xb3, 0xa0, 0x9c, 0xe0, 0xb0, 0xdc, 0x9a, 0xe6}, "ID3D11Device3"},
    {0x8992ab71, 0x02e6, 0x4b8d, {0xba, 0x48, 0xb0, 0x56, 0xdc, 0xda, 0x42, 0xc4}, "ID3D11Device4"},
    {0x8ffde202, 0xa0e7, 0x45df, {0x9e, 0x01, 0xe8, 0x37, 0x80, 0x1b, 0x5e, 0xa0}, "ID3D11Device5"},
    {0x9b7e4e00, 0x342c, 0x4106, {0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0}, "ID3D11Multithread"},
    {0xc4e7374c, 0x6243, 0x4d1b, {0xae, 0x87, 0x52, 0xb4, 0xf7, 0x40, 0xe2, 0x61}, "ID3D11VideoContext2"},
};

#endif
