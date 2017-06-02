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
#include "Framework.h"
#include "Sample.h"
#include <map>
#include <fstream>
#include "API/Window.h"
#include "Graphics/Program.h"
#include "Utils/OS.h"
#include "API/FBO.h"
#include "VR\OpenVR\VRSystem.h"
#include "Utils\ProgressBar.h"
#include <sstream>
#include <iomanip>

namespace Falcor
{
    Sample::Sample()
    {
    };

    void Sample::handleWindowSizeChange()
    {
        // Tell the device to resize the swap chain
        mpDefaultFBO = gpDevice->resizeSwapChain(mpWindow->getClientAreaWidth(), mpWindow->getClientAreaHeight());
        mpDefaultPipelineState->setFbo(mpDefaultFBO);

        // Tell the GUI the swap-chain size changed
        mpGui->onWindowResize(mpDefaultFBO->getWidth(), mpDefaultFBO->getHeight());

        // Call the user callback
        onResizeSwapChain();

        // Reset the clock, so that the first frame duration will be correct
        mFrameRate.resetClock();
    }

    void Sample::handleKeyboardEvent(const KeyboardEvent& keyEvent)
    {
        if (keyEvent.type == KeyboardEvent::Type::KeyPressed)
        {
            mPressedKeys.insert(keyEvent.key);
        }
        else
        {
            mPressedKeys.erase(keyEvent.key);
        }

        // Check if the GUI consumes it
        if(mpGui->onKeyboardEvent(keyEvent))
        {
            return;
        }
        
        //Checks if should toggle zoom
        mpPixelZoom->onKeyboardEvent(keyEvent);

        // Consume system messages first
        if(keyEvent.type == KeyboardEvent::Type::KeyPressed)
        {
            if(keyEvent.mods.isShiftDown && keyEvent.key == KeyboardEvent::Key::F12)
            {
                initVideoCapture();
            }
            else if(!keyEvent.mods.isAltDown && !keyEvent.mods.isCtrlDown && !keyEvent.mods.isShiftDown)
            {
                switch(keyEvent.key)
                {
                case KeyboardEvent::Key::F12:
                    mCaptureScreen = true;
                    break;
#if _PROFILING_ENABLED
                case KeyboardEvent::Key::P:
                    gProfileEnabled = !gProfileEnabled;
                    break;
#endif
                case KeyboardEvent::Key::V:
                    mVsyncOn = !mVsyncOn;
                    gpDevice->setVSync(mVsyncOn);
                    mFrameRate.resetClock();
                    break;
                case KeyboardEvent::Key::F1:
                    toggleText(!mShowText);
                    break;
                case KeyboardEvent::Key::F2:
                    toggleUI(!mShowUI);
                    break;
                case KeyboardEvent::Key::F5:
                    Program::reloadAllPrograms();
                    onDataReload();
                    break;
                case KeyboardEvent::Key::Escape:
                    if(mVideoCapture.pVideoCapture)
                    {
                        endVideoCapture();
                    }
                    else
                    {
                        mpWindow->shutdown();
                    }
                    break;
                case KeyboardEvent::Key::Equal:
                    mFreezeTime = !mFreezeTime;
                    break;
                }
            }
        }

        // If we got here, this is a user specific message
        onKeyEvent(keyEvent);
    }

    void Sample::handleMouseEvent(const MouseEvent& mouseEvent)
    {
        if(mpGui->onMouseEvent(mouseEvent))
        {
            return;
        }
        mpPixelZoom->onMouseEvent(mouseEvent);
        onMouseEvent(mouseEvent);
    }


    // CSample functions
    Sample::~Sample()
    {
        if(mVideoCapture.pVideoCapture)
        {
            endVideoCapture();
        }

        VRSystem::cleanup();

        mpGui.reset();
        mpDefaultPipelineState.reset();
        mpDefaultFBO.reset();
        mpTextRenderer.reset();
        mpPixelZoom.reset();
        mpRenderContext.reset();
        gpDevice->cleanup();
        gpDevice.reset();
    }

