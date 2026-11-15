// RomCloud Native : accès à Vulkan — pilote du système (libvulkan.so) ou pilote Vulkan fourni par
// l'utilisateur (Turnip, pilote libre de Mesa pour les GPU Adreno), chargé directement sans passer par le
// chargeur d'Android : on prend son point d'entrée d'ICD (vk_icdGetInstanceProcAddr), comme les
// applications qui installent leurs propres pilotes.
//
// Base du futur rendu Direct3D 11 par Vulkan (DXVK) ; sert déjà à l'outil d'inventaire tools/vk_probe.c.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_vulkan.h"

#if defined(RCN_HAS_VULKAN)

#include <dlfcn.h>
static void *open_library(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *library_symbol(void *lib, const char *name) { return dlsym(lib, name); }
static const char *library_error(void) { return dlerror(); }
#define SYSTEM_VULKAN "libvulkan.so"

typedef VkResult(VKAPI_PTR *PFN_negotiate)(uint32_t *version);

int rcn_vk_open(RcnVulkan *vk, const char *driver_path, char *error, size_t error_size) {
    memset(vk, 0, sizeof *vk);
    const char *path = driver_path && *driver_path ? driver_path : SYSTEM_VULKAN;
    vk->library = open_library(path);
    if (!vk->library) {
        snprintf(error, error_size, "pilote Vulkan introuvable (%s) : %s", path, library_error());
        return 0;
    }
    if (driver_path && *driver_path) {
        // Pilote d'ICD : interface du chargeur négociée (version 5 au plus), puis son point d'entrée.
        PFN_negotiate negotiate = (PFN_negotiate)library_symbol(vk->library, "vk_icdNegotiateLoaderICDInterfaceVersion");
        if (negotiate) {
            uint32_t version = 5;
            negotiate(&version);
            vk->icd_interface = version;
        }
        vk->get_instance_proc = (PFN_vkGetInstanceProcAddr)library_symbol(vk->library, "vk_icdGetInstanceProcAddr");
        if (!vk->get_instance_proc) vk->get_instance_proc = (PFN_vkGetInstanceProcAddr)library_symbol(vk->library, "vkGetInstanceProcAddr");
        vk->custom_driver = 1;
    } else {
        vk->get_instance_proc = (PFN_vkGetInstanceProcAddr)library_symbol(vk->library, "vkGetInstanceProcAddr");
    }
    if (!vk->get_instance_proc) {
        snprintf(error, error_size, "%s n'est pas un pilote Vulkan (point d'entrée absent)", path);
        return 0;
    }
    snprintf(vk->path, sizeof vk->path, "%s", path);
    return 1;
}

int rcn_vk_open_configured(RcnVulkan *vk, char *error, size_t error_size) {
    return rcn_vk_open(vk, getenv("RCN_VULKAN_DRIVER"), error, error_size);
}

int rcn_vk_describe(const char *driver_path, char *out, size_t out_size) {
    RcnVulkan vk;
    if (!rcn_vk_open(&vk, driver_path, out, out_size)) return 0;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)rcn_vk_global(&vk, "vkCreateInstance");
    PFN_vkEnumerateInstanceVersion enum_version = (PFN_vkEnumerateInstanceVersion)rcn_vk_global(&vk, "vkEnumerateInstanceVersion");
    uint32_t api = VK_API_VERSION_1_0;
    if (enum_version) enum_version(&api);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "RomCloud Native", 1, "rcn", 1, api};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL};
    VkInstance instance;
    VkResult r = create ? create(&ici, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
    if (r != VK_SUCCESS) {
        snprintf(out, out_size, "vkCreateInstance : erreur %d", (int)r);
        return 0;
    }
    PFN_vkEnumeratePhysicalDevices enum_devices = (PFN_vkEnumeratePhysicalDevices)rcn_vk_instance(&vk, instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties2 props2 = (PFN_vkGetPhysicalDeviceProperties2)rcn_vk_instance(&vk, instance, "vkGetPhysicalDeviceProperties2");
    PFN_vkGetPhysicalDeviceFeatures2 features2 = (PFN_vkGetPhysicalDeviceFeatures2)rcn_vk_instance(&vk, instance, "vkGetPhysicalDeviceFeatures2");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)rcn_vk_instance(&vk, instance, "vkDestroyInstance");
    uint32_t count = 1;
    VkPhysicalDevice device = VK_NULL_HANDLE;
    if (!enum_devices || enum_devices(instance, &count, &device) < 0 || !count || !props2 || !features2) {
        snprintf(out, out_size, "aucun GPU Vulkan");
        if (destroy) destroy(instance, NULL);
        return 0;
    }
    VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
    props2(device, &props);
    VkPhysicalDeviceRobustness2FeaturesEXT robust = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &robust};
    features2(device, &features);
    uint32_t v = props.properties.apiVersion;
    int dxvk = VK_API_VERSION_MAJOR(v) > 1 || VK_API_VERSION_MINOR(v) >= 3;
    dxvk = dxvk && robust.robustBufferAccess2 && robust.nullDescriptor && features.features.geometryShader;
    snprintf(out, out_size, "%s\n%s %s\nVulkan %u.%u.%u — textures BC : %s — robustness2 : %s\nDirect3D 11 par Vulkan (DXVK) : %s",
             props.properties.deviceName, driver.driverName, driver.driverInfo, VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v),
             VK_API_VERSION_PATCH(v), features.features.textureCompressionBC ? "oui" : "non",
             robust.nullDescriptor ? "oui" : "non", dxvk ? "possible" : "non (pilote trop ancien ou incomplet)");
    destroy(instance, NULL);
    return 1;
}

PFN_vkVoidFunction rcn_vk_global(RcnVulkan *vk, const char *name) { return vk->get_instance_proc(VK_NULL_HANDLE, name); }
PFN_vkVoidFunction rcn_vk_instance(RcnVulkan *vk, VkInstance instance, const char *name) { return vk->get_instance_proc(instance, name); }

#endif
