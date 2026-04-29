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

#include "rVulkanFramebuffer.h"
#include "rVulkanContext.h"
#include "rVulkanSwapchain.h"
#include <iostream>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <array>

bool rVulkanFramebuffer::Create(rVulkanContext& ctx, rVulkanSwapchain& swapchain)
{
    depthFormat_ = FindDepthFormat(ctx.GetPhysicalDevice());
    if (depthFormat_ == VK_FORMAT_UNDEFINED)
    {
        std::cerr << "[Vulkan] No suitable depth format found" << std::endl;
        return false;
    }

    if (!CreateRenderPass(ctx.GetDevice(), swapchain.GetFormat()))
        return false;

    if (!CreateDepthResources(ctx, swapchain.GetExtent().width, swapchain.GetExtent().height))
        return false;

    if (!CreateFramebuffers(ctx.GetDevice(), swapchain))
        return false;

    return true;
}

bool rVulkanFramebuffer::Recreate(rVulkanContext& ctx, rVulkanSwapchain& swapchain)
{
    DestroyFramebuffers(ctx.GetDevice());
    DestroyDepthResources(ctx);

    if (!CreateDepthResources(ctx, swapchain.GetExtent().width, swapchain.GetExtent().height))
        return false;

    if (!CreateFramebuffers(ctx.GetDevice(), swapchain))
        return false;

    return true;
}

void rVulkanFramebuffer::Destroy(rVulkanContext& ctx)
{
    DestroyFramebuffers(ctx.GetDevice());
    DestroyDepthResources(ctx);

    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.GetDevice(), renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

bool rVulkanFramebuffer::CreateRenderPass(VkDevice device, VkFormat colorFormat)
{
    // Color attachment (swapchain image)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#if defined(__APPLE__) && !(TARGET_OS_IOS)
    // On macOS MoltenVK, DONT_CARE allows Metal to use a lossy internal depth
    // representation (memoryless tile storage), causing z-fighting on Apple Silicon.
    // STORE forces full D32_SFLOAT precision to be preserved.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
#else
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
#endif
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#if defined(__APPLE__) && !(TARGET_OS_IOS)
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
#else
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
#endif

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Incoming dependency: wait for swapchain image before rendering
    VkSubpassDependency incomingDep{};
    incomingDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    incomingDep.dstSubpass = 0;
    incomingDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    incomingDep.srcAccessMask = 0;
    incomingDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    incomingDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                              | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

#if defined(__APPLE__) && !(TARGET_OS_IOS)
    // Outgoing dependency: on macOS, force depth writes to resolve via
    // LATE_FRAGMENT_TESTS_BIT, matching the post-process offscreen path.
    // Without this, MoltenVK/Metal may use lossy depth storage on Apple Silicon.
    VkSubpassDependency outgoingDep{};
    outgoingDep.srcSubpass = 0;
    outgoingDep.dstSubpass = VK_SUBPASS_EXTERNAL;
    outgoingDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    outgoingDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                              | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    outgoingDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    outgoingDep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkSubpassDependency, 2> dependencies = {incomingDep, outgoingDep};
#else
    std::array<VkSubpassDependency, 1> dependencies = {incomingDep};
#endif

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create render pass" << std::endl;
        return false;
    }

    return true;
}

bool rVulkanFramebuffer::CreateDepthResources(rVulkanContext& ctx, uint32_t width, uint32_t height)
{
    VkDevice device = ctx.GetDevice();

    // Create depth image via VMA
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat_;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
#if defined(__APPLE__) && !(TARGET_OS_IOS)
    // SAMPLED_BIT forces Metal to allocate real memory for depth instead of
    // using memoryless tile storage, ensuring full D32_SFLOAT precision.
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_SAMPLED_BIT;
#else
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
#endif
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo vmaAllocCI{};
    vmaAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(ctx.GetAllocator(), &imageInfo, &vmaAllocCI,
                       &depthImage_, &depthAllocation_, nullptr) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] VMA: failed to create depth image" << std::endl;
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthView_) != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] Failed to create depth image view" << std::endl;
        return false;
    }

    return true;
}

bool rVulkanFramebuffer::CreateFramebuffers(VkDevice device, rVulkanSwapchain& swapchain)
{
    const auto& imageViews = swapchain.GetImageViews();
    framebuffers_.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachments = {imageViews[i], depthView_};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass_;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = swapchain.GetExtent().width;
        fbInfo.height = swapchain.GetExtent().height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create framebuffer " << i << std::endl;
            return false;
        }
    }

    return true;
}

void rVulkanFramebuffer::DestroyDepthResources(rVulkanContext& ctx)
{
    if (depthView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(ctx.GetDevice(), depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE)
    {
        vmaDestroyImage(ctx.GetAllocator(), depthImage_, depthAllocation_);
        depthImage_      = VK_NULL_HANDLE;
        depthAllocation_ = VK_NULL_HANDLE;
    }
}

void rVulkanFramebuffer::DestroyFramebuffers(VkDevice device)
{
    for (auto fb : framebuffers_)
        vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers_.clear();
}

VkFormat rVulkanFramebuffer::FindDepthFormat(VkPhysicalDevice physDev)
{
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physDev, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            const char* name = (format == VK_FORMAT_D32_SFLOAT)         ? "D32_SFLOAT"
                             : (format == VK_FORMAT_D32_SFLOAT_S8_UINT) ? "D32_SFLOAT_S8_UINT"
                             : (format == VK_FORMAT_D24_UNORM_S8_UINT)  ? "D24_UNORM_S8_UINT"
                             : "unknown";
#ifndef NDEBUG
            std::cerr << "[Vulkan] Depth format selected: " << name << "\n";
#endif
            return format;
        }
    }

    std::cerr << "[Vulkan] Depth format: NONE FOUND\n";
    return VK_FORMAT_UNDEFINED;
}

#endif // DEDICATED
