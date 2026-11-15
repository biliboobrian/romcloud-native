// RomCloud Native : accès à Vulkan (rcn_vulkan.c) — Android seulement pour l'instant (en-têtes Vulkan du
// NDK) ; ailleurs ce module est vide.
#ifndef RCN_VULKAN_H
#define RCN_VULKAN_H

#if defined(__ANDROID__)
#define RCN_HAS_VULKAN 1
#include <stddef.h>
#include <vulkan/vulkan.h>

typedef struct {
    void *library;
    PFN_vkGetInstanceProcAddr get_instance_proc;
    int custom_driver;      // pilote fourni par l'utilisateur (Turnip…), sinon celui du système
    unsigned icd_interface; // version d'interface d'ICD négociée (pilote fourni)
    char path[512];
} RcnVulkan;

// Ouvre le pilote : `driver_path` (fichier .so d'un pilote Vulkan, ex. libvulkan_freedreno.so de Turnip),
// ou le pilote du système si NULL / vide. 0 et message dans `error` en cas d'échec.
int rcn_vk_open(RcnVulkan *vk, const char *driver_path, char *error, size_t error_size);
// Pilote choisi par l'utilisateur (variable RCN_VULKAN_DRIVER, posée par l'application), sinon celui du système.
int rcn_vk_open_configured(RcnVulkan *vk, char *error, size_t error_size);
// Résumé du pilote (texte sur quelques lignes) : nom, version de Vulkan, points utiles à DXVK. 0 en cas
// d'échec (message dans `out`).
int rcn_vk_describe(const char *driver_path, char *out, size_t out_size);
// Fonctions globales (vkCreateInstance, vkEnumerateInstance…) puis d'instance.
PFN_vkVoidFunction rcn_vk_global(RcnVulkan *vk, const char *name);
PFN_vkVoidFunction rcn_vk_instance(RcnVulkan *vk, VkInstance instance, const char *name);
#endif

#endif
