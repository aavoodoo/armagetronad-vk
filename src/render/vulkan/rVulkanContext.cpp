/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#include "aa_config.h"

#ifndef DEDICATED

#include "rVulkanContext.h"

#include <SDL3/SDL_vulkan.h>
#include <cstring>
#include <iostream>
#include <set>
#include "tConfiguration.h"

// 0=Auto (prefer discrete), 1=Force discrete, 2=Force integrated
static int sr_vulkanDeviceSelection = 0;
static tConfItem<int> sr_vulkanDeviceSelectionCI("VULKAN_DEVICE_SELECTION", sr_vulkanDeviceSelection);

// ============================================================================
// Validation layer callback
// ============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << "[Vulkan] " << data->pMessage << std::endl;
    }
    return VK_FALSE;
}

static const char* const VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};

static const char* const DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ============================================================================
// Global accessor
// ============================================================================

static rVulkanContext s_vulkanContext;

rVulkanContext& rGetVulkanContext()
{
    return s_vulkanContext;
}

// ============================================================================
// Implementation
// ============================================================================

rVulkanContext::rVulkanContext() = default;

rVulkanContext::~rVulkanContext()
{
    Shutdown();
}

bool rVulkanContext::Init(SDL_Window* window, bool enableValidation)
{
    validationEnabled_ = enableValidation;

    // Ensure SDL Vulkan library is loaded
    if (!SDL_Vulkan_LoadLibrary(nullptr))
    {
        std::cerr << "[Vulkan] Failed to load Vulkan library: " << SDL_GetError() << std::endl;
        return false;
    }

    if (!CreateInstance(enableValidation))
        return false;

    if (!CreateSurface(window))
        return false;

    if (!PickPhysicalDevice())
        return false;

    if (!CreateLogicalDevice())
        return false;

    return true;
}

void rVulkanContext::Shutdown()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (debugMessenger_ != VK_NULL_HANDLE)
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (func)
            func(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
}

bool rVulkanContext::CreateInstance(bool enableValidation)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Armagetron Advanced";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 4, 0);
    appInfo.pEngineName = "ArmageTron";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 4, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // Get SDL-required extensions
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);

    // Check if validation layer is actually available before requesting it
    if (enableValidation)
    {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool found = false;
        for (const auto& lp : availableLayers)
            if (strcmp(lp.layerName, VALIDATION_LAYERS[0]) == 0) { found = true; break; }

        if (!found)
        {
            std::cerr << "[Vulkan] Validation layer not available; disabling.\n";
            enableValidation = false;
        }
    }

    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidation)
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create instance: " << result << std::endl;
        return false;
    }

    // Set up debug messenger
    if (enableValidation)
    {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = vkDebugCallback;

        auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (func)
            func(instance_, &debugInfo, nullptr, &debugMessenger_);
    }

    return true;
}

bool rVulkanContext::CreateSurface(SDL_Window* window)
{
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_))
    {
        std::cerr << "[Vulkan] Failed to create surface" << std::endl;
        return false;
    }
    return true;
}

bool rVulkanContext::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        std::cerr << "[Vulkan] No Vulkan-capable GPUs found" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    // Pick the best device (prefer discrete GPU)
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int bestScore = -1;

    for (auto& dev : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        // Check queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

        bool hasGraphics = false, hasPresent = false;
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                hasGraphics = true;

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
            if (presentSupport)
                hasPresent = true;
        }

        if (!hasGraphics || !hasPresent)
            continue;

        // Check swapchain extension
        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());

        bool hasSwapchain = false;
        for (auto& ext : exts)
        {
            if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            {
                hasSwapchain = true;
                break;
            }
        }

        if (!hasSwapchain)
            continue;

        int score = 0;
        bool isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        bool isIntegrated = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);

        // VULKAN_DEVICE_SELECTION: 0=Auto, 1=Force discrete, 2=Force integrated
        if (sr_vulkanDeviceSelection == 1 && !isDiscrete) continue;
        if (sr_vulkanDeviceSelection == 2 && !isIntegrated) continue;

        if (isDiscrete)    score += 1000;
        if (isIntegrated)  score += 100;
        score += props.limits.maxImageDimension2D / 1000;

        if (score > bestScore)
        {
            bestScore = score;
            bestDevice = dev;
        }
    }

    if (bestDevice == VK_NULL_HANDLE)
    {
        std::cerr << "[Vulkan] No suitable GPU found" << std::endl;
        return false;
    }

    physicalDevice_ = bestDevice;
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties_);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);

#ifndef NDEBUG
    std::cerr << "[Vulkan] Selected GPU: " << deviceProperties_.deviceName << std::endl;
#endif
    return true;
}

bool rVulkanContext::CreateLogicalDevice()
{
    // Find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (graphicsFamily_ == UINT32_MAX && (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            graphicsFamily_ = i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &presentSupport);
        if (presentFamily_ == UINT32_MAX && presentSupport)
            presentFamily_ = i;
    }

    // Create queue infos
    std::set<uint32_t> uniqueFamilies = {graphicsFamily_, presentFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.0f;

    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supportedFeatures);

    VkPhysicalDeviceFeatures features{};
    features.fillModeNonSolid = supportedFeatures.fillModeNonSolid;
    features.wideLines = supportedFeatures.wideLines;
    features.depthBiasClamp = supportedFeatures.depthBiasClamp;
    depthBiasClampSupported_ = (supportedFeatures.depthBiasClamp == VK_TRUE);

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS;
    createInfo.pEnabledFeatures = &features;

    VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create device: " << result << std::endl;
        return false;
    }

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);

    return true;
}

uint32_t rVulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    for (uint32_t i = 0; i < memoryProperties_.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memoryProperties_.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

#endif // DEDICATED
