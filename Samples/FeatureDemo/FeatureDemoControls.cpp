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
#include "FeatureDemo.h"

Gui::DropdownList kSampleCountList = 
{
    { 1, "1" },
    { 2, "2" },
    { 4, "4" },
    { 8, "8" },
};

const Gui::DropdownList aaModeList = 
{
    { 0, "MSAA" },
    { 1, "TAA" }
};


void FeatureDemo::initControls()
{
    mControls.resize(ControlID::Count);
    mControls[ControlID::SuperSampling] = { false, false, "INTERPOLATION_MODE", "sample" };
    mControls[ControlID::EnableSpecAA] = { true, true, "_MS_DISABLE_ROUGHNESS_FILTERING" };
    mControls[ControlID::EnableShadows] = { true, false, "_ENABLE_SHADOWS" };
    mControls[ControlID::EnableReflections] = { true, false, "_ENABLE_REFLECTIONS" };
    mControls[ControlID::EnableHashedAlpha] = { true, true, "_DEFAULT_ALPHA_TEST" };
    mControls[ControlID::EnableSSAO] = { false, false, "" };

    for (uint32_t i = 0 ; i < ControlID::Count ; i++)
    {
        applyLightingProgramControl((ControlID)i);
    }
}

void FeatureDemo::applyLightingProgramControl(ControlID controlId)
{
    const ProgramControl control = mControls[controlId];
    if(control.define.size())
    {
        bool add = control.unsetOnEnabled ? !control.enabled : control.enabled;
        if (add)
        {
            mLightingPass.pProgram->addDefine(control.define, control.value);
        }
        else
        {
            mLightingPass.pProgram->removeDefine(control.define);
        }
    }
}

void FeatureDemo::applyAaMode()
{
    if (mLightingPass.pProgram == nullptr) return;

    uint32_t w = mpDefaultFBO->getWidth();
    uint32_t h = mpDefaultFBO->getHeight();

    // Common FBO desc (2 color outputs - color and normal)
    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA32Float).setColorTarget(1, ResourceFormat::RGBA8Unorm).setDepthStencilTarget(ResourceFormat::D32Float);

    // Release the TAA FBOs
    mTAA.resetFbos();

    if (mAAMode == AAMode::MSAA)
    {
        mLightingPass.pProgram->removeDefine("_OUTPUT_MOTION_VECTORS");
        applyLightingProgramControl(SuperSampling);
        fboDesc.setSampleCount(mMSAASampleCount);
    }
    else if (mAAMode == AAMode::TAA)
    {
        mLightingPass.pProgram->removeDefine("INTERPOLATION_MODE");
        mLightingPass.pProgram->addDefine("_OUTPUT_MOTION_VECTORS");
        fboDesc.setColorTarget(2, ResourceFormat::RG16Float);

        Fbo::Desc taaFboDesc;
        taaFboDesc.setColorTarget(0, ResourceFormat::RGBA32Float);
        mTAA.createFbos(w, h, taaFboDesc);
    }

    mpMainFbo = FboHelper::create2D(w, h, fboDesc);
}

