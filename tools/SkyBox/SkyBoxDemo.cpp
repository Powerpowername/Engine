#include "SkyBoxDemo.h"
#include "Camera/QuaternionCamera.hpp"
#include "Command/Command.hpp"
#include "Descriptor/Descriptor.hpp"
#include "Device/Device.hpp"
#include "PipelineState/PipelineState.hpp"
#include "Render/SkyBox.hpp"
#include "Resource/ConstantBuffer.hpp"
#include "Resource/SwapChain.hpp"
#include "Resource/Texture.hpp"
#include "RootSignature/RootSignature.hpp"
#include "Shader/Shader.hpp"

#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

using Microsoft::WRL::ComPtr;

namespace SkyBoxDemo
{
    struct SkyBoxCB
    {
        DirectX::XMFLOAT4X4 viewProjection{};
    };

    static std::shared_ptr<Device> skyBoxDevice = nullptr;
    static std::shared_ptr<CommandQueue> skyBoxCommandQueue = nullptr;
    static std::shared_ptr<CommandAllocator> skyBoxCommandAllocator = nullptr;
    static std::shared_ptr<CommandList> skyBoxCommandList = nullptr;
    static std::shared_ptr<DescriptorHeap> skyBoxRTVDescriptorHeap = nullptr;
    static std::shared_ptr<DescriptorHeap> skyBoxDSVDescriptorHeap = nullptr;
    static std::shared_ptr<DescriptorHeap> skyBoxSRVDescriptorHeap = nullptr;
    static std::shared_ptr<SwapChain> skyBoxSwapchain = nullptr;
    static std::shared_ptr<DepthTexture> skyBoxDepthTexture = nullptr;
    static std::shared_ptr<QuaternionCamera> skyBoxCamera = nullptr;
    static std::shared_ptr<SkyBox> skyBoxResource = nullptr;
    static Shader skyBoxShader;
    static RootSignature skyBoxRootSignature;
    static PipelineState skyBoxPipelineState;
    static ConstantBuffer skyBoxConstantBuffer;

    static HWND skyBoxHwnd = nullptr;
    static bool isInitialized = false;
    static D3D12_VIEWPORT skyBoxViewport{};
    static D3D12_RECT skyBoxScissor{};
    static POINT lastMousePosition{};
    static bool hasLastMousePosition = false;

