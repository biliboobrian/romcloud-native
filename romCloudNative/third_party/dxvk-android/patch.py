"""DXVK pour Android (RomCloud Native) : adaptation des sources de DXVK (2.7.1).

    python patch.py <dossier des sources de DXVK>

Changements (idempotents : les sources déjà adaptées ne changent pas) :
  - pilote WSI d'Android (src/wsi/android), choisi par DXVK_WSI_DRIVER=Android ;
  - meson.build : construction « native » sans SDL ni GLFW quand le système visé est android ;
  - chargeur Vulkan : pilote choisi par l'utilisateur (RCN_VULKAN_DRIVER, ex. Turnip) chargé par son point
    d'entrée d'ICD, avant le libvulkan.so du système.
"""
import os
import shutil
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = sys.argv[1]


def patch(rel, pairs):
    path = os.path.join(root, rel)
    text = open(path, encoding='utf-8').read()
    changed = False
    for old, new in pairs:
        if new in text:
            continue
        if old not in text:
            sys.exit(f'{rel} : texte attendu introuvable (version de DXVK différente ?) :\n{old[:200]}')
        text = text.replace(old, new, 1)
        changed = True
    if changed:
        open(path, 'w', encoding='utf-8', newline='\n').write(text)
        print('adapté :', rel)


# Pilote WSI d'Android.
os.makedirs(os.path.join(root, 'src/wsi/android'), exist_ok=True)
for name in ('wsi_platform_android.h', 'wsi_platform_android.cpp'):
    shutil.copyfile(os.path.join(here, name), os.path.join(root, 'src/wsi/android', name))

patch('src/wsi/wsi_platform.h', [(
    '''#if defined(DXVK_WSI_GLFW)
  extern WsiBootstrap GlfwWSI;
#endif''', '''#if defined(DXVK_WSI_GLFW)
  extern WsiBootstrap GlfwWSI;
#endif
#if defined(DXVK_WSI_ANDROID)
  extern WsiBootstrap AndroidWSI;
#endif''')])

patch('src/wsi/wsi_platform.cpp', [(
    '''#if defined(DXVK_WSI_GLFW)
    &GlfwWSI,
#endif
  };''', '''#if defined(DXVK_WSI_GLFW)
    &GlfwWSI,
#endif
#if defined(DXVK_WSI_ANDROID)
    &AndroidWSI,
#endif
  };'''), (
    '''#if defined(DXVK_WSI_WIN32)
        hint = "Win32";
#else''', '''#if defined(DXVK_WSI_WIN32)
        hint = "Win32";
#elif defined(DXVK_WSI_ANDROID)
        hint = "Android";
#else''')])

patch('src/wsi/meson.build', [(
    "  'glfw/wsi_window_glfw.cpp',\n]",
    "  'glfw/wsi_window_glfw.cpp',\n  'android/wsi_platform_android.cpp',\n]")])

patch('meson.build', [(
    '''  if (not lib_sdl3.found() and not lib_sdl2.found() and not lib_glfw.found())
    error('SDL3, SDL2, or GLFW are required to build dxvk-native')
  endif''', '''  if platform == 'android'
    # Android : fenêtre et surface Vulkan d'Android (src/wsi/android).
    compiler_args += ['-DDXVK_WSI_ANDROID']
  elif (not lib_sdl3.found() and not lib_sdl2.found() and not lib_glfw.found())
    error('SDL3, SDL2, or GLFW are required to build dxvk-native')
  endif'''), (
    '''  link_args += [
    '-static-libgcc',
    '-static-libstdc++',
  ]''', '''  link_args += [
    '-static-libgcc',
    '-static-libstdc++',
  ]
  if platform == 'android'
    link_args += [ '-landroid', '-llog' ]
  endif''')])

