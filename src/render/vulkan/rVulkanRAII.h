/*
 * rVulkanRAII.h — RAII wrappers for Vulkan resource lifecycle.
 *
 * Provides move-only handle wrappers and deferred destruction queues
 * to eliminate manual VK_DESTROY calls and prevent resource leaks.
 */

#ifndef RVULKANRAII_H
#define RVULKANRAII_H

#ifndef DEDICATED

#include <vulkan/vulkan.h>
#include <vector>
#include <utility>

// ============================================================================
// VkHandle — RAII wrapper for a single Vulkan handle.
//
// Automatically destroys the handle on scope exit. Move-only semantics
// ensure exactly one owner. Use release() to detach ownership (e.g., for
// transferring to a deferred destruction queue).
//
// Usage:
//   VkHandle<vkDestroyBuffer> buf(device, vkBuffer);
//   // ... use buf.get() ...
//   // destructor calls vkDestroyBuffer(device, handle, nullptr)
// ============================================================================

template<typename HandleType, void(*DestroyFn)(VkDevice, HandleType, const VkAllocationCallbacks*)>
class VkHandle
{
public:
    VkHandle() = default;
    VkHandle(VkDevice dev, HandleType h) : device_(dev), handle_(h) {}

    ~VkHandle() { reset(); }

    // Move semantics
    VkHandle(VkHandle&& o) noexcept
        : device_(o.device_), handle_(o.handle_) { o.handle_ = VK_NULL_HANDLE; }

    VkHandle& operator=(VkHandle&& o) noexcept
    {
        if (this != &o) { reset(); device_ = o.device_; handle_ = o.handle_; o.handle_ = VK_NULL_HANDLE; }
        return *this;
    }

    void reset()
    {
        if (handle_ != VK_NULL_HANDLE)
        {
            DestroyFn(device_, handle_, nullptr);
            handle_ = VK_NULL_HANDLE;
        }
    }

    HandleType get() const { return handle_; }
    HandleType release() { auto h = handle_; handle_ = VK_NULL_HANDLE; return h; }
    operator HandleType() const { return handle_; }
    explicit operator bool() const { return handle_ != VK_NULL_HANDLE; }

    // Non-copyable
    VkHandle(const VkHandle&) = delete;
    VkHandle& operator=(const VkHandle&) = delete;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    HandleType handle_ = VK_NULL_HANDLE;
};

// Type aliases for common Vulkan handles
using VkBufferRAII      = VkHandle<VkBuffer, vkDestroyBuffer>;
using VkImageRAII       = VkHandle<VkImage, vkDestroyImage>;
using VkImageViewRAII   = VkHandle<VkImageView, vkDestroyImageView>;
using VkSamplerRAII     = VkHandle<VkSampler, vkDestroySampler>;
using VkFenceRAII       = VkHandle<VkFence, vkDestroyFence>;
using VkSemaphoreRAII   = VkHandle<VkSemaphore, vkDestroySemaphore>;
using VkShaderModuleRAII = VkHandle<VkShaderModule, vkDestroyShaderModule>;
using VkFramebufferRAII = VkHandle<VkFramebuffer, vkDestroyFramebuffer>;
using VkRenderPassRAII  = VkHandle<VkRenderPass, vkDestroyRenderPass>;
using VkPipelineLayoutRAII = VkHandle<VkPipelineLayout, vkDestroyPipelineLayout>;
using VkDescriptorPoolRAII = VkHandle<VkDescriptorPool, vkDestroyDescriptorPool>;
using VkDescriptorSetLayoutRAII = VkHandle<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>;

// ============================================================================
// DeferredQueue — Per-frame-slot deferred destruction queue.
//
// Vulkan resources referenced by in-flight command buffers must not be
// destroyed until the GPU finishes. This queue holds handles for
// MAX_FRAMES_IN_FLIGHT frames before draining them.
//
// Usage:
//   DeferredQueue<VkSampler> samplerQueue;
//   samplerQueue.queue(oldSampler, currentFrame);  // defer
//   // ... later, after fence wait for this slot:
//   samplerQueue.drain(device, currentFrame, vkDestroySampler);
// ============================================================================

// Must match MAX_FRAMES_IN_FLIGHT in rVulkanRender.h.
// Using a separate constant here to avoid a header dependency cycle.
static constexpr uint32_t DEFERRED_QUEUE_SLOTS = 2;
static_assert(DEFERRED_QUEUE_SLOTS == 2, "Update if MAX_FRAMES_IN_FLIGHT changes");

template<typename T>
class DeferredQueue
{
public:
    void queue(T handle, uint32_t slot)
    {
        slots_[slot % DEFERRED_QUEUE_SLOTS].push_back(std::move(handle));
    }

    template<auto DestroyFn>
    void drain(VkDevice device, uint32_t slot)
    {
        auto& vec = slots_[slot % DEFERRED_QUEUE_SLOTS];
        for (auto& h : vec)
            DestroyFn(device, h, nullptr);
        vec.clear();
    }

    // Drain with a custom destructor lambda (for compound types like VkTextureInfo)
    template<typename Fn>
    void drainWith(VkDevice device, uint32_t slot, Fn&& fn)
    {
        auto& vec = slots_[slot % DEFERRED_QUEUE_SLOTS];
        for (auto& h : vec)
            fn(device, h);
        vec.clear();
    }

    [[nodiscard]] bool empty(uint32_t slot) const { return slots_[slot % DEFERRED_QUEUE_SLOTS].empty(); }
    [[nodiscard]] size_t size(uint32_t slot) const { return slots_[slot % DEFERRED_QUEUE_SLOTS].size(); }

    // Drain ALL slots (for shutdown)
    template<auto DestroyFn>
    void drainAll(VkDevice device)
    {
        for (uint32_t i = 0; i < DEFERRED_QUEUE_SLOTS; ++i)
            drain<DestroyFn>(device, i);
    }

    template<typename Fn>
    void drainAllWith(VkDevice device, Fn&& fn)
    {
        for (uint32_t i = 0; i < DEFERRED_QUEUE_SLOTS; ++i)
            drainWith(device, i, fn);
    }

private:
    std::vector<T> slots_[DEFERRED_QUEUE_SLOTS];
};

#endif // DEDICATED
#endif // RVULKANRAII_H
