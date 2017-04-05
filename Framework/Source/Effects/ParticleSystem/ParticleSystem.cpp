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
#include "ParticleSystem.h"
#include "API/Resource.h"
#include "glm/gtc/random.hpp"
#include <algorithm>
#include "Data/Effects/ParticleData.h"

namespace Falcor
{
    void ParticleSystem::init(RenderContext* pCtx, uint32_t maxParticles)
    {
        mMaxParticles = maxParticles;

        //Shaders
        mEmitCs = ComputeProgram::createFromFile("Effects/ParticleEmit.cs.hlsl");
        mSimulateCs = ComputeProgram::createFromFile("Effects/ParticleSimulate.cs.hlsl");
        mDrawParticles = GraphicsProgram::createFromFile("Effects/ParticleVertex.vs.hlsl", "Effects/ParticleSimple.ps.hlsl");

        //Buffers
        //IndexList
        std::vector<uint32_t> indices;
        indices.resize(maxParticles);
        uint32_t counter = 0;
        std::generate(indices.begin(), indices.end(), [&counter] {return counter++; });
        ProgramReflection::SharedConstPtr pEmitReflect = mEmitCs->getActiveVersion()->getReflector();
        ProgramReflection::BufferReflection::SharedConstPtr indexListReflect = 
            pEmitReflect->getBufferDesc("IndexList", ProgramReflection::BufferReflection::Type::Structured);
        mpIndexList = StructuredBuffer::create(indexListReflect, maxParticles);
        mpIndexList->setBlob(indices.data(), 0, indices.size() * sizeof(uint32_t));
        //ParticlePool
        ProgramReflection::BufferReflection::SharedConstPtr particlePoolReflect =
            pEmitReflect->getBufferDesc("ParticlePool", ProgramReflection::BufferReflection::Type::Structured);
        mpParticlePool = StructuredBuffer::create(particlePoolReflect, maxParticles);
        //Simulate Dispatch indirect Args
        uint32_t indirectArgsSize = sizeof(D3D12_DISPATCH_ARGUMENTS) + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        mpIndirectArgs = StructuredBuffer::create(pEmitReflect->
            getBufferDesc("dispatchArgs", ProgramReflection::BufferReflection::Type::Structured), 
            indirectArgsSize / sizeof(uint32_t), Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
        uint32_t initialValues[8] = { 1, 1, 1, 1, 1, 0, 0, 0 };
        mpIndirectArgs->setBlob(initialValues, 0, indirectArgsSize);

        //Vars
        mEmitVars = ComputeVars::create(pEmitReflect);
        mEmitVars->setStructuredBuffer("IndexList", mpIndexList);
        mEmitVars->setStructuredBuffer("ParticlePool", mpParticlePool);
        mEmitVars->setStructuredBuffer("dispatchArgs", mpIndirectArgs);
        mEmitVars->setRawBuffer("numAlive", mpIndexList->getUAVCounter());
        mSimulateVars = ComputeVars::create(mSimulateCs->getActiveVersion()->getReflector());
        mSimulateVars->setStructuredBuffer("IndexList", mpIndexList);
        mSimulateVars->setStructuredBuffer("ParticlePool", mpParticlePool);
        mSimulateVars->setStructuredBuffer("drawArgs", mpIndirectArgs);
        mSimulateVars->setRawBuffer("numAlive", mpIndexList->getUAVCounter());
        mpDrawVars = GraphicsVars::create(mDrawParticles->getActiveVersion()->getReflector());
        mpDrawVars->setStructuredBuffer("IndexList", mpIndexList);
        mpDrawVars->setStructuredBuffer("ParticlePool", mpParticlePool);

        //State
        mEmitState = ComputeState::create();
        mEmitState->setProgram(mEmitCs);
        mSimulateState = ComputeState::create();
        mSimulateState->setProgram(mSimulateCs);
        mpDrawState = GraphicsState::create();
        mpDrawState->setProgram(mDrawParticles);

        //Create vertex buffer
        //Buffer::SharedPtr pBuffer = Buffer::create(1, Resource::BindFlags::Vertex, Buffer::CpuAccess::None, nullptr);
        Vao::BufferVec bufferVec;
        //bufferVec.push_back(pBuffer);
        VertexLayout::SharedPtr pLayout = VertexLayout::create();
        //Craete and check vao
        Vao::Topology topology = Vao::Topology::TriangleList;
        pVao = Vao::create(bufferVec, pLayout, nullptr, ResourceFormat::R32Uint, topology);
    }

    void ParticleSystem::emit(RenderContext* pCtx, uint32_t num)
    {
        //Fill emit data
        EmitData emitData;
        for (uint32_t i = 0; i < num; ++i)
        {
            Particle p;  
            p.pos = Emitter.spawnPos + glm::linearRand(-Emitter.randSpawnPos, Emitter.randSpawnPos);
            p.vel = Emitter.vel + glm::linearRand(-Emitter.randVel, Emitter.randVel);
            p.accel = Emitter.accel + glm::linearRand(-Emitter.randAccel, Emitter.randAccel);
            p.scale = 0.1f;
            p.life = Emitter.duration + glm::linearRand(-Emitter.randDuration, Emitter.randDuration);
            //TODO
            //Not needed in final i think, can just use which side of counter barrier. for debugging
            p.alive = 1;
            emitData.particles[i] = p;
        }
        emitData.numEmit = num;
        emitData.maxParticles = mMaxParticles;

        //Send vars and call
        pCtx->pushComputeState(mEmitState);
        mEmitVars->getConstantBuffer(0)->setBlob(&emitData, 0u, sizeof(EmitData));
        pCtx->pushComputeVars(mEmitVars);
        u32 numGroups = num % 64 == 0 ? num / 64 : (num / 64) + 1;
        pCtx->dispatch(1, numGroups, 1);
        pCtx->popComputeVars();
        pCtx->popComputeState();
    }

    void ParticleSystem::update(RenderContext* pCtx, float dt)
    {
        SimulatePerFrame perFrame;
        perFrame.dt = dt;
        perFrame.maxParticles = mMaxParticles;

        pCtx->pushComputeState(mSimulateState);
        mSimulateVars->getConstantBuffer(0)->setBlob(&perFrame, 0u, sizeof(SimulatePerFrame));
        pCtx->pushComputeVars(mSimulateVars);
        pCtx->DispatchIndirect(mpIndirectArgs.get(), 0, 1);
        pCtx->popComputeVars();
        pCtx->popComputeState();
    }

    void ParticleSystem::render(RenderContext* pCtx, glm::mat4 view, glm::mat4 proj)
    {
        VSPerFrame cbuf;
        cbuf.view = view;
        cbuf.proj = proj;
        mpDrawVars->getConstantBuffer(0)->setBlob(&cbuf, 0, sizeof(cbuf));

        pCtx->getGraphicsState()->setProgram(mDrawParticles);
        pCtx->getGraphicsState()->setVao(pVao);

        //This gives can't write to multiple buf same swapchain
        //pCtx->pushGraphicsState(mpDrawState);
        pCtx->pushGraphicsVars(mpDrawVars);
        pCtx->DrawIndirect(mpIndirectArgs.get(), sizeof(D3D12_DISPATCH_ARGUMENTS), 1);
        pCtx->popGraphicsVars();
        //pCtx->popGraphicsState();
    }

}