    static std::filesystem::path GetModuleDirectory()
    {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(nullptr,modulePath,MAX_PATH);
        if(length == 0 || length >= MAX_PATH)
        {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(modulePath).parent_path();
    }

    static void LogLine(const std::string& text)
    {
        std::ofstream logFile(GetModuleDirectory() / "skybox_demo.log",std::ios::app);
        logFile << text << "\n";
    }

    static std::filesystem::path FindFileUpward(
        std::filesystem::path basePath,
        const std::filesystem::path& relativePath)
    {
        std::error_code error;
        for(int i = 0;i < 10;i++)
        {
            std::filesystem::path candidate = basePath / relativePath;
            if(std::filesystem::exists(candidate,error))
            {
                return candidate;
            }

            std::filesystem::path parent = basePath.parent_path();
            if(parent.empty() || parent == basePath)
            {
                break;
            }
            basePath = parent;
        }
        return {};
    }

    static std::filesystem::path ResolveFilePath(const std::filesystem::path& relativePath)
    {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(nullptr,modulePath,MAX_PATH);
        if(length > 0 && length < MAX_PATH)
        {
            std::filesystem::path filePath = FindFileUpward(std::filesystem::path(modulePath).parent_path(),relativePath);
            if(!filePath.empty())
            {
                return filePath;
            }
        }

        std::error_code error;
        std::filesystem::path currentPath = std::filesystem::current_path(error);
        if(!error)
        {
            std::filesystem::path filePath = FindFileUpward(currentPath,relativePath);
            if(!filePath.empty())
            {
                return filePath;
            }
        }

        return {};
    }

    static bool CreateDepthTexture()
    {
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2D.MipSlice = 0;

        auto depthHandle = skyBoxDSVDescriptorHeap->GetFreeDescriptorHandle();
        if(!depthHandle)
        {
            return false;
        }

        DepthTextureCreateInfo depthCreateInfo{};
        depthCreateInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS,
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        depthCreateInfo.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthCreateInfo.hasClearValue = true;
        depthCreateInfo.clearValue = clearValue;
        depthCreateInfo.mainDsvDesc = dsvDesc;
        depthCreateInfo.mainDsvCpuHandle = depthHandle->cpuHandle;

        return DepthTextureGenerator::CreateDepthTexture(
            skyBoxDevice->GetDeviceComPtr(),
            depthCreateInfo,
            skyBoxDepthTexture);
    }

    static bool CreateSkyBoxPipeline()
    {
        if(!skyBoxConstantBuffer.Init(skyBoxDevice->GetDeviceComPtr(),sizeof(SkyBoxCB)))
        {
            LogLine("CreateSkyBoxPipeline failed: constant buffer");
            return false;
        }

        std::filesystem::path shaderPath = ResolveFilePath("resource/shaders/skybox/skybox.hlsl");
        if(shaderPath.empty())
        {
            LogLine("CreateSkyBoxPipeline failed: shader path");
            return false;
        }

        Shader::StageCompileDesc vsDesc{"VSMain","vs_5_1","VS"};
        Shader::StageCompileDesc psDesc{"PSMain","ps_5_1","PS"};
        if(!skyBoxShader.CompileGraphicsFromFile(shaderPath.wstring(),vsDesc,psDesc))
        {
            LogLine("CreateSkyBoxPipeline failed: shader compile");
            LogLine(skyBoxShader.GetError());
            return false;
        }

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        skyBoxRootSignature.Reset();
        skyBoxRootSignature.AddRootCBV(0);
        skyBoxRootSignature.AddSRVTable(1,0,D3D12_SHADER_VISIBILITY_PIXEL);
        skyBoxRootSignature.AddStaticSampler(sampler);
        if(!skyBoxRootSignature.CreateRootSignature(skyBoxDevice->GetDeviceComPtr()))
        {
            LogLine("CreateSkyBoxPipeline failed: root signature");
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputElements[1]{};
        inputElements[0] = {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_INPUT_LAYOUT_DESC inputLayout{inputElements,1};

        GraphicsPipelineDesc psoDesc{};
        psoDesc.rootSignature = skyBoxRootSignature.GetRootSignatureComPtr();
        psoDesc.vs = skyBoxShader.GetVsShaderByteCode();
        psoDesc.ps = skyBoxShader.GetPsShaderByteCode();
        psoDesc.inputLayout = inputLayout;
        psoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.rtvFormats[0] = skyBoxSwapchain->GetFormat();
        psoDesc.numRenderTargets = 1;
        psoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        if(!skyBoxPipelineState.CreateGraphicsPipelineState(skyBoxDevice->GetDeviceComPtr(),psoDesc))
        {
            LogLine("CreateSkyBoxPipeline failed: pso");
            return false;
        }

        return true;
    }

    static bool InitializeSkyBoxResource(ComPtr<ID3D12GraphicsCommandList> commandListObj)
    {
        std::filesystem::path positiveX = ResolveFilePath("resource/textures/sky/clouds1/clouds1_east.bmp");
        std::filesystem::path negativeX = ResolveFilePath("resource/textures/sky/clouds1/clouds1_west.bmp");
        std::filesystem::path positiveY = ResolveFilePath("resource/textures/sky/clouds1/clouds1_up.bmp");
        std::filesystem::path negativeY = ResolveFilePath("resource/textures/sky/clouds1/clouds1_down.bmp");
        std::filesystem::path positiveZ = ResolveFilePath("resource/textures/sky/clouds1/clouds1_north.bmp");
        std::filesystem::path negativeZ = ResolveFilePath("resource/textures/sky/clouds1/clouds1_south.bmp");
        if(positiveX.empty() || negativeX.empty() || positiveY.empty() ||
            negativeY.empty() || positiveZ.empty() || negativeZ.empty())
        {
            LogLine("initialize failed: skybox texture paths");
            return false;
        }

        std::string positiveXString = positiveX.generic_string();
        std::string negativeXString = negativeX.generic_string();
        std::string positiveYString = positiveY.generic_string();
        std::string negativeYString = negativeY.generic_string();
        std::string positiveZString = positiveZ.generic_string();
        std::string negativeZString = negativeZ.generic_string();

        skyBoxResource = std::make_shared<SkyBox>();
        return skyBoxResource->Initialize(
            skyBoxDevice->GetDeviceComPtr().Get(),
            commandListObj.Get(),
            skyBoxSRVDescriptorHeap.get(),
            positiveXString.c_str(),
            negativeXString.c_str(),
            positiveYString.c_str(),
            negativeYString.c_str(),
            positiveZString.c_str(),
            negativeZString.c_str());
    }

    bool initialize(HWND hwnd)
    {
        LogLine("initialize begin");
        if(isInitialized)
        {
            return true;
        }
        if(hwnd == nullptr)
        {
            LogLine("initialize failed: hwnd is null");
            return false;
        }

        skyBoxHwnd = hwnd;
        skyBoxDevice = std::make_shared<Device>();
        if(!skyBoxDevice->Initialize())
        {
            LogLine("initialize failed: device");
            return false;
        }

        skyBoxCommandQueue = std::make_shared<CommandQueue>();
        if(!skyBoxCommandQueue->Initialize(skyBoxDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT))
        {
            LogLine("initialize failed: command queue");
            return false;
        }

        skyBoxCommandAllocator = std::make_shared<CommandAllocator>();
        if(!skyBoxCommandAllocator->Initialize(skyBoxDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT))
        {
            LogLine("initialize failed: command allocator");
            return false;
        }

        skyBoxCommandList = std::make_shared<CommandList>();
        if(!skyBoxCommandList->Initialize(
            skyBoxDevice->GetDeviceComPtr(),
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            skyBoxCommandAllocator->GetCommandAllocatorComPtr()))
        {
            LogLine("initialize failed: command list");
            return false;
        }

        skyBoxRTVDescriptorHeap = std::make_shared<DescriptorHeap>();
        skyBoxDSVDescriptorHeap = std::make_shared<DescriptorHeap>();
        skyBoxSRVDescriptorHeap = std::make_shared<DescriptorHeap>();
        if(!skyBoxRTVDescriptorHeap->CreateDescriptorHeap(
            skyBoxDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            2))
        {
            LogLine("initialize failed: rtv heap");
            return false;
        }
        if(!skyBoxDSVDescriptorHeap->CreateDescriptorHeap(
            skyBoxDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            1))
        {
            LogLine("initialize failed: dsv heap");
            return false;
        }
        if(!skyBoxSRVDescriptorHeap->CreateDescriptorHeap(
            skyBoxDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            4))
        {
            LogLine("initialize failed: srv heap");
            return false;
        }

        std::vector<DescriptorHandle> swapChainHandles;
        auto swapChainHandle0 = skyBoxRTVDescriptorHeap->GetFreeDescriptorHandle();
        auto swapChainHandle1 = skyBoxRTVDescriptorHeap->GetFreeDescriptorHandle();
        if(!swapChainHandle0 || !swapChainHandle1)
        {
            LogLine("initialize failed: swapchain rtv handles");
            return false;
        }
        swapChainHandles.push_back(*swapChainHandle0);
        swapChainHandles.push_back(*swapChainHandle1);

        skyBoxSwapchain = std::make_shared<SwapChain>();
        if(!skyBoxSwapchain->Create(
            skyBoxDevice->GetFactoryComPtr(),
            skyBoxCommandQueue->GetCommandQueueComPtr(),
            skyBoxDevice->GetDeviceComPtr(),
            swapChainHandles,
            2,
            skyBoxHwnd,
            WINDOW_WIDTH,
            WINDOW_HEIGHT))
        {
            LogLine("initialize failed: swapchain");
            return false;
        }

        skyBoxCamera = std::make_shared<QuaternionCamera>();
        skyBoxCamera->SetPerspective(DirectX::XM_PIDIV4,static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),0.1f,1000.0f);
        skyBoxCamera->SetPosition(DirectX::XMFLOAT3(0.0f,0.0f,0.0f));
        skyBoxCamera->SetOrientationCameraLookAt(DirectX::XMFLOAT3(0.0f,0.0f,1.0f));

        if(!skyBoxCommandAllocator->Reset())
        {
            LogLine("initialize failed: reset command allocator");
            return false;
        }
        if(!skyBoxCommandList->Reset(skyBoxCommandAllocator->GetCommandAllocatorComPtr()))
        {
            LogLine("initialize failed: reset command list");
            return false;
        }
        auto commandListObj = skyBoxCommandList->GetGraphicsComPtr();

        if(!CreateDepthTexture())
        {
            LogLine("initialize failed: depth texture");
            return false;
        }

        if(!InitializeSkyBoxResource(commandListObj))
        {
            LogLine("initialize failed: skybox resource");
            return false;
        }
        if(!CreateSkyBoxPipeline())
        {
            LogLine("initialize failed: skybox pipeline");
            return false;
        }

        skyBoxViewport.TopLeftX = 0.0f;
        skyBoxViewport.TopLeftY = 0.0f;
        skyBoxViewport.Width = static_cast<float>(WINDOW_WIDTH);
        skyBoxViewport.Height = static_cast<float>(WINDOW_HEIGHT);
        skyBoxViewport.MinDepth = 0.0f;
        skyBoxViewport.MaxDepth = 1.0f;
        skyBoxScissor.left = 0;
        skyBoxScissor.top = 0;
        skyBoxScissor.right = WINDOW_WIDTH;
        skyBoxScissor.bottom = WINDOW_HEIGHT;

        if(FAILED(commandListObj->Close()))
        {
            LogLine("initialize failed: close command list");
            return false;
        }

        ID3D12CommandList* initCommandLists[] = {commandListObj.Get()};
        skyBoxCommandQueue->Execute(initCommandLists,1);
        skyBoxCommandQueue->Flush();
        skyBoxResource->ReleaseUploadResources();

        isInitialized = true;
        LogLine("initialize ok");
        return true;
    }

    void InputCallBack()
    {
        if(!isInitialized || !skyBoxCamera || skyBoxHwnd == nullptr)
        {
            return;
        }

        float moveSpeed = 0.05f;
        if((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            moveSpeed = 0.15f;
        }

        if((GetAsyncKeyState('W') & 0x8000) != 0)
        {
            skyBoxCamera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,moveSpeed));
        }
        if((GetAsyncKeyState('S') & 0x8000) != 0)
        {
            skyBoxCamera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,-moveSpeed));
        }
        if((GetAsyncKeyState('A') & 0x8000) != 0)
        {
            skyBoxCamera->MoveLocal(DirectX::XMFLOAT3(-moveSpeed,0.0f,0.0f));
        }
        if((GetAsyncKeyState('D') & 0x8000) != 0)
        {
            skyBoxCamera->MoveLocal(DirectX::XMFLOAT3(moveSpeed,0.0f,0.0f));
        }
        if((GetAsyncKeyState('Q') & 0x8000) != 0)
        {
            skyBoxCamera->MoveWorld(DirectX::XMFLOAT3(0.0f,-moveSpeed,0.0f));
        }
        if((GetAsyncKeyState('E') & 0x8000) != 0)
        {
            skyBoxCamera->MoveWorld(DirectX::XMFLOAT3(0.0f,moveSpeed,0.0f));
        }