    void Sample::run(const SampleConfig& config)
    {
        mTimeScale = config.timeScale;
        mFreezeTime = config.freezeTimeOnStartup;

        // Start the logger
        Logger::init();
        Logger::showBoxOnError(config.showMessageBoxOnError);

        // Show the progress bar
        ProgressBar::MessageList msgList =
        {
            { "Initializing Falcor" },
            { "Takes a while, doesn't it?" },
            { "Don't get too bored now" },
            { "Getting there" },
            { "Loading. Seriously, loading" },
            { "Are we there yet?"},
            { "NI!"}
        };

        ProgressBar::SharedPtr pBar = ProgressBar::create(msgList);

        // Create the window
        mpWindow = Window::create(config.windowDesc, this);
        if (mpWindow == nullptr)
        {
            logError("Failed to create device and window");
            return;
        }

        gpDevice = Device::create(mpWindow, config.deviceDesc);
        if (gpDevice == nullptr)
        {
            logError("Failed to create device");
            return;
        }

        if (config.deviceCreatedCallback)
        {
            config.deviceCreatedCallback();
        }

        // Set the icon
        setWindowIcon("Framework\\Nvidia.ico", mpWindow->getApiHandle());

        // Get the default objects before calling onLoad()
        mpDefaultFBO = gpDevice->getSwapChainFbo();
        mpDefaultPipelineState = GraphicsState::create();
        mpDefaultPipelineState->setFbo(mpDefaultFBO);
        mpRenderContext = gpDevice->getRenderContext();
        mpRenderContext->setGraphicsState(mpDefaultPipelineState);

        // Init the UI
        initUI();

        // Init VR
        mVrEnabled = config.enableVR;
        if (mVrEnabled)
        {
            VRSystem::start(mpRenderContext);
        }

        // Load and run
        mArgList.parseCommandLine(GetCommandLineA());
        mpPixelZoom = PixelZoom::create();
        mpPixelZoom->init(mpDefaultFBO.get());
        onLoad();
        pBar = nullptr;
        mpWindow->msgLoop();

        onShutdown();
        Logger::shutdown();
    }

    void Sample::calculateTime()
    {
        if (mVideoCapture.pVideoCapture)
        {
            // We are capturing video at a constant FPS
            mCurrentTime += mVideoCapture.timeDelta * mTimeScale;
        }
        else if (mFreezeTime == false)
        {
            float elapsedTime = mFrameRate.getLastFrameTime() * mTimeScale;
            mCurrentTime += elapsedTime;
        }
    }

    void Sample::renderGUI()
    {
        constexpr char help[] =
            "  'F1'      - Show\\Hide text\n"
            "  'F2'      - Show\\Hide GUI\n"
            "  'F5'      - Reload shaders\n"
            "  'ESC'     - Quit\n"
            "  'V'       - Toggle VSync\n"
            "  'F12'     - Capture screenshot\n"
            "  'Shift+F12' - Video capture\n"
            "  '='       - Pause\\resume timer\n"
            "  'Z'       - Zoom in on a pixel\n"
            "  'MouseWheel' - Change level of zoom\n"
#if _PROFILING_ENABLED
            "  'P'       - Enable profiling\n";
#else
            ;
#endif

        mpGui->pushWindow("Falcor", 250, 200, 20, 40, false);
        mpGui->addText("Keyboard Shortcuts");
        mpGui->addTooltip(help, true);

        if(mpGui->beginGroup("Global Controls"))
        {
            mpGui->addFloatVar("Time", mCurrentTime, 0, FLT_MAX);
            mpGui->addFloatVar("Time Scale", mTimeScale, 0, FLT_MAX);

            if (mpGui->addButton("Reset"))
            {
                mCurrentTime = 0.0f;
            }

            if (mpGui->addButton(mFreezeTime ? "Play" : "Pause", true))
            {
                mFreezeTime = !mFreezeTime;
            }

            if (mpGui->addButton("Stop", true))
            {
                mFreezeTime = true;
                mCurrentTime = 0.0f;
            }

            mCaptureScreen = mpGui->addButton("Screen Capture");
            if (mpGui->addButton("Video Capture", true))
            {
                initVideoCapture();
            }
            mpGui->endGroup();
        }

        onGuiRender();
        mpGui->popWindow();

        if (mVideoCapture.pUI)
        {
            mVideoCapture.pUI->render(mpGui.get());
        }
        mpGui->render(mpRenderContext.get(), mFrameRate.getLastFrameTime());
    }

