// Essai de DXVK dans l'application : pilote Vulkan choisi (Turnip sous le chargeur d'Android, voir
// rcn_vulkan.c) donné aux bibliothèques de DXVK, appareil Direct3D 11 sans fenêtre, effacement d'une
// texture et relecture d'un pixel. Résultat en texte (fenêtre « Pilote Vulkan »).
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>

#include <d3d11.h>
#include <dxgi.h>

extern "C" {
#include "rcn_vulkan.h"
}

typedef HRESULT(WINAPI *PFN_D3D11CreateDevice_)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef void (*PFN_set_loader)(PFN_vkGetInstanceProcAddr);

extern "C" int rcn_dxvk_test(char *out, size_t size) {
  RcnVulkan vk;
  char error[512];
  if (!rcn_vk_open_configured(&vk, error, sizeof error)) {
    snprintf(out, size, "pilote : %s", error);
    return 0;
  }
  void *dxgi = dlopen("libdxvk_dxgi.so", RTLD_NOW | RTLD_GLOBAL);
  void *d3d11 = dxgi ? dlopen("libdxvk_d3d11.so", RTLD_NOW | RTLD_GLOBAL) : nullptr;
  if (!d3d11) {
    snprintf(out, size, "DXVK absent de l'application : %s", dlerror());
    return 0;
  }
  // Chacune des deux bibliothèques a sa copie du chargeur Vulkan de DXVK.
  for (void *lib : { dxgi, d3d11 }) {
    auto set = reinterpret_cast<PFN_set_loader>(dlsym(lib, "dxvk_android_set_vulkan_loader"));
    if (set) set(vk.get_instance_proc);
  }
  auto create = reinterpret_cast<PFN_D3D11CreateDevice_>(dlsym(d3d11, "D3D11CreateDevice"));
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0, got = D3D_FEATURE_LEVEL_9_1;
  HRESULT hr = create ? create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, &got, &context) : E_FAIL;
  if (FAILED(hr)) {
    snprintf(out, size, "DXVK : D3D11CreateDevice a échoué (0x%08x)", unsigned(hr));
    return 0;
  }
  // Adaptateur vu par le jeu.
  char adapter[128] = "?";
  IDXGIDevice *dxgi_device = nullptr;
  if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_device)))) {
    IDXGIAdapter *a = nullptr;
    DXGI_ADAPTER_DESC desc;
    if (SUCCEEDED(dxgi_device->GetAdapter(&a)) && SUCCEEDED(a->GetDesc(&desc))) {
      size_t i = 0;
      for (; i + 1 < sizeof adapter && desc.Description[i]; i++) adapter[i] = char(desc.Description[i]);
      adapter[i] = 0;
      a->Release();
    }
    dxgi_device->Release();
  }

  D3D11_TEXTURE2D_DESC td = {};
  td.Width = 64;
  td.Height = 64;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET;
  ID3D11Texture2D *target = nullptr, *staging = nullptr;
  ID3D11RenderTargetView *view = nullptr;
  device->CreateTexture2D(&td, nullptr, &target);
  device->CreateRenderTargetView(target, nullptr, &view);
  const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
  context->ClearRenderTargetView(view, color);
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  device->CreateTexture2D(&td, nullptr, &staging);
  context->CopyResource(staging, target);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  unsigned px[4] = { 0, 0, 0, 0 };
  hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
  if (SUCCEEDED(hr)) {
    const unsigned char *p = static_cast<const unsigned char *>(mapped.pData);
    for (int i = 0; i < 4; i++) px[i] = p[i];
    context->Unmap(staging, 0);
  }
  snprintf(out, size, "DXVK : Direct3D 11 (niveau %x.%x) sur « %s »\neffacement et relecture : %u %u %u %u (attendu 64 128 191 255)",
           unsigned(got) >> 12, (unsigned(got) >> 8) & 0xF, adapter, px[0], px[1], px[2], px[3]);
  staging->Release();
  view->Release();
  target->Release();
  context->Release();
  device->Release();
  return px[0] == 64 && px[1] == 128 && px[2] == 191;
}