patch('src/vulkan/vulkan_loader.cpp', [(
    '''  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadVulkanLibrary() {''',
    '''#if defined(__ANDROID__)
  // Point d'entrée du pilote donné par le runtime (dxvk_android_set_vulkan_loader), avant toute création.
  static PFN_vkGetInstanceProcAddr g_androidLoader = nullptr;

  // Pilote Vulkan choisi par l'utilisateur (RCN_VULKAN_DRIVER, posé par RomCloud Native : Turnip…), chargé
  // par son point d'entrée d'ICD sans passer par le chargeur d'Android.
  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadCustomDriver() {
    const char* path = getenv("RCN_VULKAN_DRIVER");
    if (!path || !*path)
      return { };
    HMODULE library = LoadLibraryA(path);
    if (!library) {
      Logger::err(str::format("Vulkan: failed to load driver ", path));
      return { };
    }
    using PFN_negotiate = VkResult (VKAPI_PTR *)(uint32_t*);
    auto negotiate = reinterpret_cast<PFN_negotiate>(GetProcAddress(library, "vk_icdNegotiateLoaderICDInterfaceVersion"));
    if (negotiate) {
      uint32_t version = 5;
      negotiate(&version);
    }
    auto proc = GetProcAddress(library, "vk_icdGetInstanceProcAddr");
    if (!proc)
      proc = GetProcAddress(library, "vkGetInstanceProcAddr");
    if (!proc) {
      FreeLibrary(library);
      return { };
    }
    Logger::info(str::format("Vulkan: using driver ", path));
    return std::make_pair(library, reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc));
  }
#endif

  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadVulkanLibrary() {
#if defined(__ANDROID__)
    // Pilote déjà chargé par le runtime (espace de noms du système, format HAL d'Android).
    if (g_androidLoader)
      return std::make_pair(HMODULE(nullptr), g_androidLoader);
    auto custom = loadCustomDriver();
    if (custom.second)
      return custom;
#endif'''), ('#include <tuple>\n', '#include <cstdlib>\n#include <tuple>\n')])

# Fenêtre d'Android donnée par le runtime : exportée par les deux bibliothèques (chacune contient sa copie
# du pilote WSI, liée statiquement), que le runtime appelle toutes deux.
patch('src/vulkan/vulkan_loader.cpp', [(
    '\nnamespace dxvk::vk {\n',
    '''
#if defined(__ANDROID__)
namespace dxvk::vk { void setAndroidLoader(PFN_vkGetInstanceProcAddr proc); }
// Appelé par le runtime avec le vkGetInstanceProcAddr du pilote qu'il a chargé.
extern "C" __attribute__((visibility("default"))) void dxvk_android_set_vulkan_loader(PFN_vkGetInstanceProcAddr proc) {
  dxvk::vk::setAndroidLoader(proc);
}
#endif

namespace dxvk::vk {
'''), (
    '  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadCustomDriver() {',
    '''  void setAndroidLoader(PFN_vkGetInstanceProcAddr proc) { g_androidLoader = proc; }

  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadCustomDriver() {''')])

patch('src/dxgi/dxgi.sym', [('    DXGIGetDebugInterface1;\n', '    DXGIGetDebugInterface1;\n    dxvk_android_set_window;\n')])
patch('src/d3d11/d3d11.sym', [('    D3D11On12CreateDevice;\n', '    D3D11On12CreateDevice;\n    dxvk_android_set_window;\n')])
for sym in ('src/dxgi/dxgi.sym', 'src/d3d11/d3d11.sym'):
    patch(sym, [('    dxvk_android_set_window;\n', '    dxvk_android_set_window;\n    dxvk_android_set_vulkan_loader;\n')])

# Android rend VK_SUBOPTIMAL_KHR à chaque image quand la chaîne d'images n'a pas la rotation de l'écran
# (preTransform identité en paysage) : l'image reste correcte, le compositeur la tourne. Sans ceci, DXVK
# recrée sa chaîne d'images à chaque image.
patch('src/dxvk/dxvk_presenter.cpp', [(
    '''    // This is a normal occurence, but may be useful for
    // debugging purposes in case WSI goes wrong somehow.
''', '''#if defined(DXVK_WSI_ANDROID)
    if (m_acquireStatus == VK_SUBOPTIMAL_KHR)
      m_acquireStatus = VK_SUCCESS;
#endif

    // This is a normal occurence, but may be useful for
    // debugging purposes in case WSI goes wrong somehow.
'''), (
    '''    std::lock_guard lock(m_surfaceMutex);

    if (status != VK_SUCCESS) {''', '''    std::lock_guard lock(m_surfaceMutex);

#if defined(DXVK_WSI_ANDROID)
    if (status == VK_SUBOPTIMAL_KHR)
      status = VK_SUCCESS;
    if (m_acquireStatus == VK_SUBOPTIMAL_KHR)
      m_acquireStatus = VK_SUCCESS;
#endif

    if (status != VK_SUCCESS) {''')])

print('sources de DXVK prêtes pour Android')
