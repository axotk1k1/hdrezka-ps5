#pragma once

#include "vulkan/vulkan.h"
#include "VulkanDevice.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace rezka
{

struct TextureResource
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};

class TextureLoader
{
public:
    static TextureResource create_texture_from_rgba(vks::VulkanDevice *device,
                                                    VkQueue queue,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    const uint8_t *rgba_pixels);

    static TextureResource load_image_from_memory(vks::VulkanDevice *device,
                                                  VkQueue queue,
                                                  const uint8_t *data,
                                                  size_t size);

    static uint8_t *decode_image_rgba(const uint8_t *data, size_t size, int *w, int *h);
    static uint8_t *load_file_rgba(const char *path, int *w, int *h);
    static void free_decoded_image(uint8_t *pixels);
    // Raw deflate (a zip entry, method 8) into out's size; false when it does not fill it.
    static bool inflate_raw(const uint8_t *data, size_t size, std::vector<uint8_t> &out);

    static void destroy_texture(vks::VulkanDevice *device, TextureResource &tex);
};

struct VideoTexture
{
    vks::VulkanDevice *device = nullptr;
    VkQueue queue = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void *mapped_staging = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t hui_handle = 0;

    bool init(vks::VulkanDevice *dev, VkQueue q, uint32_t w, uint32_t h);
    bool update_frame(const uint8_t *rgba_pixels, size_t nbytes);
    void release();
};

} // namespace rezka
