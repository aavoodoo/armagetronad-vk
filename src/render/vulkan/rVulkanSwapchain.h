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

#ifndef RVULKANSWAPCHAIN_H
#define RVULKANSWAPCHAIN_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include <vector>

class rVulkanContext;

//! Manages VkSwapchainKHR, image views, and presentation.
class rVulkanSwapchain
{
public:
    rVulkanSwapchain();
    ~rVulkanSwapchain();

    //! Create swapchain for the given context and dimensions
    bool Create(rVulkanContext& ctx, uint32_t width, uint32_t height);

    //! Recreate after window resize
    bool Recreate(rVulkanContext& ctx, uint32_t width, uint32_t height);

    //! Destroy all swapchain resources
    void Destroy(VkDevice device);

    // Accessors
    VkSwapchainKHR   GetSwapchain()   const { return swapchain_; }
    VkFormat         GetFormat()      const { return format_; }
    VkExtent2D       GetExtent()      const { return extent_; }
    uint32_t         GetImageCount()  const { return static_cast<uint32_t>(imageViews_.size()); }
    VkImageView      GetImageView(uint32_t i) const { return imageViews_[i]; }

    const std::vector<VkImageView>& GetImageViews() const { return imageViews_; }
    const std::vector<VkImage>&     GetImages()     const { return images_; }

    // Non-copyable
    rVulkanSwapchain(const rVulkanSwapchain&) = delete;
    rVulkanSwapchain& operator=(const rVulkanSwapchain&) = delete;

private:
    VkSurfaceFormatKHR ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR   ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D         ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h);

    VkSwapchainKHR           swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage>     images_;
    std::vector<VkImageView> imageViews_;
    VkFormat                 format_     = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent_     = {0, 0};
};

#endif // DEDICATED
#endif // RVULKANSWAPCHAIN_H
