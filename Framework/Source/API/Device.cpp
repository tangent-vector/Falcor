/***************************************************************************
# Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
#pragma once
#include "Framework.h"
#include "API/Device.h"

namespace Falcor
{
    Device::SharedPtr gpDevice;
    
    Device::SharedPtr Device::create(Window::SharedPtr& pWindow, const Device::Desc& desc)
    {
        if (gpDevice)
        {
            logError("Falcor only supports a single device");
            return false;
        }
        gpDevice = SharedPtr(new Device(pWindow));
        if (gpDevice->init(desc) == false)
        {
            gpDevice = nullptr;
        }
        return gpDevice;
    }

    bool Device::init(const Desc& desc)
    {
        if (apiInit(desc) == false) return false;

        const uint32_t kDirectQueueIndex = (uint32_t)LowLevelContextData::CommandQueueType::Direct;
        if(mCmdQueues[kDirectQueueIndex].size())
        {
            mpRenderContext = RenderContext::create(mCmdQueues[kDirectQueueIndex][0]);
        }

        // Create the descriptor pools
        DescriptorPool::Desc poolDesc;
        poolDesc.setDescCount(DescriptorPool::Type::Srv, 16 * 1024).setDescCount(DescriptorPool::Type::Sampler, 2048).setShaderVisible(true);
        mpGpuDescPool = DescriptorPool::create(poolDesc, mpRenderContext->getLowLevelData()->getFence());
        poolDesc.setShaderVisible(false).setDescCount(DescriptorPool::Type::Rtv, 1024).setDescCount(DescriptorPool::Type::Dsv, 1024);
        mpCpuDescPool = DescriptorPool::create(poolDesc, mpRenderContext->getLowLevelData()->getFence());

        if(mpRenderContext) mpRenderContext->reset();

        mVsyncOn = desc.enableVsync;

        // Create the swap-chain
        mpResourceAllocator = ResourceAllocator::create(1024 * 1024 * 2, mpRenderContext->getLowLevelData()->getFence());
        if (createSwapChain(desc.colorFormat) == false)
        {
            return false;
        }

        // Update the FBOs
        if (updateDefaultFBO(mpWindow->getClientAreaWidth(), mpWindow->getClientAreaHeight(), desc.colorFormat, desc.depthFormat) == false)
        {
            return false;
        }

        mpFrameFence = GpuFence::create();
        return true;
    }

    void Device::releaseFboData()
    {
        // First, delete all FBOs
        for (uint32_t i = 0; i < arraysize(mFrameData); i++)
        {
            mFrameData[i].pFbo->attachColorTarget(nullptr, 0);
            mFrameData[i].pFbo->attachDepthStencilTarget(nullptr);
        }

        // Now execute all deferred releases
        decltype(mDeferredReleases)().swap(mDeferredReleases);
    }

    bool Device::updateDefaultFBO(uint32_t width, uint32_t height, ResourceFormat colorFormat, ResourceFormat depthFormat)
    {
        std::vector<ResourceHandle> apiHandles;
        getApiFboData(width, height, colorFormat, depthFormat, apiHandles, mCurrentBackBufferIndex);

        for (uint32_t i = 0; i < kSwapChainBuffers; i++)
        {
            // Create a texture object
            auto pColorTex = Texture::SharedPtr(new Texture(width, height, 1, 1, 1, 1, colorFormat, Texture::Type::Texture2D, Texture::BindFlags::RenderTarget));
            pColorTex->mApiHandle = apiHandles[i];

            // Create the FBO if it's required
            if (mFrameData[i].pFbo == nullptr)
            {
                mFrameData[i].pFbo = Fbo::create();
            }

            mFrameData[i].pFbo->attachColorTarget(pColorTex, 0);

            // Create a depth texture
            if (depthFormat != ResourceFormat::Unknown)
            {
                auto pDepth = Texture::create2D(width, height, depthFormat, 1, 1, nullptr, Texture::BindFlags::DepthStencil);
                mFrameData[i].pFbo->attachDepthStencilTarget(pDepth);
            }
        }
        return true;
    }

    Fbo::SharedPtr Device::getSwapChainFbo() const
    {
        return mFrameData[mCurrentBackBufferIndex].pFbo;
    }

    void Device::releaseResource(ApiObjectHandle pResource)
    {
        if (pResource)
        {
            mDeferredReleases.push({ mpFrameFence->getCpuValue(), pResource });
        }
    }

    void Device::executeDeferredReleases()
    {
        mpResourceAllocator->executeDeferredReleases();
        uint64_t gpuVal = mpFrameFence->getGpuValue();
        while (mDeferredReleases.size() && mDeferredReleases.front().frameID < gpuVal)
        {
            mDeferredReleases.front().pApiObject.Release();
            mDeferredReleases.pop();
        }
    }

    void Device::toggleVSync(bool enable)
    {
        mVsyncOn = enable;
    }

    void Device::cleanup()
    {
        mpRenderContext->flush(true);
        // Release all the bound resources. Need to do that before deleting the RenderContext
        mpRenderContext->setGraphicsState(nullptr);
        mpRenderContext->setGraphicsVars(nullptr);
        mpRenderContext->setComputeState(nullptr);
        mpRenderContext->setComputeVars(nullptr);
        //releaseFboData(pData);
        mpRenderContext.reset();
        mpResourceAllocator.reset();

        destroyApiObjects();
        mpWindow.reset();
    }

    void Device::present()
    {
        mpRenderContext->resourceBarrier(mFrameData[mCurrentBackBufferIndex].pFbo->getColorTexture(0).get(), Resource::State::Present);
        mpRenderContext->flush();
        apiPresent();
        executeDeferredReleases();
        mpRenderContext->reset();
        mFrameID++;
    }
}