        POINT currentMousePosition{};
        if(GetCursorPos(&currentMousePosition))
        {
            ScreenToClient(skyBoxHwnd,&currentMousePosition);
            if((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
            {
                if(hasLastMousePosition)
                {
                    float yawRadians = static_cast<float>(currentMousePosition.x - lastMousePosition.x) * 0.005f;
                    float pitchRadians = static_cast<float>(currentMousePosition.y - lastMousePosition.y) * 0.005f;
                    skyBoxCamera->AddYawPitch(yawRadians,pitchRadians);
                }
                lastMousePosition = currentMousePosition;
                hasLastMousePosition = true;
            }
            else
            {
                lastMousePosition = currentMousePosition;
                hasLastMousePosition = false;
            }
        }
    }

    void RenderCallBack()
    {
        if(!isInitialized || !skyBoxCommandAllocator || !skyBoxCommandList || !skyBoxSwapchain || !skyBoxDepthTexture || !skyBoxResource)
        {
            return;
        }

        if(!skyBoxCommandAllocator->Reset())
        {
            LogLine("render failed: reset command allocator");
            return;
        }
        if(!skyBoxCommandList->Reset(skyBoxCommandAllocator->GetCommandAllocatorComPtr()))
        {
            LogLine("render failed: reset command list");
            return;
        }

        auto commandListObj = skyBoxCommandList->GetGraphicsComPtr();
        auto backBufferResource = skyBoxSwapchain->GetCurrentBackBufferResource();
        auto backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            backBufferResource.Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandListObj->ResourceBarrier(1,&backBufferTransition);

        commandListObj->RSSetViewports(1,&skyBoxViewport);
        commandListObj->RSSetScissorRects(1,&skyBoxScissor);

        const FLOAT clearColor[4] = {0.02f,0.03f,0.05f,1.0f};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = skyBoxSwapchain->GetCurrentRTVCPUHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = skyBoxDepthTexture->GetDsvCpuHandle();
        commandListObj->ClearRenderTargetView(rtv,clearColor,0,nullptr);
        commandListObj->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);
        commandListObj->OMSetRenderTargets(1,&rtv,FALSE,&dsv);

        DirectX::XMFLOAT4X4 viewProjection{};
        DirectX::XMStoreFloat4x4(&viewProjection,skyBoxCamera->GetViewProjection());
        SkyBoxCB skyBoxCB{};
        skyBoxCB.viewProjection = viewProjection;
        skyBoxConstantBuffer.Update(&skyBoxCB,sizeof(skyBoxCB));

        ID3D12DescriptorHeap* descriptorHeaps[] = {skyBoxSRVDescriptorHeap->GetDescriptorHeapComPtr().Get()};
        commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
        commandListObj->SetGraphicsRootSignature(skyBoxRootSignature.GetRootSignatureComPtr().Get());
        commandListObj->SetPipelineState(skyBoxPipelineState.GetPipelineStateComPtr().Get());
        commandListObj->SetGraphicsRootConstantBufferView(0,skyBoxConstantBuffer.GPUAddress());
        commandListObj->SetGraphicsRootDescriptorTable(1,skyBoxResource->GetSrvGpuHandle());
        skyBoxResource->Draw(commandListObj.Get());

        backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            backBufferResource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        commandListObj->ResourceBarrier(1,&backBufferTransition);

        if(FAILED(commandListObj->Close()))
        {
            LogLine("render failed: close command list");
            return;
        }

        ID3D12CommandList* commandLists[] = {commandListObj.Get()};
        skyBoxCommandQueue->Execute(commandLists,1);
        skyBoxSwapchain->Present();
        skyBoxCommandQueue->Flush();
    }

    void Shutdown()
    {
        LogLine("shutdown begin");
        if(skyBoxCommandQueue)
        {
            skyBoxCommandQueue->Flush();
        }

        skyBoxResource.reset();

        skyBoxDepthTexture.reset();
        skyBoxSwapchain.reset();
        skyBoxCamera.reset();
        skyBoxSRVDescriptorHeap.reset();
        skyBoxDSVDescriptorHeap.reset();
        skyBoxRTVDescriptorHeap.reset();
        skyBoxCommandList.reset();
        skyBoxCommandAllocator.reset();
        skyBoxCommandQueue.reset();
        skyBoxDevice.reset();
        skyBoxHwnd = nullptr;
        hasLastMousePosition = false;
        isInitialized = false;
        LogLine("shutdown end");
    }
}

bool InitializeSkyBoxDemo(void* hwnd)
{
    return SkyBoxDemo::initialize(static_cast<HWND>(hwnd));
}

void InputSkyBoxDemo()
{
    SkyBoxDemo::InputCallBack();
}

void RenderSkyBoxDemo()
{
    SkyBoxDemo::RenderCallBack();
}

void ShutdownSkyBoxDemo()
{
    SkyBoxDemo::Shutdown();
}