    void Sample::renderFrame()
    {
        if (gpDevice->isWindowOccluded())
        {
            return;
        }

        mFrameRate.newFrame();
        {
            PROFILE(onFrameRender);
            // The swap-chain FBO might have changed between frames, so get it
            mpDefaultFBO = gpDevice->getSwapChainFbo();
            mpRenderContext = gpDevice->getRenderContext();
            calculateTime();

            // Bind the default state
            mpRenderContext->setGraphicsState(mpDefaultPipelineState);
            mpDefaultPipelineState->setFbo(mpDefaultFBO);
            onFrameRender();
        }
        {
            PROFILE(renderGUI);
            if(mShowUI)
            {
                renderGUI();
            }
        }

        renderText(getFpsMsg(), glm::vec2(10, 10));
        mpPixelZoom->render(mpRenderContext.get(), gpDevice->getSwapChainFbo().get());

        captureVideoFrame();
        printProfileData();
        if (mCaptureScreen)
        {
            captureScreen();
        }
        {
            PROFILE(present);
            gpDevice->present();
        }
    }

    void Sample::captureScreen()
    {
        std::string filename = getExecutableName();

        // Now we have a folder and a filename, look for an available filename (we don't overwrite existing files)
        std::string prefix = std::string(filename);
        std::string executableDir = getExecutableDirectory();
        std::string pngFile;
        if(findAvailableFilename(prefix, executableDir, "png", pngFile))
        {
            Texture::SharedPtr pTexture = gpDevice->getSwapChainFbo()->getColorTexture(0);
            pTexture->captureToFile(0, 0, pngFile);
        }
        else
        {
            logError("Could not find available filename when capturing screen");
        }
        mCaptureScreen = false;
    }

    void Sample::initUI()
    {
        mpGui = Gui::create(mpDefaultFBO->getWidth(), mpDefaultFBO->getHeight());
        mpTextRenderer = TextRenderer::create();
    }

    const std::string Sample::getFpsMsg() const
    {
        std::string s;
        if(mShowText)
        {
            std::stringstream strstr;
            float msPerFrame = mFrameRate.getAverageFrameTime();
            std::string msStr = std::to_string(msPerFrame);
            s = std::to_string(int(ceil(1000 / msPerFrame))) + " FPS (" + msStr.erase(msStr.size() - 4) + " ms/frame)";
            if (mVsyncOn) s += std::string(", VSync");
        }
        return s;
    }

    void Sample::toggleText(bool enabled)
    {
        mShowText = enabled;
    }

    void Sample::resizeSwapChain(uint32_t width, uint32_t height)
    {
        mpWindow->resize(width, height);
        mpPixelZoom->init(gpDevice->getSwapChainFbo().get());
    }

    bool Sample::isKeyPressed(const KeyboardEvent::Key& key) const 
    {
        return mPressedKeys.find(key) != mPressedKeys.cend();
    }

    void Sample::renderText(const std::string& msg, const glm::vec2& position, const glm::vec2 shadowOffset) const
    {
        if(mShowText)
        {
            // Render outline first
            if(shadowOffset.x != 0.f || shadowOffset.y != 0)
            {
                const glm::vec3 oldColor = mpTextRenderer->getTextColor();
                mpTextRenderer->setTextColor(glm::vec3(0.f));   // Black outline 
                mpTextRenderer->begin(mpRenderContext, position + shadowOffset);
                mpTextRenderer->renderLine(msg);
                mpTextRenderer->end();
                mpTextRenderer->setTextColor(oldColor);
            }
            mpTextRenderer->begin(mpRenderContext, position);
            mpTextRenderer->renderLine(msg);
            mpTextRenderer->end();
        }
    }

