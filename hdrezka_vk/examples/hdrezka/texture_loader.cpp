#include "texture_loader.hpp"
#include "VulkanTools.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "tinygltf/stb_image.h"

#include <cstring>
#include <algorithm>

namespace rezka
{

TextureResource TextureLoader::create_texture_from_rgba(vks::VulkanDevice *device,
                                                        VkQueue queue,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        const uint8_t *rgba_pixels)
{
    TextureResource res;
    if (!device || width == 0 || height == 0 || !rgba_pixels) return res;

    res.width = width;
    res.height = height;

    VkDeviceSize image_size = width * height * 4;

    // 1. Create staging buffer
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;

    VkBufferCreateInfo buf_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = image_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device->logicalDevice, &buf_info, nullptr, &staging_buffer) != VK_SUCCESS) {
        return res;
    }

    VkMemoryRequirements buf_mem_reqs;
    vkGetBufferMemoryRequirements(device->logicalDevice, staging_buffer, &buf_mem_reqs);

    VkMemoryAllocateInfo buf_alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buf_mem_reqs.size,
        .memoryTypeIndex = device->getMemoryType(buf_mem_reqs.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    if (vkAllocateMemory(device->logicalDevice, &buf_alloc_info, nullptr, &staging_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
        return res;
    }

    vkBindBufferMemory(device->logicalDevice, staging_buffer, staging_memory, 0);

    // Copy pixels to staging buffer
    void *mapped = nullptr;
    vkMapMemory(device->logicalDevice, staging_memory, 0, image_size, 0, &mapped);
    if (mapped) {
        std::memcpy(mapped, rgba_pixels, image_size);
        vkUnmapMemory(device->logicalDevice, staging_memory);
    }

    // 2. Create device image
    VkImageCreateInfo img_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device->logicalDevice, &img_info, nullptr, &res.image) != VK_SUCCESS) {
        vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
        vkFreeMemory(device->logicalDevice, staging_memory, nullptr);
        return res;
    }

    VkMemoryRequirements img_mem_reqs;
    vkGetImageMemoryRequirements(device->logicalDevice, res.image, &img_mem_reqs);

    VkMemoryAllocateInfo img_alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = img_mem_reqs.size,
        .memoryTypeIndex = device->getMemoryType(img_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (vkAllocateMemory(device->logicalDevice, &img_alloc_info, nullptr, &res.memory) != VK_SUCCESS) {
        vkDestroyImage(device->logicalDevice, res.image, nullptr);
        res.image = VK_NULL_HANDLE;
        vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
        vkFreeMemory(device->logicalDevice, staging_memory, nullptr);
        return res;
    }

    vkBindImageMemory(device->logicalDevice, res.image, res.memory, 0);

    // 3. Record copy command
    VkCommandBuffer cmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    VkImageSubresourceRange subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vks::tools::setImageLayout(cmd, res.image, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);

    VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging_buffer, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vks::tools::setImageLayout(cmd, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresource_range);

    device->flushCommandBuffer(cmd, queue);

    // Clean up staging buffer
    vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
    vkFreeMemory(device->logicalDevice, staging_memory, nullptr);

    // 4. Create image view
    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = res.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = subresource_range,
    };
    if (vkCreateImageView(device->logicalDevice, &view_info, nullptr, &res.view) != VK_SUCCESS) {
        destroy_texture(device, res);
    }

    return res;
}

TextureResource TextureLoader::load_image_from_memory(vks::VulkanDevice *device,
                                                      VkQueue queue,
                                                      const uint8_t *data,
                                                      size_t size)
{
    TextureResource res;
    if (!data || size == 0) return res;

    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) return res;

    res = create_texture_from_rgba(device, queue, static_cast<uint32_t>(width), static_cast<uint32_t>(height), pixels);
    stbi_image_free(pixels);
    return res;
}

uint8_t *TextureLoader::decode_image_rgba(const uint8_t *data, size_t size, int *w, int *h)
{
    if (!data || size == 0 || !w || !h) return nullptr;
    int channels = 0;
    return stbi_load_from_memory(data, static_cast<int>(size), w, h, &channels, 4);
}

