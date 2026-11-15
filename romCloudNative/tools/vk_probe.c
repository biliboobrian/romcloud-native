// Inventaire Vulkan de l'appareil, pour juger s'il peut servir de base à DXVK (Direct3D 11 par Vulkan) :
// pilote, version de Vulkan, extensions et fonctions utiles à DXVK. Pilote du système, ou pilote donné
// en argument (ex. Turnip : libvulkan_freedreno.so).
//
//   vk_probe [chemin/du/pilote.so]
//
// Compilation (NDK) : avec runtime/rcn_vulkan.c, -I runtime.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rcn_vulkan.h"

#define GET(vk, inst, name) PFN_##name name = (PFN_##name)rcn_vk_instance(vk, inst, #name)

static const char *const wanted[] = {
    // Indispensables à DXVK 2.x (ou presque)
    "VK_EXT_robustness2", "VK_KHR_maintenance5", "VK_EXT_extended_dynamic_state3", "VK_KHR_pipeline_library",
    "VK_EXT_graphics_pipeline_library", "VK_EXT_transform_feedback", "VK_EXT_vertex_attribute_divisor",
    "VK_KHR_vertex_attribute_divisor", "VK_EXT_custom_border_color", "VK_EXT_depth_clip_enable",
    "VK_EXT_memory_priority", "VK_EXT_non_seamless_cube_map", "VK_EXT_shader_module_identifier",
    "VK_KHR_swapchain", "VK_EXT_attachment_feedback_loop_layout", "VK_EXT_line_rasterization",
    "VK_EXT_4444_formats", "VK_EXT_conservative_rasterization", "VK_EXT_fragment_shader_interlock",
    "VK_KHR_present_id", "VK_KHR_present_wait", "VK_EXT_swapchain_maintenance1",
};

int main(int argc, char **argv) {
    RcnVulkan vk;
    char error[512];
    if (!rcn_vk_open(&vk, argc > 1 ? argv[1] : NULL, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    printf("pilote : %s%s (interface d'ICD %u)\n", vk.path, vk.custom_driver ? " (fourni)" : "", vk.icd_interface);
    GET(&vk, VK_NULL_HANDLE, vkCreateInstance);
    PFN_vkEnumerateInstanceVersion enum_version = (PFN_vkEnumerateInstanceVersion)rcn_vk_global(&vk, "vkEnumerateInstanceVersion");
    uint32_t api = VK_API_VERSION_1_0;
    if (enum_version) enum_version(&api);
    printf("Vulkan de l'instance : %u.%u.%u\n", VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api));
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vk_probe", 1, "rcn", 1, api};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL};
    VkInstance instance;
    VkResult r = vkCreateInstance(&ici, NULL, &instance);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance : %d\n", r);
        return 1;
    }
    GET(&vk, instance, vkEnumeratePhysicalDevices);
    GET(&vk, instance, vkGetPhysicalDeviceProperties2);
    GET(&vk, instance, vkGetPhysicalDeviceFeatures2);
    GET(&vk, instance, vkEnumerateDeviceExtensionProperties);
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, NULL);
    VkPhysicalDevice devices[4];
    if (count > 4) count = 4;
    vkEnumeratePhysicalDevices(instance, &count, devices);
    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
        vkGetPhysicalDeviceProperties2(devices[d], &props);
        uint32_t v = props.properties.apiVersion;
        printf("\nGPU %u : %s — Vulkan %u.%u.%u — pilote %s (%s)\n", d, props.properties.deviceName, VK_API_VERSION_MAJOR(v),
               VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v), driver.driverName, driver.driverInfo);
        VkPhysicalDeviceRobustness2FeaturesEXT robust = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
        VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, &robust};
        VkPhysicalDeviceVulkan13Features v13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &xfb};
        VkPhysicalDeviceVulkan12Features v12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &v13};
        VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v12};
        vkGetPhysicalDeviceFeatures2(devices[d], &features);
        VkPhysicalDeviceFeatures *f = &features.features;
        printf("  geometryShader %u  tessellation %u  textureCompressionBC %u  dualSrcBlend %u  logicOp %u  depthClamp %u\n",
               f->geometryShader, f->tessellationShader, f->textureCompressionBC, f->dualSrcBlend, f->logicOp, f->depthClamp);
        printf("  shaderClipDistance %u  shaderCullDistance %u  multiViewport %u  independentBlend %u  imageCubeArray %u\n",
               f->shaderClipDistance, f->shaderCullDistance, f->multiViewport, f->independentBlend, f->imageCubeArray);
        printf("  robustness2 : robustBufferAccess2 %u  robustImageAccess2 %u  nullDescriptor %u\n", robust.robustBufferAccess2,
               robust.robustImageAccess2, robust.nullDescriptor);
        printf("  transformFeedback %u  geometryStreams %u\n", xfb.transformFeedback, xfb.geometryStreams);
        printf("  1.2 : timelineSemaphore %u  descriptorIndexing %u  bufferDeviceAddress %u  shaderFloat16 %u\n", v12.timelineSemaphore,
               v12.descriptorIndexing, v12.bufferDeviceAddress, v12.shaderFloat16);
        printf("  1.3 : dynamicRendering %u  synchronization2 %u  maintenance4 %u\n", v13.dynamicRendering, v13.synchronization2,
               v13.maintenance4);
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &n, NULL);
        VkExtensionProperties *ext = calloc(n, sizeof *ext);
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &n, ext);
        printf("  %u extensions ; utiles à DXVK :\n", n);
        for (size_t w = 0; w < sizeof wanted / sizeof wanted[0]; w++) {
            int has = 0;
            for (uint32_t e = 0; e < n; e++) has |= strcmp(ext[e].extensionName, wanted[w]) == 0;
            printf("    %s %s\n", has ? "oui" : "NON", wanted[w]);
        }
        free(ext);
    }
    return 0;
}
