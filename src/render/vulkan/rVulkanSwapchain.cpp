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

#include "rVulkanSwapchain.h"
#include "rScreen.h"
#include "rVulkanContext.h"
#include <algorithm>
#include <iostream>
#ifdef __ANDROID__
#include <SDL3/SDL_log.h>
#endif

rVulkanSwapchain::rVulkanSwapchain() = default;

rVulkanSwapchain::~rVulkanSwapchain() = default;

bool rVulkanSwapchain::Create(rVulkanContext& ctx, uint32_t width, uint32_t height)
{
    VkPhysicalDevice physDev = ctx.GetPhysicalDevice();
    VkDevice device = ctx.GetDevice();
    VkSurfaceKHR surface = ctx.GetSurface();

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &caps);

    // Query formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &formatCount, formats.data());

    // Query present modes
    uint32_t modeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &modeCount, modes.data());

    VkSurfaceFormatKHR surfaceFormat = ChooseFormat(formats);
    VkPresentModeKHR presentMode = ChoosePresentMode(modes);
    VkExtent2D extent = ChooseExtent(caps, width, height);

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
    // On Android/iOS landscape, caps.currentExtent may be in portrait coordinate space
    // (pre-rotation). Ensure the swapchain extent is landscape (width > height).
    bool needsSwap = (caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR  ||
                      caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
    if (needsSwap && extent.width < extent.height)
    {
        std::swap(extent.width, extent.height);
    }
    // Fallback: if extent is still portrait after transform check, force landscape.
    if (extent.width < extent.height)
        std::swap(extent.width, extent.height);
#endif

    // Request one more image than minimum for triple buffering
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    uint32_t families[] = {ctx.GetGraphicsFamily(), ctx.GetPresentFamily()};
    if (families[0] != families[1])
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

#ifdef __ANDROID__
    // On Android we don't pre-rotate rendered content to match the display rotation.
    // Use IDENTITY so the compositor applies the rotation instead.
    // VK_SUBOPTIMAL_KHR will be returned each present (preTransform != currentTransform)
    // but that is harmless — the recreation loop guard in the renderer handles this.
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
#else
    createInfo.preTransform = caps.currentTransform;
#endif
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    VkSwapchainKHR oldSwapchain = swapchain_;
    createInfo.oldSwapchain = oldSwapchain; // for recreation

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &newSwapchain);
    if (result != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create swapchain: " << result << std::endl;
        // oldSwapchain is still valid and untouched — caller can retry or use it
        return false;
    }
    // Destroy old swapchain now that the new one is live (Vulkan retires it)
    if (oldSwapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    swapchain_ = newSwapchain;

    format_ = surfaceFormat.format;
    extent_ = extent;

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, images_.data());

    // Create image views
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        result = vkCreateImageView(device, &viewInfo, nullptr, &imageViews_[i]);
        if (result != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create image view " << i << std::endl;
            return false;
        }
    }

    return true;
}

bool rVulkanSwapchain::Recreate(rVulkanContext& ctx, uint32_t width, uint32_t height)
{
    vkDeviceWaitIdle(ctx.GetDevice());

    // Destroy old image views (images are owned by swapchain)
    for (auto view : imageViews_)
        vkDestroyImageView(ctx.GetDevice(), view, nullptr);
    imageViews_.clear();
    images_.clear();

    // oldSwapchain is set inside Create via swapchain_ member
    return Create(ctx, width, height);
}

void rVulkanSwapchain::Destroy(VkDevice device)
{
    for (auto view : imageViews_)
        vkDestroyImageView(device, view, nullptr);
    imageViews_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

VkSurfaceFormatKHR rVulkanSwapchain::ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    // Prefer BGRA8 UNORM — game uses legacy non-linear colors, sRGB would double-apply gamma
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    // Fallback to BGRA8 SRGB
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return formats[0];
}

VkPresentModeKHR rVulkanSwapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes)
{
    auto has = [&](VkPresentModeKHR m) {
        for (auto mode : modes) if (mode == m) return true;
        return false;
    };

    // presentMode: 0=Auto, 1=VSync, 2=Immediate, 3=Mailbox
    extern rScreenSettings currentScreensetting;
    switch (currentScreensetting.presentMode)
    {
    case 1: // VSync — locked to refresh rate
        return VK_PRESENT_MODE_FIFO_KHR;
    case 2: // Immediate — no vsync, lowest latency, may tear
        if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
        return VK_PRESENT_MODE_FIFO_KHR;
    case 3: // Mailbox — triple-buffered, no tearing
        if (has(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
        return VK_PRESENT_MODE_FIFO_KHR;
    default: // Auto — prefer Mailbox (triple-buffered, no tearing) > FIFO (VSync)
        if (has(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
        return VK_PRESENT_MODE_FIFO_KHR;
    }
}

VkExtent2D rVulkanSwapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    VkExtent2D extent = {w, h};
    extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, extent.width));
    extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, extent.height));
    return extent;
}

#endif // DEDICATED