    void Sample::printProfileData()
    {
#if _PROFILING_ENABLED
        if(gProfileEnabled)
        {
            std::string profileMsg;
            Profiler::endFrame(profileMsg);
            renderText(profileMsg, glm::vec2(10, 300));
        }
#endif
    }

    void Sample::initVideoCapture()
    {
        if(mVideoCapture.pUI == nullptr)
        {
            mVideoCapture.pUI = VideoEncoderUI::create(20, 300, 240, 220, [this]() {startVideoCapture(); }, [this]() {endVideoCapture(); });
        }
    }

	void Sample::captureMemory(MemoryCheck & memoryCheck)
	{
		memoryCheck.pTotalVirtualMemory = getTotalVirtualMemory();
		memoryCheck.pTotalUsedVirtualMemory = getUsedVirtualMemory();
		memoryCheck.pCurrentlyUsedVirtualMemory = getProcessUsedVirtualMemory();
	}

	void Sample::writeMemoryRange(const MemoryCheckRange & memoryCheckRange)
	{
		//	
		std::string startTVM_B = std::to_string(memoryCheckRange.startCheck.pTotalVirtualMemory);
		std::string startTUVM_B = std::to_string(memoryCheckRange.startCheck.pTotalUsedVirtualMemory);
		std::string startCUVM_B = std::to_string(memoryCheckRange.startCheck.pCurrentlyUsedVirtualMemory);

		std::string startTVM_MB = std::to_string(memoryCheckRange.startCheck.pTotalVirtualMemory / (1024 * 1024));
		std::string startTUVM_MB = std::to_string(memoryCheckRange.startCheck.pTotalUsedVirtualMemory / (1024 * 1024));
		std::string startCUVM_MB = std::to_string(memoryCheckRange.startCheck.pCurrentlyUsedVirtualMemory / (1024 * 1024));

		std::string startCheck = "At the Start Frame, " + std::to_string(memoryCheckRange.startCheck.pFrame) + ", : \n ";
		startCheck = startCheck + ("Total Virtual Memory : " + startTVM_B + " bytes, " + startTVM_MB + " MB. \n");
		startCheck = startCheck + ("Total Used Virtual Memory By All Processes : " + startTUVM_B + " bytes, " + startTUVM_MB + " MB. \n");
		startCheck = startCheck + ("Virtual Memory used by this Process : " + startCUVM_B + " bytes, " + startCUVM_MB + " MB. \n \n");
	
		//
		std::string endTVM_B = std::to_string(memoryCheckRange.endCheck.pTotalVirtualMemory);
		std::string endTUVM_B = std::to_string(memoryCheckRange.endCheck.pTotalUsedVirtualMemory);
		std::string endCUVM_B = std::to_string(memoryCheckRange.endCheck.pCurrentlyUsedVirtualMemory);

		std::string endTVM_MB = std::to_string(memoryCheckRange.endCheck.pTotalVirtualMemory / (1024 * 1024));
		std::string endTUVM_MB = std::to_string(memoryCheckRange.endCheck.pTotalUsedVirtualMemory / (1024 * 1024));
		std::string endCUVM_MB = std::to_string(memoryCheckRange.endCheck.pCurrentlyUsedVirtualMemory / (1024 * 1024));

		std::string endCheck = "At the End Frame, " + std::to_string(memoryCheckRange.endCheck.pFrame) + ", : \n ";
		endCheck = endCheck + ("Total Virtual Memory : " + endTVM_B + " bytes, " + endTVM_MB + " MB. \n");
		endCheck = endCheck + ("Total Used Virtual Memory By All Processes : " + endTUVM_B + " bytes, " + endTUVM_MB + " MB. \n");
		endCheck = endCheck + ("Virtual Memory used by this Process : " + endCUVM_B + " bytes, " + endCUVM_MB + " MB. \n \n");

		//
		std::string differenceCheck = "Difference : \n";
		unsigned long long difference = 0;

		if (memoryCheckRange.startCheck.pCurrentlyUsedVirtualMemory > memoryCheckRange.endCheck.pCurrentlyUsedVirtualMemory)
		{
			difference = memoryCheckRange.startCheck.pCurrentlyUsedVirtualMemory - memoryCheckRange.endCheck.pCurrentlyUsedVirtualMemory;
			differenceCheck = differenceCheck + "-" + std::to_string(difference) + "\n \n";
		}
		else
		{
			difference = memoryCheckRange.endCheck.pCurrentlyUsedVirtualMemory - memoryCheckRange.startCheck.pCurrentlyUsedVirtualMemory;
			differenceCheck = differenceCheck + std::to_string(difference) + "\n \n";
		}


		//	
		std::string filename = getExecutableName();

		// Now we have a folder and a filename, look for an available filename (we don't overwrite existing files)
		std::string prefix = std::string(filename);
		std::string executableDir = getExecutableDirectory();
		std::string txtFile;

		//	
		if (findAvailableFilename(prefix, executableDir, "txt", txtFile))
		{
			std::ofstream of;
			of.open(txtFile);
			of << differenceCheck;
			of << startCheck;
			of << endCheck;
			of.close();
		}
		else
		{
			logError("Could not find available filename when checking memory");
		}

	}

