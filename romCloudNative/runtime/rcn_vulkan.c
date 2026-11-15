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

#include <android/dlext.h>
#include <dlfcn.h>
#include <stdint.h>

// Un pilote fourni (Turnip) dépend de bibliothèques du système (libcutils.so…) que l'espace de noms isolé
// d'une application ne voit pas. Comme les émulateurs (liblinkernsbypass), on crée avec les fonctions de
// l'éditeur de liens du système un espace de noms qui hérite de l'espace « default » (celui de libdl,
// désigné par l'adresse appelante) et l'on y charge le pilote.
typedef struct android_namespace_t android_namespace_t;
typedef android_namespace_t *(*PFN_create_namespace)(const char *name, const char *ld_library_path, const char *default_library_path,
                                                     uint64_t type, const char *permitted_when_isolated_path,
                                                     android_namespace_t *parent, const void *caller_addr);
typedef void *(*PFN_dlopen_ext)(const char *filename, int flags, const android_dlextinfo *extinfo, const void *caller_addr);

// Adresse d'une fonction de l'éditeur de liens du système (linker64), introuvable par dlsym depuis
// l'espace de noms d'une application : sa table de symboles est lue dans le fichier, l'adresse de base
// prise dans /proc/self/maps.
#include <elf.h>
static void *linker_symbol(const char *wanted) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return NULL;
    char line[1024], path[512] = {0};
    uintptr_t base = 0;
    while (fgets(line, sizeof line, maps)) {
        unsigned long start, offset;
        char file[512] = {0};
        if (sscanf(line, "%lx-%*lx %*s %lx %*s %*s %511s", &start, &offset, file) == 3 && offset == 0 &&
            strlen(file) > 9 && strcmp(file + strlen(file) - 9, "/linker64") == 0) {
            base = start;
            snprintf(path, sizeof path, "%s", file);
            break;
        }
    }
    fclose(maps);
    if (!base) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    void *result = NULL;
    Elf64_Ehdr eh;
    if (fread(&eh, sizeof eh, 1, f) == 1 && eh.e_shentsize == sizeof(Elf64_Shdr) && eh.e_shnum < 256) {
        Elf64_Shdr sh[256];
        fseek(f, (long)eh.e_shoff, SEEK_SET);
        if (fread(sh, sizeof(Elf64_Shdr), eh.e_shnum, f) == eh.e_shnum) {
            // Premier segment chargé (adresse virtuelle de référence, 0 en général).
            Elf64_Phdr ph;
            uintptr_t first_vaddr = 0;
            for (int i = 0; i < eh.e_phnum; i++) {
                fseek(f, (long)(eh.e_phoff + (Elf64_Off)i * sizeof ph), SEEK_SET);
                if (fread(&ph, sizeof ph, 1, f) == 1 && ph.p_type == PT_LOAD) {
                    first_vaddr = ph.p_vaddr & ~(uintptr_t)0xFFF;
                    break;
                }
            }
            for (int s = 0; s < eh.e_shnum && !result; s++) {
                if (sh[s].sh_type != SHT_SYMTAB && sh[s].sh_type != SHT_DYNSYM) continue;
                Elf64_Shdr *strtab = &sh[sh[s].sh_link];
                char *names = malloc(strtab->sh_size);
                Elf64_Sym *syms = malloc(sh[s].sh_size);
                if (names && syms) {
                    fseek(f, (long)strtab->sh_offset, SEEK_SET);
                    size_t got_names = fread(names, 1, strtab->sh_size, f);
                    fseek(f, (long)sh[s].sh_offset, SEEK_SET);
                    size_t got_syms = fread(syms, 1, sh[s].sh_size, f);
                    if (got_names == strtab->sh_size && got_syms == sh[s].sh_size) {
                        for (size_t k = 0; k < sh[s].sh_size / sizeof(Elf64_Sym); k++) {
                            if (syms[k].st_name < strtab->sh_size && syms[k].st_value && strcmp(names + syms[k].st_name, wanted) == 0) {
                                result = (void *)(base + syms[k].st_value - first_vaddr);
                                break;
                            }
                        }
                    }
                }
                free(names);
                free(syms);
            }
        }
    }
    fclose(f);
    return result;
}

static void *open_in_system_namespace(const char *path) {
    PFN_create_namespace create = (PFN_create_namespace)linker_symbol("__loader_android_create_namespace");
    PFN_dlopen_ext open_ext = (PFN_dlopen_ext)linker_symbol("__loader_android_dlopen_ext");
    if (!create || !open_ext) return NULL;
    char dir[512];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    // Adresse dans libdl (espace de noms « default ») : l'espace créé en hérite (type partagé : 2).
    const void *caller = (const void *)dlopen;
    android_namespace_t *ns = create("rcn-vulkan-driver", dir, dir, 2 /* ANDROID_NAMESPACE_TYPE_SHARED */, NULL, NULL, caller);
    if (!ns) return NULL;
    android_dlextinfo info = {0};
    info.flags = ANDROID_DLEXT_USE_NAMESPACE;
    info.library_namespace = ns;
    return open_ext(path, RTLD_NOW | RTLD_LOCAL, &info, caller);
}

static void *open_library(const char *path) {
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib && strchr(path, '/')) lib = open_in_system_namespace(path);
    return lib;
}
static void *library_symbol(void *lib, const char *name) { return dlsym(lib, name); }
static const char *library_error(void) { return dlerror(); }
#define SYSTEM_VULKAN "libvulkan.so"

typedef VkResult(VKAPI_PTR *PFN_negotiate)(uint32_t *version);

// Pilote au format HAL d'Android (Turnip pour Android n'exporte que « HMI ») : module hw_module_t dont
// open("vk0") rend un hwvulkan_device_t, qui donne vkGetInstanceProcAddr (hardware/hwvulkan.h).
struct hw_module_t;
struct hw_device_t;
typedef struct {
    int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
} hw_module_methods_t;
struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[32 - 7];
};
struct hw_device_t {
    uint32_t tag, version;
    struct hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *device);
};
typedef struct {
    struct hw_device_t common;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
} hwvulkan_device_t;

static PFN_vkGetInstanceProcAddr hal_entry(void *library) {
    struct hw_module_t *module = (struct hw_module_t *)library_symbol(library, "HMI");
    if (!module || !module->methods || !module->methods->open) return NULL;
    struct hw_device_t *device = NULL;
    if (module->methods->open(module, "vk0", &device) != 0 || !device) return NULL;
    return ((hwvulkan_device_t *)device)->GetInstanceProcAddr;
}

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
        if (!vk->get_instance_proc) {
            vk->get_instance_proc = hal_entry(vk->library);
            if (vk->get_instance_proc) vk->hal = 1;
        }
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
    snprintf(out, out_size, "%s%s\n%s %s\nVulkan %u.%u.%u — textures BC : %s — robustness2 : %s\nDirect3D 11 par Vulkan (DXVK) : %s",
             vk.hal ? "[HAL] " : "", props.properties.deviceName, driver.driverName, driver.driverInfo, VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v),
             VK_API_VERSION_PATCH(v), features.features.textureCompressionBC ? "oui" : "non",
             robust.nullDescriptor ? "oui" : "non", dxvk ? "possible" : "non (pilote trop ancien ou incomplet)");
    destroy(instance, NULL);
    return 1;
}

PFN_vkVoidFunction rcn_vk_global(RcnVulkan *vk, const char *name) { return vk->get_instance_proc(VK_NULL_HANDLE, name); }
PFN_vkVoidFunction rcn_vk_instance(RcnVulkan *vk, VkInstance instance, const char *name) { return vk->get_instance_proc(instance, name); }

#endif
