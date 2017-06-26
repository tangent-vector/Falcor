/***************************************************************************
# Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/

#include "Framework.h"
#include "API/Texture.h"
#include "API/Device.h"
#include "API/Resource.h"

namespace Falcor
{
    struct TextureApiData
    {
    };

    void Texture::apiInit()
    {
    }

    Texture::~Texture()
    {
        if(gpDevice) // #VKTODO This shouldn't be here
        {
            gpDevice->releaseResource(mApiHandle);
        }
    }

    // Like getD3D12ResourceFlags but for Images specifically
    VkImageUsageFlags getVkImageUsageFlags(Resource::BindFlags bindFlags)
    {
        VkImageUsageFlags vkFlags = 0;

        if (is_set(bindFlags, Resource::BindFlags::UnorderedAccess))
        {
            vkFlags |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        if (is_set(bindFlags, Resource::BindFlags::DepthStencil))
        {

            vkFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        if (is_set(bindFlags, Resource::BindFlags::ShaderResource) == false)
        {
            // #VKTODO what does VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT mean?
            vkFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        if (is_set(bindFlags, Resource::BindFlags::RenderTarget))
        {
            vkFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        // According to spec, must not be 0
        assert(vkFlags != 0);

        return vkFlags;
    }

    uint32_t getMaxMipCount(const VkExtent3D& size)
    {
        return 1 + uint32_t(glm::log2(float(glm::max(glm::max(size.width, size.height), size.depth))));
    }

    uint32_t Texture::getMipLevelDataSize(uint32_t mipLevel) const
    {
        return 0;
    }

    void Texture::evict(const Sampler* pSampler) const
    {
    }

    VkImageType getVkImageType(Texture::Type type)
    {
        switch (type)
        {
        case Texture::Type::Texture1D:
            return VK_IMAGE_TYPE_1D;

        case Texture::Type::Texture2D:
        case Texture::Type::Texture2DMultisample:
        case Texture::Type::TextureCube:
            return VK_IMAGE_TYPE_2D;

        case Texture::Type::Texture3D:
            return VK_IMAGE_TYPE_3D;
        default:
            should_not_get_here();
            return VK_IMAGE_TYPE_1D;
        }
    }

    void Texture::initResource(const void* pData, bool autoGenMips)
    {
        VkImageCreateInfo imageInfo = {};

        imageInfo.arrayLayers = mArraySize;
        imageInfo.extent.depth = mDepth;
        imageInfo.extent.height = align_to(getFormatHeightCompressionRatio(mFormat), mHeight);
        imageInfo.extent.width = align_to(getFormatWidthCompressionRatio(mFormat), mWidth);
        imageInfo.flags = (mType == Texture::Type::TextureCube) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        imageInfo.format = getVkFormat(mFormat);
        imageInfo.imageType = getVkImageType(mType);
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.mipLevels = min(mMipLevels, getMaxMipCount(imageInfo.extent));
        imageInfo.pQueueFamilyIndices = nullptr;
        imageInfo.queueFamilyIndexCount = 0;
        imageInfo.samples = (VkSampleCountFlagBits)mSampleCount;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = getVkImageUsageFlags(mBindFlags);

        mState = Resource::State::Undefined; // Default is common, which is not what we want

        VkImage image;
        if (VK_FAILED(vkCreateImage(gpDevice->getApiHandle(), &imageInfo, nullptr, &image)))
        {
            logError("Failed to create texture.");
            return;
        }
        mApiHandle = image;

        // Allocate the GPU memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(gpDevice->getApiHandle(), image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = gpDevice->getVkMemoryType(Device::MemoryType::Default);
        
        VkDeviceMemory deviceMem;
        if (vkAllocateMemory(gpDevice->getApiHandle(), &allocInfo, nullptr, &deviceMem) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(gpDevice->getApiHandle(), image, deviceMem, 0);

        mApiHandle.setDeviceMem(deviceMem);
        if (pData != nullptr)
        {

        }
    }
}
