// DXVK pour Android (RomCloud Native) : pilote WSI d'Android. Voir wsi_platform_android.h.
#if defined(DXVK_WSI_ANDROID)

#include "wsi_platform_android.h"

#include <android/native_window.h>
#include <vulkan/vulkan_android.h>

#include <atomic>
#include <cstring>

namespace {
  // Fenêtre de l'application (donnée par le runtime). L'écran annoncé au jeu est fixe, comme celui de la
  // couche OpenGL ES (rcn_dxgi.c) : 1920 × 1080 à 60 Hz, modes 1280 × 720, 1600 × 900 et 1920 × 1080 ;
  // l'image est mise à l'échelle de la fenêtre par la chaîne d'images.
  std::atomic<ANativeWindow*> g_window { nullptr };
  std::atomic<uint32_t> g_width { 1920 }, g_height { 1080 };
  const uint32_t g_modes[][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 } };
  const HMONITOR g_monitor = reinterpret_cast<HMONITOR>(uintptr_t(1));
}

// Appelé par le runtime quand la surface d'affichage change (NULL quand elle disparaît). La taille de la
// fenêtre ne change pas l'écran annoncé au jeu.
extern "C" __attribute__((visibility("default"))) void dxvk_android_set_window(ANativeWindow* window, uint32_t, uint32_t) {
  g_window.store(window);
}

namespace dxvk::wsi {

  std::vector<const char *> AndroidWsiDriver::getInstanceExtensions() {
    return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
  }

  HMONITOR AndroidWsiDriver::getDefaultMonitor() { return g_monitor; }
  HMONITOR AndroidWsiDriver::enumMonitors(uint32_t index) { return index == 0 ? g_monitor : nullptr; }
  HMONITOR AndroidWsiDriver::enumMonitors(const LUID*[], uint32_t, uint32_t index) { return enumMonitors(index); }

  bool AndroidWsiDriver::getDisplayName(HMONITOR hMonitor, WCHAR (&Name)[32]) {
    static const char name[] = "\\\\.\\DISPLAY1";
    for (size_t i = 0; i < sizeof name; i++) Name[i] = WCHAR(name[i]);
    return hMonitor == g_monitor;
  }

  bool AndroidWsiDriver::getDesktopCoordinates(HMONITOR hMonitor, RECT* pRect) {
    pRect->left = 0;
    pRect->top = 0;
    pRect->right = LONG(g_width.load());
    pRect->bottom = LONG(g_height.load());
    return hMonitor == g_monitor;
  }

  static void modeOf(WsiMode* pMode, uint32_t w, uint32_t h) {
    pMode->width = w;
    pMode->height = h;
    pMode->refreshRate = WsiRational { 60, 1 };
    pMode->bitsPerPixel = 32;
    pMode->interlaced = false;
  }

  bool AndroidWsiDriver::getDisplayMode(HMONITOR hMonitor, uint32_t modeNumber, WsiMode* pMode) {
    if (hMonitor != g_monitor || modeNumber >= sizeof g_modes / sizeof g_modes[0]) return false;
    modeOf(pMode, g_modes[modeNumber][0], g_modes[modeNumber][1]);
    return true;
  }
  bool AndroidWsiDriver::getCurrentDisplayMode(HMONITOR hMonitor, WsiMode* pMode) {
    if (hMonitor != g_monitor) return false;
    modeOf(pMode, g_width.load(), g_height.load());
    return true;
  }
  bool AndroidWsiDriver::getDesktopDisplayMode(HMONITOR hMonitor, WsiMode* pMode) { return getCurrentDisplayMode(hMonitor, pMode); }
  WsiEdidData AndroidWsiDriver::getMonitorEdid(HMONITOR) { return {}; }

  void AndroidWsiDriver::getWindowSize(HWND, uint32_t* pWidth, uint32_t* pHeight) {
    ANativeWindow* window = g_window.load();
    // Taille de l'écran annoncé : la chaîne d'images garde la taille demandée par le jeu, la surface
    // d'Android la met à l'échelle de la fenêtre.
    (void)window;
    uint32_t w = g_width.load(), h = g_height.load();
    if (pWidth) *pWidth = w;
    if (pHeight) *pHeight = h;
  }

  // La fenêtre d'Android a toujours la taille de l'écran : rien à changer.
  void AndroidWsiDriver::resizeWindow(HWND, DxvkWindowState*, uint32_t, uint32_t) { }
  bool AndroidWsiDriver::setWindowMode(HMONITOR, HWND, DxvkWindowState*, const WsiMode&) { return true; }
  bool AndroidWsiDriver::enterFullscreenMode(HMONITOR, HWND, DxvkWindowState*, bool) { return true; }
  bool AndroidWsiDriver::leaveFullscreenMode(HWND, DxvkWindowState*, bool) { return true; }
  bool AndroidWsiDriver::restoreDisplayMode() { return true; }
  HMONITOR AndroidWsiDriver::getWindowMonitor(HWND) { return g_monitor; }
  bool AndroidWsiDriver::isWindow(HWND hWindow) { return hWindow != nullptr; }
  bool AndroidWsiDriver::isMinimized(HWND) { return g_window.load() == nullptr; }
  bool AndroidWsiDriver::isOccluded(HWND) { return g_window.load() == nullptr; }
  void AndroidWsiDriver::updateFullscreenWindow(HMONITOR, HWND, bool) { }

  VkResult AndroidWsiDriver::createSurface(HWND, PFN_vkGetInstanceProcAddr pfnVkGetInstanceProcAddr,
                                           VkInstance instance, VkSurfaceKHR* pSurface) {
    ANativeWindow* window = g_window.load();
    if (!window) return VK_ERROR_SURFACE_LOST_KHR;
    auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(pfnVkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkAndroidSurfaceCreateInfoKHR info = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
    info.window = window;
    return create(instance, &info, nullptr, pSurface);
  }

  static bool createAndroidWsiDriver(WsiDriver **driver) {
    *driver = new AndroidWsiDriver();
    return true;
  }

  WsiBootstrap AndroidWSI = {
    "Android",
    createAndroidWsiDriver
  };

}

#endif