void FeatureDemo::onGuiRender()
{
    static const char* kImageFileString = "Image files\0*.jpg;*.bmp;*.dds;*.png;*.tiff;*.tif;*.tga\0\0";
    if (mpGui->addButton("Load Model"))
    {
        std::string filename;
        if (openFileDialog(Model::kSupportedFileFormatsStr, filename))
        {
            loadModel(filename, true);
        }
    }

    if (mpGui->addButton("Load Scene"))
    {
        std::string filename;
        if (openFileDialog(Scene::kFileFormatString, filename))
        {
            loadScene(filename, true);
        }
    }

    if (mpSceneRenderer)
    {
        uint32_t maxAniso = mpSceneSampler->getMaxAnisotropy();
        if (mpGui->addIntVar("Max Anisotropy", (int&)maxAniso, 1, 16))
        {
            setSceneSampler(maxAniso);
        }

        if (mpGui->addButton("Load SkyBox Texture"))
        {
            std::string filename;
            if (openFileDialog(kImageFileString, filename))
            {
                initSkyBox(filename);
            }
        }

        //  Anti-Aliasing Controls.
        if (mpGui->beginGroup("Anti-Aliasing"))
        {
            bool reapply = false;
            reapply = reapply || mpGui->addDropdown("AA Mode", aaModeList, (uint32_t&)mAAMode);

            if (mAAMode == AAMode::MSAA)
            {
                reapply = reapply || mpGui->addDropdown("Sample Count", kSampleCountList, mMSAASampleCount);

                if (mpGui->addCheckBox("Super Sampling", mControls[ControlID::SuperSampling].enabled))
                {
                    applyLightingProgramControl(ControlID::SuperSampling);
                }
            }

            //  Temporal Anti-Aliasing.
            if (mAAMode == AAMode::TAA)
            {
                if (mpGui->beginGroup("TAA"))
                {
                    //  Render the TAA UI.
                    mTAA.pTAA->renderUI(mpGui.get());

                    //  Choose the Sample Pattern for TAA.
                    Gui::DropdownList samplePatternList;
                    samplePatternList.push_back({ (uint32_t)SamplePattern::Halton, "Halton" });
                    samplePatternList.push_back({ (uint32_t)SamplePattern::DX11, "DX11" });
                    mpGui->addDropdown("Sample Pattern", samplePatternList, (uint32_t&)mTAASamplePattern);

                    // Disable super-sampling
                    mpGui->endGroup();
                }
            }

            if (reapply) applyAaMode();

            if (mpGui->addCheckBox("Specular AA", mControls[ControlID::EnableSpecAA].enabled))
            {
                applyLightingProgramControl(ControlID::EnableSpecAA);
            }
            mpGui->endGroup();
        }

        if (mpGui->beginGroup("Reflections"))
        {
            if (mpGui->addCheckBox("Enable", mControls[ControlID::EnableReflections].enabled))
            {
                applyLightingProgramControl(ControlID::EnableReflections);
            }

            if(mControls[ControlID::EnableReflections].enabled)
            {
                if (mpGui->addButton("Load Reflection Texture"))
                {
                    std::string filename;
                    if (openFileDialog(kImageFileString, filename))
                    {
                        initEnvMap(filename);
                    }
                }
            }
            mpGui->endGroup();
        }

        if(mpGui->beginGroup("Scene Controls"))
        {
            const Scene* pScene = mpSceneRenderer->getScene();

            vec2 depthRange(pScene->getActiveCamera()->getNearPlane(), pScene->getActiveCamera()->getFarPlane());
            if (mpGui->addFloat2Var("Depth Range", depthRange, 0, FLT_MAX))
            {
                pScene->getActiveCamera()->setDepthRange(depthRange.x, depthRange.y);
            }

            if (pScene->getPathCount() > 0)
            {
                if (mpGui->addCheckBox("Camera Path", mUseCameraPath))
                {
                    applyCameraPathState();
                }
            }

            if (pScene->getLightCount() && mpGui->beginGroup("Light Sources"))
            {
                for (uint32_t i = 0; i < pScene->getLightCount(); i++)
                {
                    Light* pLight = pScene->getLight(i).get();
                    pLight->renderUI(mpGui.get(), pLight->getName().c_str());
                }
                mpGui->endGroup();
            }
        }

        mpToneMapper->renderUI(mpGui.get(), "Tone-Mapping");

        if (mpGui->beginGroup("Shadows"))
        {
            if (mpGui->addCheckBox("Enable Shadows", mControls[ControlID::EnableShadows].enabled))
            {
                applyLightingProgramControl(ControlID::EnableShadows);
            }
            if (mControls[ControlID::EnableShadows].enabled)
            {
                mpGui->addCheckBox("Update Map", mShadowPass.updateShadowMap);
                mShadowPass.pCsm->renderUi(mpGui.get());
            }
            mpGui->endGroup();
        }

        if (mpGui->beginGroup("SSAO"))
        {
            mpGui->addCheckBox("Enable SSAO", mControls[ControlID::EnableSSAO].enabled);
            if (mControls[ControlID::EnableSSAO].enabled)
            {
                mSSAO.pSSAO->renderGui(mpGui.get());
            }
            mpGui->endGroup();
        }

        if (mpGui->addCheckBox("Hashed-Alpha Test", mControls[ControlID::EnableHashedAlpha].enabled))
        {
            applyLightingProgramControl(ControlID::EnableHashedAlpha);
        }
    }
}
