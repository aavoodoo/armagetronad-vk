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

#ifndef RVULKANFRAMEBUFFER_H
#define RVULKANFRAMEBUFFER_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>

class rVulkanContext;
class rVulkanSwapchain;

//! Manages the main render pass, depth buffer, and per-swapchain-image framebuffers.
class rVulkanFramebuffer
{
public:
    rVulkanFramebuffer() = default;
    ~rVulkanFramebuffer() = default;

    //! Create render pass, depth buffer, and framebuffers for the swapchain
    bool Create(rVulkanContext& ctx, rVulkanSwapchain& swapchain);

    //! Recreate after swapchain resize
    bool Recreate(rVulkanContext& ctx, rVulkanSwapchain& swapchain);

    //! Destroy all resources
    void Destroy(rVulkanContext& ctx);

    [[nodiscard]] VkRenderPass  GetRenderPass()              const { return renderPass_; }
    [[nodiscard]] VkFramebuffer GetFramebuffer(uint32_t idx) const { return framebuffers_[idx]; }
    [[nodiscard]] VkFormat      GetDepthFormat()             const { return depthFormat_; }

    // Non-copyable
    rVulkanFramebuffer(const rVulkanFramebuffer&) = delete;
    rVulkanFramebuffer& operator=(const rVulkanFramebuffer&) = delete;

private:
    bool CreateRenderPass(VkDevice device, VkFormat colorFormat);
    bool CreateDepthResources(rVulkanContext& ctx, uint32_t width, uint32_t height);
    bool CreateFramebuffers(VkDevice device, rVulkanSwapchain& swapchain);
    void DestroyDepthResources(rVulkanContext& ctx);
    void DestroyFramebuffers(VkDevice device);

    VkFormat FindDepthFormat(VkPhysicalDevice physDev);

    VkRenderPass               renderPass_       = VK_NULL_HANDLE;
    VkImage                    depthImage_       = VK_NULL_HANDLE;
    VmaAllocation              depthAllocation_  = VK_NULL_HANDLE;
    VkImageView                depthView_        = VK_NULL_HANDLE;
    VkFormat                   depthFormat_      = VK_FORMAT_UNDEFINED;
    std::vector<VkFramebuffer> framebuffers_;
};

#endif // DEDICATED
#endif // RVULKANFRAMEBUFFER_H