	void Sample::startVideoCapture()
    {
        // create the capture object and frame buffer
        VideoEncoder::Desc desc;
        desc.flipY      = false;
        desc.codec      = mVideoCapture.pUI->getCodec();
        desc.filename   = mVideoCapture.pUI->getFilename();
        desc.format     = VideoEncoder::InputFormat::R8G8B8A8;
        desc.fps        = mVideoCapture.pUI->getFPS();
        desc.height     = mpDefaultFBO->getHeight();
        desc.width      = mpDefaultFBO->getWidth();
        desc.bitrateMbps = mVideoCapture.pUI->getBitrate();
        desc.gopSize    = mVideoCapture.pUI->getGopSize();

        mVideoCapture.pVideoCapture = VideoEncoder::create(desc);

        assert(mVideoCapture.pVideoCapture);
        mVideoCapture.pFrame = new uint8_t[desc.width*desc.height * 4];

        mVideoCapture.timeDelta = 1 / (float)desc.fps;

        if(mVideoCapture.pUI->useTimeRange())
        {
            if(mVideoCapture.pUI->getStartTime() > mVideoCapture.pUI->getEndTime())
            {
                mVideoCapture.timeDelta = -mVideoCapture.timeDelta;
            }
            mCurrentTime = mVideoCapture.pUI->getStartTime();
            if(!mVideoCapture.pUI->captureUI())
            {
                mShowUI = false;
            }
        }
    }

    void Sample::endVideoCapture()
    {
        if(mVideoCapture.pVideoCapture)
        {
            mVideoCapture.pVideoCapture->endCapture();
            mShowUI = true;
        }
        mVideoCapture.pUI = nullptr;
        mVideoCapture.pVideoCapture = nullptr;
        safe_delete_array(mVideoCapture.pFrame);
    }

    void Sample::captureVideoFrame()
    {
        if(mVideoCapture.pVideoCapture)
        {
            mVideoCapture.pVideoCapture->appendFrame(mpRenderContext->readTextureSubresource(mpDefaultFBO->getColorTexture(0).get(), 0).data());

            if(mVideoCapture.pUI->useTimeRange())
            {
                if(mVideoCapture.timeDelta >= 0)
                {
                    if(mCurrentTime >= mVideoCapture.pUI->getEndTime())
                    {
                        endVideoCapture();
                    }
                }
                else if(mCurrentTime < mVideoCapture.pUI->getEndTime())
                {
                    endVideoCapture();
                }
            }
        }
    }

    void Sample::shutdownApp()
    {
        mpWindow->shutdown();
    }

    void Sample::pollForEvents()
    {
        mpWindow->pollForEvents();
    }

    void Sample::setWindowTitle(const std::string& title)
    {
        mpWindow->setWindowTitle(title);
    }
}