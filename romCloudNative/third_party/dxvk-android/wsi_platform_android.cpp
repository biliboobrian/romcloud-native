// DXVK pour Android (RomCloud Native) : pilote WSI d'Android. Voir wsi_platform_android.h.
#if defined(DXVK_WSI_ANDROID)

#include "wsi_platform_android.h"

#include <android/native_window.h>
#include <vulkan/vulkan_android.h>

#include <atomic>
#include <cstring>

namespace {
  // Fenêtre de l'application (donnée par le runtime) et taille de l'écran annoncée au jeu.
  std::atomic<ANativeWindow*> g_window { nullptr };
  std::atomic<uint32_t> g_width { 1920 }, g_height { 1080 };
  const HMONITOR g_monitor = reinterpret_cast<HMONITOR>(uintptr_t(1));
}

// Appelé par le runtime quand la surface d'affichage change (NULL quand elle disparaît). La taille de
// l'écran annoncée au jeu reste celle donnée (jeu en plein écran, image mise à l'échelle par le pilote).
extern "C" __attribute__((visibility("default"))) void dxvk_android_set_window(ANativeWindow* window, uint32_t width, uint32_t height) {
  g_window.store(window);
  if (width && height) {
    g_width.store(width);
    g_height.store(height);
  }
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

  static void currentMode(WsiMode* pMode) {
    pMode->width = g_width.load();
    pMode->height = g_height.load();
    pMode->refreshRate = WsiRational { 60, 1 };
    pMode->bitsPerPixel = 32;
    pMode->interlaced = false;
  }

  bool AndroidWsiDriver::getDisplayMode(HMONITOR hMonitor, uint32_t modeNumber, WsiMode* pMode) {
    if (hMonitor != g_monitor || modeNumber != 0) return false;
    currentMode(pMode);
    return true;
  }
  bool AndroidWsiDriver::getCurrentDisplayMode(HMONITOR hMonitor, WsiMode* pMode) { return getDisplayMode(hMonitor, 0, pMode); }
  bool AndroidWsiDriver::getDesktopDisplayMode(HMONITOR hMonitor, WsiMode* pMode) { return getDisplayMode(hMonitor, 0, pMode); }
  WsiEdidData AndroidWsiDriver::getMonitorEdid(HMONITOR) { return {}; }

  void AndroidWsiDriver::getWindowSize(HWND, uint32_t* pWidth, uint32_t* pHeight) {
    ANativeWindow* window = g_window.load();
    uint32_t w = window ? uint32_t(ANativeWindow_getWidth(window)) : g_width.load();
    uint32_t h = window ? uint32_t(ANativeWindow_getHeight(window)) : g_height.load();
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
