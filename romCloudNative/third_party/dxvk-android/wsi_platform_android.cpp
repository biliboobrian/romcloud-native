// DXVK pour Android (RomCloud Native) : pilote WSI d'Android. Voir wsi_platform_android.h.
#if defined(DXVK_WSI_ANDROID)

#include "wsi_platform_android.h"

#include <android/native_window.h>
#include <vulkan/vulkan_android.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
  // Fenêtre de l'application (donnée par le runtime). L'écran annoncé au jeu est celui de la couche
  // OpenGL ES (rcn_dxgi.c) : 1920 × 1080 à 60 Hz, ou la résolution choisie pour le jeu dans l'application
  // (RCN_SCREEN=<largeur>x<hauteur>). Modes proposés : les courants (960 × 540 à 1920 × 1080) et l'écran,
  // comme sur un vrai moniteur — un jeu qui ne retrouve pas la résolution de ses réglages dans la liste peut
  // planter (Dishonored, 1280 × 720 enregistré, écran annoncé 960 × 540). La chaîne d'images prend la taille
  // de l'écran annoncé (createSurface) ; l'affichage d'Android la met à l'échelle de la fenêtre.
  std::atomic<ANativeWindow*> g_window { nullptr };
  const HMONITOR g_monitor = reinterpret_cast<HMONITOR>(uintptr_t(1));

  struct Screen {
    uint32_t width = 1920, height = 1080;
    uint32_t modes[5][2] = {};
    uint32_t count = 0;
    Screen() {
      const char* v = std::getenv("RCN_SCREEN");
      unsigned x = 0, y = 0;
      if (v && std::sscanf(v, "%ux%u", &x, &y) == 2 && x >= 320 && y >= 200 && x <= 7680 && y <= 4320)
        width = x, height = y;
      // Du plus petit au plus grand, l'écran annoncé à sa place s'il n'est pas déjà dans la liste.
      const uint32_t candidates[][2] = { { 960, 540 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 } };
      bool added = false;
      for (const auto& c : candidates) {
        if (!added && (width < c[0] || (width == c[0] && height <= c[1]))) {
          if (width != c[0] || height != c[1]) modes[count][0] = width, modes[count][1] = height, count++;
          added = true;
        }
        modes[count][0] = c[0], modes[count][1] = c[1], count++;
      }
      if (!added) modes[count][0] = width, modes[count][1] = height, count++;
    }
  };
  const Screen& screen() {
    static const Screen s;
    return s;
  }
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
    pRect->right = LONG(screen().width);
    pRect->bottom = LONG(screen().height);
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
    if (hMonitor != g_monitor || modeNumber >= screen().count) return false;
    modeOf(pMode, screen().modes[modeNumber][0], screen().modes[modeNumber][1]);
    return true;
  }
  bool AndroidWsiDriver::getCurrentDisplayMode(HMONITOR hMonitor, WsiMode* pMode) {
    if (hMonitor != g_monitor) return false;
    modeOf(pMode, screen().width, screen().height);
    return true;
  }
  bool AndroidWsiDriver::getDesktopDisplayMode(HMONITOR hMonitor, WsiMode* pMode) { return getCurrentDisplayMode(hMonitor, pMode); }
  WsiEdidData AndroidWsiDriver::getMonitorEdid(HMONITOR) { return {}; }

  void AndroidWsiDriver::getWindowSize(HWND, uint32_t* pWidth, uint32_t* pHeight) {
    ANativeWindow* window = g_window.load();
    // Taille de l'écran annoncé : la chaîne d'images garde la taille demandée par le jeu, la surface
    // d'Android la met à l'échelle de la fenêtre.
    (void)window;
    uint32_t w = screen().width, h = screen().height;
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
    // Tampons de la surface à la taille de l'écran annoncé au jeu (RCN_SCREEN) : DXVK prend la taille de la
    // surface pour sa chaîne d'images ; sans cela, il agrandit chaque image à 1920 × 1080 sur le processeur
    // graphique, alors que l'affichage d'Android met la surface à l'échelle sans rien lui coûter.
    // RCN_WSI_NATIVE_SIZE=1 : tampons laissés à la taille de la fenêtre (comportement d'avant).
    if (!std::getenv("RCN_WSI_NATIVE_SIZE"))
      ANativeWindow_setBuffersGeometry(window, int32_t(screen().width), int32_t(screen().height), 0);
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
