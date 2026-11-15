// DXVK pour Android (RomCloud Native) : fenêtre et écran d'Android pour DXVK (pilote WSI).
//
// Un seul écran et une seule fenêtre : l'ANativeWindow que l'application donne au runtime
// (dxvk_android_set_window). Le jeu Windows passe son HWND (fictif) ; tout HWND désigne cette fenêtre.
#pragma once

#include "../wsi_platform.h"

struct ANativeWindow;

namespace dxvk::wsi {

  class AndroidWsiDriver : public WsiDriver {
  public:
    std::vector<const char *> getInstanceExtensions() override;

    HMONITOR getDefaultMonitor() override;
    HMONITOR enumMonitors(uint32_t index) override;
    HMONITOR enumMonitors(const LUID *adapterLUID[], uint32_t numLUIDs, uint32_t index) override;
    bool getDisplayName(HMONITOR hMonitor, WCHAR (&Name)[32]) override;
    bool getDesktopCoordinates(HMONITOR hMonitor, RECT* pRect) override;
    bool getDisplayMode(HMONITOR hMonitor, uint32_t modeNumber, WsiMode* pMode) override;
    bool getCurrentDisplayMode(HMONITOR hMonitor, WsiMode* pMode) override;
    bool getDesktopDisplayMode(HMONITOR hMonitor, WsiMode* pMode) override;
    WsiEdidData getMonitorEdid(HMONITOR hMonitor) override;

    void getWindowSize(HWND hWindow, uint32_t* pWidth, uint32_t* pHeight) override;
    void resizeWindow(HWND hWindow, DxvkWindowState* pState, uint32_t width, uint32_t height) override;
    bool setWindowMode(HMONITOR hMonitor, HWND hWindow, DxvkWindowState* pState, const WsiMode& mode) override;
    bool enterFullscreenMode(HMONITOR hMonitor, HWND hWindow, DxvkWindowState* pState, bool modeSwitch) override;
    bool leaveFullscreenMode(HWND hWindow, DxvkWindowState* pState, bool restoreCoordinates) override;
    bool restoreDisplayMode() override;
    HMONITOR getWindowMonitor(HWND hWindow) override;
    bool isWindow(HWND hWindow) override;
    bool isMinimized(HWND hWindow) override;
    bool isOccluded(HWND hWindow) override;
    void updateFullscreenWindow(HMONITOR hMonitor, HWND hWindow, bool forceTopmost) override;
    VkResult createSurface(HWND hWindow, PFN_vkGetInstanceProcAddr pfnVkGetInstanceProcAddr,
                           VkInstance instance, VkSurfaceKHR* pSurface) override;
  };

}