uint8_t *TextureLoader::load_file_rgba(const char *path, int *w, int *h)
{
    if (!path || !w || !h) return nullptr;
    int channels = 0;
    return stbi_load(path, w, h, &channels, 4);
}

void TextureLoader::free_decoded_image(uint8_t *pixels)
{
    if (pixels) {
        stbi_image_free(pixels);
    }
}

void TextureLoader::destroy_texture(vks::VulkanDevice *device, TextureResource &tex)
{
    if (!device) return;
    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device->logicalDevice, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(device->logicalDevice, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device->logicalDevice, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
}

bool VideoTexture::init(vks::VulkanDevice *dev, VkQueue q, uint32_t w, uint32_t h)
{
    release();
    if (!dev || w == 0 || h == 0) return false;
    device = dev;
    queue = q;
    width = w;
    height = h;

    VkDeviceSize image_size = width * height * 4;

    VkBufferCreateInfo buf_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = image_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device->logicalDevice, &buf_info, nullptr, &staging_buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements buf_mem_reqs;
    vkGetBufferMemoryRequirements(device->logicalDevice, staging_buffer, &buf_mem_reqs);

    VkMemoryAllocateInfo buf_alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buf_mem_reqs.size,
        .memoryTypeIndex = device->getMemoryType(buf_mem_reqs.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    if (vkAllocateMemory(device->logicalDevice, &buf_alloc_info, nullptr, &staging_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
        staging_buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device->logicalDevice, staging_buffer, staging_memory, 0);
    vkMapMemory(device->logicalDevice, staging_memory, 0, image_size, 0, &mapped_staging);

    VkImageCreateInfo img_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device->logicalDevice, &img_info, nullptr, &image) != VK_SUCCESS) {
        release();
        return false;
    }

    VkMemoryRequirements img_mem_reqs;
    vkGetImageMemoryRequirements(device->logicalDevice, image, &img_mem_reqs);

    VkMemoryAllocateInfo img_alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = img_mem_reqs.size,
        .memoryTypeIndex = device->getMemoryType(img_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (vkAllocateMemory(device->logicalDevice, &img_alloc_info, nullptr, &memory) != VK_SUCCESS) {
        release();
        return false;
    }

    vkBindImageMemory(device->logicalDevice, image, memory, 0);

    VkCommandBuffer cmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    VkImageSubresourceRange subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vks::tools::setImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresource_range);
    device->flushCommandBuffer(cmd, queue);

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = subresource_range,
    };
    if (vkCreateImageView(device->logicalDevice, &view_info, nullptr, &view) != VK_SUCCESS) {
        release();
        return false;
    }

    return true;
}

bool VideoTexture::update_frame(const uint8_t *rgba_pixels, size_t nbytes)
{
    if (!device || !mapped_staging || !rgba_pixels || image == VK_NULL_HANDLE) {
        return false;
    }

    const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;
    if (nbytes < image_size || image_size == 0)
        return false;
    std::memcpy(mapped_staging, rgba_pixels, static_cast<size_t>(image_size));

    VkCommandBuffer cmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    VkImageSubresourceRange subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vks::tools::setImageLayout(cmd, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);

    VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vks::tools::setImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresource_range);

    device->flushCommandBuffer(cmd, queue);
    return true;
}

void VideoTexture::release()
{
    if (!device) return;
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device->logicalDevice, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device->logicalDevice, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device->logicalDevice, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
    if (mapped_staging && staging_memory != VK_NULL_HANDLE) {
        vkUnmapMemory(device->logicalDevice, staging_memory);
        mapped_staging = nullptr;
    }
    if (staging_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
        staging_buffer = VK_NULL_HANDLE;
    }
    if (staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device->logicalDevice, staging_memory, nullptr);
        staging_memory = VK_NULL_HANDLE;
    }
    width = 0;
    height = 0;
    hui_handle = 0;
}

} // namespace rezka
