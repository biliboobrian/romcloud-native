// Essai de DXVK pour Android : création d'un appareil Direct3D 11 (sans fenêtre), effacement d'une texture
// et relecture d'un pixel. Pilote Vulkan : celui du système, ou celui de RCN_VULKAN_DRIVER (Turnip).
//
// Compilation (NDK, C++) : -I <dxvk>/include/native -I <dxvk>/include/native/windows
//   -I <dxvk>/include/native/directx, liée à libdxvk_d3d11.so et libdxvk_dxgi.so.
#include <cstdio>

#include <d3d11.h>

int main() {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context);
  std::printf("D3D11CreateDevice : 0x%08x\n", unsigned(hr));
  if (FAILED(hr)) return 1;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 64;
  desc.Height = 64;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ID3D11Texture2D* target = nullptr;
  ID3D11RenderTargetView* view = nullptr;
  device->CreateTexture2D(&desc, nullptr, &target);
  device->CreateRenderTargetView(target, nullptr, &view);
  const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
  context->ClearRenderTargetView(view, color);

  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging = nullptr;
  device->CreateTexture2D(&desc, nullptr, &staging);
  context->CopyResource(staging, target);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
  if (SUCCEEDED(hr)) {
    const unsigned char* p = static_cast<const unsigned char*>(mapped.pData);
    std::printf("pixel : %u %u %u %u (attendu 64 128 191 255)\n", p[0], p[1], p[2], p[3]);
    context->Unmap(staging, 0);
  }
  staging->Release();
  view->Release();
  target->Release();
  context->Release();
  device->Release();
  return 0;
}
