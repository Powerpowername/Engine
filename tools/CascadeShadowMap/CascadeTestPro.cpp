#include "CascadeTestPro.h"
#include "Device/Device.hpp"
#include "Resource/SwapChain.hpp"
#include "Command/Command.hpp"
#include "Descriptor/Descriptor.hpp"
#include "PipelineState/PipelineState.hpp"
#include "RootSignature/RootSignature.hpp"
#include "Shader/Shader.hpp"
#include "Resource/Texture.hpp"
#include "Camera/QuaternionCamera.hpp"
#include "tools/CascadeShadowMap/CascadeShadowMap.hpp"
#include "Resource/Mesh.hpp"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define SHADOW_MAP_WIDTH 2048
#define SHADOW_MAP_HEIGHT 2048
#define CUBE_COUNT 20
#define CUBE_TEXTURE_GROUP_COUNT (CUBE_COUNT / 2)

namespace CascadeShadow
{
    static std::shared_ptr<Device> cascadeDevice = nullptr;
    static std::shared_ptr<CommandQueue> cascadeCommandQueue = nullptr;
    static std::shared_ptr<CommandAllocator> cascadeCommandAllocator = nullptr;
    static std::shared_ptr<CommandList> cascadeCommandList = nullptr;

    static std::shared_ptr<DescriptorHeap> cascadeRTVDescriptorHeap = nullptr;
    static std::shared_ptr<DescriptorHeap> cascadeDSVDescriptorHeap = nullptr;
    static std::shared_ptr<DescriptorHeap> cascadeSRVDescriptorHeap = nullptr;

    static std::shared_ptr<QuaternionCamera> camera = nullptr;
    static std::shared_ptr<SwapChain> cascadeSwapchain = nullptr;
    static std::shared_ptr<DepthTexture> depthTexture = nullptr;

    static Shader depthShader;
    static Shader sceneShader;
    static Texture diffuseTexture;
    static std::array<Texture,CUBE_TEXTURE_GROUP_COUNT> cubeDiffuseTextures;
    static TextureGenerator textureGenerator;
    static CascadeShadowMapCalculate cascadeShadowMapCalculate;
    static CascadeShadowMap cascadeShadowMap;

    static VertexBuffer<Vertex> planeVertexBuffer;
    static IndexBuffer<Index> planeIndexBuffer;
    static VertexBuffer<Vertex> cubeVertexBuffer;
    static IndexBuffer<Index> cubeIndexBuffer;

    static PipelineState depthPso;
    static PipelineState scenePso;
    static RootSignature depthPsoRootSignature;
    static RootSignature scenePsoRootSignature;

    static ConstantBuffer lightConstantBuffer;
    static ConstantBuffer lightSpaceMatricesConstantBuffer;
    static std::array<ConstantBuffer,CUBE_COUNT + 1> sceneCameraConstantBuffers;

    static HWND cascadeHwnd = nullptr;
    static bool isInitialized = false;

    static D3D12_VIEWPORT sceneViewport{};
    static D3D12_RECT sceneScissor{};
    static D3D12_VIEWPORT shadowViewport{};
    static D3D12_RECT shadowScissor{};

    static std::vector<Vertex> planeVertex;
    static std::vector<Index> planeIndex;
    static std::vector<Vertex> cubeVertex;
    static std::vector<Index> cubeIndex;
    static std::vector<RenderResourceConifg> renderResourceConifg;

    static std::array<DirectX::XMFLOAT4X4,CUBE_COUNT + 1> modelMatrices{};
    static std::array<DirectX::XMFLOAT4X4,16> lightSpaceMatricesData{};
    static std::array<D3D12_GPU_DESCRIPTOR_HANDLE,CUBE_TEXTURE_GROUP_COUNT> cubeDiffuseSrvTableGpuHandles{};
    static DirectX::XMFLOAT3 lightDir = DirectX::XMFLOAT3(-0.70f,0.55f,-0.45f);
    static POINT lastMousePosition{};
    static bool hasLastMousePosition = false;

    static DirectX::XMFLOAT3 GetShadowLightDirection()
    {
        return DirectX::XMFLOAT3(-lightDir.x, -lightDir.y, -lightDir.z);
    }

    bool initialize(HWND hwnd)
    {
        if(isInitialized)
            return true;
        if(hwnd == nullptr)
            return false;

        cascadeHwnd = hwnd;

        // ---------------- device / command ----------------
        cascadeDevice = std::make_shared<Device>();
        if(!cascadeDevice->Initialize())
            return false;

        cascadeCommandQueue = std::make_shared<CommandQueue>();
        if(!cascadeCommandQueue->Initialize(cascadeDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT))
            return false;

        cascadeCommandAllocator = std::make_shared<CommandAllocator>();
        if(!cascadeCommandAllocator->Initialize(cascadeDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT))
            return false;

        cascadeCommandList = std::make_shared<CommandList>();
        if(!cascadeCommandList->Initialize(
            cascadeDevice->GetDeviceComPtr(),
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            cascadeCommandAllocator->GetCommandAllocatorComPtr()))
            return false;

        // ---------------- descriptor / swapchain ----------------
        cascadeRTVDescriptorHeap = std::make_shared<DescriptorHeap>();
        cascadeDSVDescriptorHeap = std::make_shared<DescriptorHeap>();
        cascadeSRVDescriptorHeap = std::make_shared<DescriptorHeap>();

        if(!cascadeRTVDescriptorHeap->CreateDescriptorHeap(
            cascadeDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            2))
            return false;

        if(!cascadeDSVDescriptorHeap->CreateDescriptorHeap(
            cascadeDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            2))
            return false;

        if(!cascadeSRVDescriptorHeap->CreateDescriptorHeap(
            cascadeDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            2 + CUBE_TEXTURE_GROUP_COUNT * 2 + 1))
            return false;

        std::vector<DescriptorHandle> swapChainDesc;
        auto swapChainDesc0 = cascadeRTVDescriptorHeap->GetFreeDescriptorHandle();
        auto swapChainDesc1 = cascadeRTVDescriptorHeap->GetFreeDescriptorHandle();
        if(!swapChainDesc0 || !swapChainDesc1)
            return false;
        swapChainDesc.push_back(*swapChainDesc0);
        swapChainDesc.push_back(*swapChainDesc1);

        cascadeSwapchain = std::make_shared<SwapChain>();
        if(!cascadeSwapchain->Create(
            cascadeDevice->GetFactoryComPtr(),
            cascadeCommandQueue->GetCommandQueueComPtr(),
            cascadeDevice->GetDeviceComPtr(),
            swapChainDesc,
            2,
            cascadeHwnd,
            WINDOW_WIDTH,
            WINDOW_HEIGHT))
            return false;

        // ---------------- camera ----------------
        camera = std::make_shared<QuaternionCamera>();
        camera->SetPerspective(DirectX::XM_PIDIV4,static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),0.1f,1000.0f);
        camera->SetPosition(DirectX::XMFLOAT3(180.0f,120.0f,-260.0f));
        camera->SetOrientationCameraLookAt(DirectX::XMFLOAT3(0.0f,80.0f,0.0f));

        // ---------------- command reset ----------------
        if(!cascadeCommandAllocator->Reset())
            return false;
        if(!cascadeCommandList->Reset(cascadeCommandAllocator->GetCommandAllocatorComPtr()))
            return false;

        auto commandListObj = cascadeCommandList->GetGraphicsComPtr();

        // ---------------- scene depth texture ----------------
        D3D12_CLEAR_VALUE sceneDepthClearValue{};
        sceneDepthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        sceneDepthClearValue.DepthStencil.Depth = 1.0f;
        sceneDepthClearValue.DepthStencil.Stencil = 0;

        D3D12_DEPTH_STENCIL_VIEW_DESC sceneDepthDsvDesc{};
        sceneDepthDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        sceneDepthDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        sceneDepthDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        sceneDepthDsvDesc.Texture2D.MipSlice = 0;

        DepthTextureCreateInfo depthTextureCreateInfo;
        depthTextureCreateInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS,
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        depthTextureCreateInfo.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthTextureCreateInfo.hasClearValue = true;
        depthTextureCreateInfo.clearValue = sceneDepthClearValue;
        depthTextureCreateInfo.mainDsvDesc = sceneDepthDsvDesc;
        auto sceneDepthDsv = cascadeDSVDescriptorHeap->GetFreeDescriptorHandle();
        if(!sceneDepthDsv)
            return false;
        depthTextureCreateInfo.mainDsvCpuHandle = sceneDepthDsv->cpuHandle;
        if(!DepthTextureGenerator::CreateDepthTexture(
            cascadeDevice->GetDeviceComPtr(),
            depthTextureCreateInfo,
            depthTexture))
            return false;

        // ---------------- diffuse texture ----------------
        unsigned int diffusePixel = 0xffffffff;
        auto diffuseTextureSrv = cascadeSRVDescriptorHeap->GetFreeDescriptorHandle();
        if(!diffuseTextureSrv)
            return false;
        auto diffuseShadowSrv = cascadeSRVDescriptorHeap->GetFreeDescriptorHandle();
        if(!diffuseShadowSrv)
            return false;
        D3D12_CPU_DESCRIPTOR_HANDLE diffuseShadowSrvCpuHandle = diffuseShadowSrv->cpuHandle;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,CUBE_TEXTURE_GROUP_COUNT> cubeShadowSrvCpuHandles{};
        std::array<TextureCreateResult,CUBE_TEXTURE_GROUP_COUNT> cubeTextureCreateResults{};
        std::array<unsigned int,CUBE_TEXTURE_GROUP_COUNT> cubeDiffusePixels{
            0xff4040ff,
            0xff40a0ff,
            0xff40ff80,
            0xffffd040,
            0xffff6040,
            0xffff40c0,
            0xffa040ff,
            0xff40ffff,
            0xff90ff40,
            0xffd090ff
        };
        TextureCreateResult diffuseTextureCreateResult;
        Texture2DCreateInfo textureCreateInfo;
        textureCreateInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            1,
            1,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_NONE);
        textureCreateInfo.initialSubresource.pData = &diffusePixel;
        textureCreateInfo.initialSubresource.RowPitch = sizeof(diffusePixel);
        textureCreateInfo.initialSubresource.SlicePitch = sizeof(diffusePixel);
        textureCreateInfo.srvCpuHandle = diffuseTextureSrv->cpuHandle;
        textureCreateInfo.srvGpuHandle = diffuseTextureSrv->gpuHandle;
        textureCreateInfo.finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        textureCreateInfo.srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureCreateInfo.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        textureCreateInfo.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        textureCreateInfo.srvDesc.Texture2D.MostDetailedMip = 0;
        textureCreateInfo.srvDesc.Texture2D.MipLevels = 1;
        textureCreateInfo.srvDesc.Texture2D.PlaneSlice = 0;
        textureCreateInfo.srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        if(!textureGenerator.CreateTexture2D(
            cascadeDevice->GetDeviceComPtr(),
            commandListObj,
            textureCreateInfo,
            diffuseTextureCreateResult))
            return false;
        diffuseTexture = diffuseTextureCreateResult.texture;

        for(UINT i = 0;i < CUBE_TEXTURE_GROUP_COUNT;i++)
        {
            auto cubeTextureSrv = cascadeSRVDescriptorHeap->GetFreeDescriptorHandle();
            auto cubeShadowSrv = cascadeSRVDescriptorHeap->GetFreeDescriptorHandle();
            if(!cubeTextureSrv || !cubeShadowSrv)
                return false;

            Texture2DCreateInfo cubeTextureCreateInfo = textureCreateInfo;
            cubeTextureCreateInfo.initialSubresource.pData = &cubeDiffusePixels[i];
            cubeTextureCreateInfo.srvCpuHandle = cubeTextureSrv->cpuHandle;
            cubeTextureCreateInfo.srvGpuHandle = cubeTextureSrv->gpuHandle;
            if(!textureGenerator.CreateTexture2D(
                cascadeDevice->GetDeviceComPtr(),
                commandListObj,
                cubeTextureCreateInfo,
                cubeTextureCreateResults[i]))
                return false;

            cubeDiffuseTextures[i] = cubeTextureCreateResults[i].texture;
            cubeDiffuseSrvTableGpuHandles[i] = cubeTextureSrv->gpuHandle;
            cubeShadowSrvCpuHandles[i] = cubeShadowSrv->cpuHandle;
        }

        // ---------------- scene shader / pso ----------------
        Shader::StageCompileDesc sceneVSDesc{"VSMain","vs_5_1","VS"};
        Shader::StageCompileDesc scenePSDesc{"PSMain","ps_5_1","PS"};
        if(!sceneShader.CompileGraphicsFromFile(
            L"D:\\C++proj\\Engine\\resource\\shaders\\cascadeShadow\\cascadeShadow.hlsl",
            sceneVSDesc,
            scenePSDesc))
            return false;

        D3D12_INPUT_ELEMENT_DESC sceneInputElements[3]{};
        sceneInputElements[0] = {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        sceneInputElements[1] = {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        sceneInputElements[2] = {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_INPUT_LAYOUT_DESC sceneInputLayout{sceneInputElements,3};

        D3D12_STATIC_SAMPLER_DESC diffuseSampler{};
        diffuseSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        diffuseSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        diffuseSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        diffuseSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        diffuseSampler.ShaderRegister = 0;
        diffuseSampler.RegisterSpace = 0;
        diffuseSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        diffuseSampler.MinLOD = 0.0f;
        diffuseSampler.MaxLOD = D3D12_FLOAT32_MAX;
        diffuseSampler.MipLODBias = 0.0f;
        diffuseSampler.MaxAnisotropy = 1;
        diffuseSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        diffuseSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;

        D3D12_STATIC_SAMPLER_DESC shadowSampler = diffuseSampler;
        shadowSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        shadowSampler.ShaderRegister = 1;

        scenePsoRootSignature.AddSRVTable(2,0,D3D12_SHADER_VISIBILITY_PIXEL);
        scenePsoRootSignature.AddRootCBV(0);
        scenePsoRootSignature.AddRootCBV(1);
        scenePsoRootSignature.AddRootCBV(2);
        scenePsoRootSignature.AddStaticSampler(diffuseSampler);
        scenePsoRootSignature.AddStaticSampler(shadowSampler);
        if(!scenePsoRootSignature.CreateRootSignature(cascadeDevice->GetDeviceComPtr()))
            return false;

        GraphicsPipelineDesc scenePsoDesc{};
        scenePsoDesc.rootSignature = scenePsoRootSignature.GetRootSignatureComPtr();
        scenePsoDesc.vs = sceneShader.GetVsShaderByteCode();
        scenePsoDesc.ps = sceneShader.GetPsShaderByteCode();
        scenePsoDesc.inputLayout = sceneInputLayout;
        scenePsoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        scenePsoDesc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        scenePsoDesc.blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        scenePsoDesc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        scenePsoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        scenePsoDesc.rtvFormats[0] = cascadeSwapchain->GetFormat();
        scenePsoDesc.numRenderTargets = 1;
        scenePsoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        if(!scenePso.CreateGraphicsPipelineState(cascadeDevice->GetDeviceComPtr(),scenePsoDesc))
            return false;

        // ---------------- depth shader / pso ----------------
        Shader::StageCompileDesc depthVSDesc{"VSMain","vs_5_1","VS"};
        Shader::StageCompileDesc depthGSDesc{"GSMain","gs_5_1","GS"};
        Shader::StageCompileDesc depthPSDesc{"PSMain","ps_5_1","PS"};
        if(!depthShader.CompileGraphicsFromFile(
            L"D:\\C++proj\\Engine\\resource\\shaders\\cascadeShadow\\cascadeShadowDepth.hlsl",
            depthVSDesc,
            depthGSDesc,
            depthPSDesc))
            return false;

        D3D12_INPUT_ELEMENT_DESC depthInputElement{
            "POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0
        };
        D3D12_INPUT_LAYOUT_DESC depthInputLayout{&depthInputElement,1};

        depthPsoRootSignature.AddRootConstants(16,0);
        depthPsoRootSignature.AddRootCBV(1);
        if(!depthPsoRootSignature.CreateRootSignature(cascadeDevice->GetDeviceComPtr()))
            return false;

        GraphicsPipelineDesc depthPsoDesc{};
        depthPsoDesc.rootSignature = depthPsoRootSignature.GetRootSignatureComPtr();
        depthPsoDesc.vs = depthShader.GetVsShaderByteCode();
        depthPsoDesc.gs = depthShader.GetGsShaderByteCode();
        depthPsoDesc.ps = depthShader.GetPsShaderByteCode();
        depthPsoDesc.inputLayout = depthInputLayout;
        depthPsoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        depthPsoDesc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        depthPsoDesc.rasterizer.DepthBias = 1000;
        depthPsoDesc.rasterizer.DepthBiasClamp = 0.0f;
        depthPsoDesc.rasterizer.SlopeScaledDepthBias = 1.0f;
        depthPsoDesc.blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        depthPsoDesc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depthPsoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        depthPsoDesc.numRenderTargets = 0;
        depthPsoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        if(!depthPso.CreateGraphicsPipelineState(cascadeDevice->GetDeviceComPtr(),depthPsoDesc))
            return false;

        // ---------------- constant buffer ----------------
        for(auto& it : sceneCameraConstantBuffers)
        {
            if(!it.Init(cascadeDevice->GetDeviceComPtr(),sizeof(std::array<DirectX::XMFLOAT4X4,3>)))
                return false;
        }
        if(!lightConstantBuffer.Init(cascadeDevice->GetDeviceComPtr(),288))
            return false;
        if(!lightSpaceMatricesConstantBuffer.Init(cascadeDevice->GetDeviceComPtr(),sizeof(lightSpaceMatricesData)))
            return false;

        // ---------------- geometry ----------------
        planeVertex = {
            {DirectX::XMFLOAT3(-1000.0f,0.0f,-1000.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(0.0f,0.0f)},
            {DirectX::XMFLOAT3(-1000.0f,0.0f, 1000.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3( 1000.0f,0.0f, 1000.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3( 1000.0f,0.0f,-1000.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(1.0f,0.0f)}
        };
        planeIndex = {
            {0},{1},{2},
            {0},{2},{3}
        };

        cubeVertex = {
            {DirectX::XMFLOAT3(-10.0f,-10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,0.0f,1.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f,-10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,0.0f,1.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f, 10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,0.0f,1.0f), DirectX::XMFLOAT2(1.0f,0.0f)},
            {DirectX::XMFLOAT3(-10.0f, 10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,0.0f,1.0f), DirectX::XMFLOAT2(0.0f,0.0f)},

            {DirectX::XMFLOAT3( 10.0f,-10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,0.0f,-1.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3(-10.0f,-10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,0.0f,-1.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3(-10.0f, 10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,0.0f,-1.0f), DirectX::XMFLOAT2(1.0f,0.0f)},
            {DirectX::XMFLOAT3( 10.0f, 10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,0.0f,-1.0f), DirectX::XMFLOAT2(0.0f,0.0f)},

            {DirectX::XMFLOAT3(-10.0f,-10.0f,-10.0f), DirectX::XMFLOAT3(-1.0f,0.0f,0.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3(-10.0f,-10.0f, 10.0f), DirectX::XMFLOAT3(-1.0f,0.0f,0.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3(-10.0f, 10.0f, 10.0f), DirectX::XMFLOAT3(-1.0f,0.0f,0.0f), DirectX::XMFLOAT2(1.0f,0.0f)},
            {DirectX::XMFLOAT3(-10.0f, 10.0f,-10.0f), DirectX::XMFLOAT3(-1.0f,0.0f,0.0f), DirectX::XMFLOAT2(0.0f,0.0f)},

            {DirectX::XMFLOAT3( 10.0f,-10.0f, 10.0f), DirectX::XMFLOAT3(1.0f,0.0f,0.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f,-10.0f,-10.0f), DirectX::XMFLOAT3(1.0f,0.0f,0.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f, 10.0f,-10.0f), DirectX::XMFLOAT3(1.0f,0.0f,0.0f), DirectX::XMFLOAT2(1.0f,0.0f)},
            {DirectX::XMFLOAT3( 10.0f, 10.0f, 10.0f), DirectX::XMFLOAT3(1.0f,0.0f,0.0f), DirectX::XMFLOAT2(0.0f,0.0f)},

            {DirectX::XMFLOAT3(-10.0f, 10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f, 10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f, 10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(1.0f,0.0f)},
            {DirectX::XMFLOAT3(-10.0f, 10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,1.0f,0.0f), DirectX::XMFLOAT2(0.0f,0.0f)},

            {DirectX::XMFLOAT3(-10.0f,-10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,-1.0f,0.0f), DirectX::XMFLOAT2(0.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f,-10.0f,-10.0f), DirectX::XMFLOAT3(0.0f,-1.0f,0.0f), DirectX::XMFLOAT2(1.0f,1.0f)},
            {DirectX::XMFLOAT3( 10.0f,-10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,-1.0f,0.0f), DirectX::XMFLOAT2(1.0f,0.0f)},
            {DirectX::XMFLOAT3(-10.0f,-10.0f, 10.0f), DirectX::XMFLOAT3(0.0f,-1.0f,0.0f), DirectX::XMFLOAT2(0.0f,0.0f)}
        };

        cubeIndex = {
            {0},{1},{2},{0},{2},{3},
            {4},{5},{6},{4},{6},{7},
            {8},{9},{10},{8},{10},{11},
            {12},{13},{14},{12},{14},{15},
            {16},{17},{18},{16},{18},{19},
            {20},{21},{22},{20},{22},{23}
        };

        if(!planeVertexBuffer.Init(cascadeDevice->GetDeviceComPtr(),commandListObj,planeVertex))
            return false;
        if(!planeVertexBuffer.UploadToDefault(commandListObj,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER))
            return false;
        if(!planeIndexBuffer.Init(cascadeDevice->GetDeviceComPtr(),commandListObj,planeIndex))
            return false;
        if(!planeIndexBuffer.UploadToDefault(commandListObj,D3D12_RESOURCE_STATE_INDEX_BUFFER))
            return false;

        if(!cubeVertexBuffer.Init(cascadeDevice->GetDeviceComPtr(),commandListObj,cubeVertex))
            return false;
        if(!cubeVertexBuffer.UploadToDefault(commandListObj,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER))
            return false;
        if(!cubeIndexBuffer.Init(cascadeDevice->GetDeviceComPtr(),commandListObj,cubeIndex))
            return false;
        if(!cubeIndexBuffer.UploadToDefault(commandListObj,D3D12_RESOURCE_STATE_INDEX_BUFFER))
            return false;

        // ---------------- model ----------------
        renderResourceConifg.clear();
        renderResourceConifg.reserve(CUBE_COUNT + 1);

        DirectX::XMStoreFloat4x4(&modelMatrices[0],DirectX::XMMatrixIdentity());
        RenderResourceConifg planeRenderResourceConifg{};
        planeRenderResourceConifg.vertexBufferView = planeVertexBuffer.GetVertexBufferView();
        planeRenderResourceConifg.indexBufferView = planeIndexBuffer.GetIndexBufferView();
        planeRenderResourceConifg.model = modelMatrices[0];
        planeRenderResourceConifg.indexCountPerInstance = static_cast<UINT>(planeIndex.size());
        planeRenderResourceConifg.instanceCount = 1;
        planeRenderResourceConifg.startIndexLocation = 0;
        planeRenderResourceConifg.baseVertexLocation = 0;
        planeRenderResourceConifg.startInstanceLocation = 0;
        renderResourceConifg.push_back(planeRenderResourceConifg);

        for(UINT i = 0;i < CUBE_COUNT;i++)
        {
            DirectX::XMStoreFloat4x4(
                &modelMatrices[i + 1],
                DirectX::XMMatrixTranslation(-90.0f + static_cast<float>(i) * 20.0f,10.0f,0.0f));
            RenderResourceConifg cubeRenderResourceConifg{};
            cubeRenderResourceConifg.vertexBufferView = cubeVertexBuffer.GetVertexBufferView();
            cubeRenderResourceConifg.indexBufferView = cubeIndexBuffer.GetIndexBufferView();
            cubeRenderResourceConifg.model = modelMatrices[i + 1];
            cubeRenderResourceConifg.indexCountPerInstance = static_cast<UINT>(cubeIndex.size());
            cubeRenderResourceConifg.instanceCount = 1;
            cubeRenderResourceConifg.startIndexLocation = 0;
            cubeRenderResourceConifg.baseVertexLocation = 0;
            cubeRenderResourceConifg.startInstanceLocation = 0;
            renderResourceConifg.push_back(cubeRenderResourceConifg);
        }

        // ---------------- cascade shadow ----------------
        if(!cascadeShadowMapCalculate.Initialize(camera,GetShadowLightDirection()))
            return false;

        for(size_t i = 0;i < cascadeShadowMapCalculate.GetLightViewMatrices().size();i++)
        {
            lightSpaceMatricesData[i] = cascadeShadowMapCalculate.GetLightViewMatrices()[i];
        }
        lightSpaceMatricesConstantBuffer.Update(lightSpaceMatricesData.data(),sizeof(lightSpaceMatricesData));

        D3D12_CLEAR_VALUE shadowClearValue{};
        shadowClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        shadowClearValue.DepthStencil.Depth = 1.0f;
        shadowClearValue.DepthStencil.Stencil = 0;

        CascadeRenderConfig cascadeRenderConfig{};
        cascadeRenderConfig.device = cascadeDevice->GetDeviceComPtr();
        cascadeRenderConfig.pso = depthPso.GetPipelineStateComPtr();
        cascadeRenderConfig.rootSignature = depthPsoRootSignature.GetRootSignatureComPtr();
        cascadeRenderConfig.commandList = commandListObj;
        cascadeRenderConfig.commandAllocator = cascadeCommandAllocator->GetCommandAllocatorComPtr();
        cascadeRenderConfig.lightSpaceMatricesCbAddress = lightSpaceMatricesConstantBuffer.GPUAddress();
        if(!cascadeShadowMap.intialize(
            cascadeRenderConfig,
            CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R32_TYPELESS,
                SHADOW_MAP_WIDTH,
                SHADOW_MAP_HEIGHT,
                5,
                1,
                1,
                0,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            *cascadeDSVDescriptorHeap,
            *cascadeSRVDescriptorHeap,
            SHADOW_MAP_WIDTH,
            SHADOW_MAP_HEIGHT,
            shadowClearValue))
            return false;

        cascadeDevice->GetDeviceComPtr()->CopyDescriptorsSimple(
            1,
            diffuseShadowSrvCpuHandle,
            cascadeShadowMap.GetSrvCpuHandle(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for(UINT i = 0;i < CUBE_TEXTURE_GROUP_COUNT;i++)
        {
            cascadeDevice->GetDeviceComPtr()->CopyDescriptorsSimple(
                1,
                cubeShadowSrvCpuHandles[i],
                cascadeShadowMap.GetSrvCpuHandle(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        // ---------------- viewport / scissor ----------------
        sceneViewport.TopLeftX = 0.0f;
        sceneViewport.TopLeftY = 0.0f;
        sceneViewport.Width = static_cast<float>(WINDOW_WIDTH);
        sceneViewport.Height = static_cast<float>(WINDOW_HEIGHT);
        sceneViewport.MinDepth = 0.0f;
        sceneViewport.MaxDepth = 1.0f;

        sceneScissor.left = 0;
        sceneScissor.top = 0;
        sceneScissor.right = WINDOW_WIDTH;
        sceneScissor.bottom = WINDOW_HEIGHT;

        shadowViewport.TopLeftX = 0.0f;
        shadowViewport.TopLeftY = 0.0f;
        shadowViewport.Width = static_cast<float>(SHADOW_MAP_WIDTH);
        shadowViewport.Height = static_cast<float>(SHADOW_MAP_HEIGHT);
        shadowViewport.MinDepth = 0.0f;
        shadowViewport.MaxDepth = 1.0f;

        shadowScissor.left = 0;
        shadowScissor.top = 0;
        shadowScissor.right = SHADOW_MAP_WIDTH;
        shadowScissor.bottom = SHADOW_MAP_HEIGHT;

        HRESULT initCloseResult = commandListObj->Close();
        if(FAILED(initCloseResult))
            return false;

        ID3D12CommandList* initCommandLists[] = {commandListObj.Get()};
        cascadeCommandQueue->Execute(initCommandLists,1);
        cascadeCommandQueue->Flush();

        isInitialized = true;
        return true;
    }

    void InputCallBack()
    {
        if(!isInitialized || !camera || cascadeHwnd == nullptr)
            return;

        float moveSpeed = 2.5f;
        if((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
            moveSpeed = 6.0f;

        if((GetAsyncKeyState('W') & 0x8000) != 0)
            camera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,moveSpeed));
        if((GetAsyncKeyState('S') & 0x8000) != 0)
            camera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,-moveSpeed));
        if((GetAsyncKeyState('A') & 0x8000) != 0)
            camera->MoveLocal(DirectX::XMFLOAT3(-moveSpeed,0.0f,0.0f));
        if((GetAsyncKeyState('D') & 0x8000) != 0)
            camera->MoveLocal(DirectX::XMFLOAT3(moveSpeed,0.0f,0.0f));
        if((GetAsyncKeyState('Q') & 0x8000) != 0)
            camera->MoveWorld(DirectX::XMFLOAT3(0.0f,-moveSpeed,0.0f));
        if((GetAsyncKeyState('E') & 0x8000) != 0)
            camera->MoveWorld(DirectX::XMFLOAT3(0.0f,moveSpeed,0.0f));

        POINT currentMousePosition{};
        if(GetCursorPos(&currentMousePosition))
        {
            ScreenToClient(cascadeHwnd,&currentMousePosition);
            if((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
            {
                if(hasLastMousePosition)
                {
                    float yawRadians = static_cast<float>(currentMousePosition.x - lastMousePosition.x) * 0.005f;
                    float pitchRadians = static_cast<float>(currentMousePosition.y - lastMousePosition.y) * 0.005f;
                    camera->AddYawPitch(yawRadians,pitchRadians);
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
        if(!isInitialized || !cascadeCommandAllocator || !cascadeCommandList || !cascadeSwapchain || !depthTexture)
            return;

        if(!cascadeCommandAllocator->Reset())
            return;
        if(!cascadeCommandList->Reset(cascadeCommandAllocator->GetCommandAllocatorComPtr()))
            return;

        auto commandListObj = cascadeCommandList->GetGraphicsComPtr();
        if(commandListObj == nullptr)
            return;

        // ---------------- update constant buffer ----------------
        cascadeShadowMapCalculate.UpdateCameraLightConfig(camera,GetShadowLightDirection());
        for(size_t i = 0;i < cascadeShadowMapCalculate.GetLightViewMatrices().size();i++)
        {
            lightSpaceMatricesData[i] = cascadeShadowMapCalculate.GetLightViewMatrices()[i];
        }
        lightSpaceMatricesConstantBuffer.Update(lightSpaceMatricesData.data(),sizeof(lightSpaceMatricesData));

        std::array<unsigned char,288> lightData{};
        DirectX::XMFLOAT3 cameraPosition = camera->GetPosition();
        int cascadeCount = static_cast<int>(cascadeShadowMapCalculate.GetShadowCascadeLevels().size());
        float farPlane = camera->GetFarZ();
        std::memcpy(lightData.data(),&lightDir,sizeof(lightDir));
        std::memcpy(lightData.data() + 12,&farPlane,sizeof(float));
        std::memcpy(lightData.data() + 16,&cameraPosition,sizeof(cameraPosition));
        std::memcpy(lightData.data() + 28,&cascadeCount,sizeof(int));
        for(size_t i = 0;i < cascadeShadowMapCalculate.GetShadowCascadeLevels().size();i++)
        {
            float splitDistance = cascadeShadowMapCalculate.GetShadowCascadeLevels()[i];
            std::memcpy(lightData.data() + 32 + static_cast<UINT>(i) * 16,&splitDistance,sizeof(float));
        }
        lightConstantBuffer.Update(lightData.data(),static_cast<UINT>(lightData.size()));

        for(size_t i = 0;i < renderResourceConifg.size();i++)
        {
            std::array<DirectX::XMFLOAT4X4,3> sceneCameraData{};
            DirectX::XMStoreFloat4x4(&sceneCameraData[0],camera->GetProjection());
            DirectX::XMStoreFloat4x4(&sceneCameraData[1],camera->GetView());
            sceneCameraData[2] = renderResourceConifg[i].model;
            sceneCameraConstantBuffers[i].Update(sceneCameraData.data(),sizeof(sceneCameraData));
        }

        // ---------------- shadow pass ----------------
        commandListObj->RSSetViewports(1,&shadowViewport);
        commandListObj->RSSetScissorRects(1,&shadowScissor);
        cascadeShadowMap.Render(renderResourceConifg);

        // ---------------- scene pass ----------------
        auto backBufferResource = cascadeSwapchain->GetCurrentBackBufferResource();
        auto backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            backBufferResource.Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandListObj->ResourceBarrier(1,&backBufferTransition);

        commandListObj->RSSetViewports(1,&sceneViewport);
        commandListObj->RSSetScissorRects(1,&sceneScissor);

        const FLOAT clearColor[4] = {0.20f,0.24f,0.30f,1.0f};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = cascadeSwapchain->GetCurrentRTVCPUHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = depthTexture->GetDsvCpuHandle();
        commandListObj->ClearRenderTargetView(rtv,clearColor,0,nullptr);
        commandListObj->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);
        commandListObj->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
        commandListObj->SetGraphicsRootSignature(scenePsoRootSignature.GetRootSignatureComPtr().Get());
        commandListObj->SetPipelineState(scenePso.GetPipelineStateComPtr().Get());
        ID3D12DescriptorHeap* descriptorHeaps[] = {cascadeSRVDescriptorHeap->GetDescriptorHeapComPtr().Get()};
        commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
        commandListObj->SetGraphicsRootDescriptorTable(0,diffuseTexture.GetSrvGpuHandle());
        commandListObj->SetGraphicsRootConstantBufferView(2,lightConstantBuffer.GPUAddress());
        commandListObj->SetGraphicsRootConstantBufferView(3,lightSpaceMatricesConstantBuffer.GPUAddress());
        commandListObj->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VERTEX_BUFFER_VIEW planeVBView = planeVertexBuffer.GetVertexBufferView();
        D3D12_INDEX_BUFFER_VIEW planeIBView = planeIndexBuffer.GetIndexBufferView();
        commandListObj->IASetVertexBuffers(0,1,&planeVBView);
        commandListObj->IASetIndexBuffer(&planeIBView);
        commandListObj->SetGraphicsRootConstantBufferView(1,sceneCameraConstantBuffers[0].GPUAddress());
        commandListObj->DrawIndexedInstanced(static_cast<UINT>(planeIndex.size()),1,0,0,0);

        D3D12_VERTEX_BUFFER_VIEW cubeVBView = cubeVertexBuffer.GetVertexBufferView();
        D3D12_INDEX_BUFFER_VIEW cubeIBView = cubeIndexBuffer.GetIndexBufferView();
        commandListObj->IASetVertexBuffers(0,1,&cubeVBView);
        commandListObj->IASetIndexBuffer(&cubeIBView);
        for(UINT i = 0;i < CUBE_COUNT;i++)
        {
            commandListObj->SetGraphicsRootDescriptorTable(0,cubeDiffuseSrvTableGpuHandles[i / 2]);
            commandListObj->SetGraphicsRootConstantBufferView(1,sceneCameraConstantBuffers[i + 1].GPUAddress());
            commandListObj->DrawIndexedInstanced(static_cast<UINT>(cubeIndex.size()),1,0,0,0);
        }

        backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            backBufferResource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        commandListObj->ResourceBarrier(1,&backBufferTransition);

        HRESULT renderCloseResult = commandListObj->Close();
        if(FAILED(renderCloseResult))
            return;

        ID3D12CommandList* commandLists[] = {commandListObj.Get()};
        cascadeCommandQueue->Execute(commandLists,1);
        cascadeSwapchain->Present();
        cascadeCommandQueue->Flush();
    }

    void Shutdown()
    {
        if(cascadeCommandQueue)
            cascadeCommandQueue->Flush();

        diffuseTexture.Reset();
        for(auto& it : cubeDiffuseTextures)
            it.Reset();
        depthTexture.reset();
        cascadeSwapchain.reset();
        camera.reset();
        cascadeSRVDescriptorHeap.reset();
        cascadeDSVDescriptorHeap.reset();
        cascadeRTVDescriptorHeap.reset();
        cascadeCommandList.reset();
        cascadeCommandAllocator.reset();
        cascadeCommandQueue.reset();
        cascadeDevice.reset();
        renderResourceConifg.clear();
        planeVertex.clear();
        planeIndex.clear();
        cubeVertex.clear();
        cubeIndex.clear();
        cascadeHwnd = nullptr;
        hasLastMousePosition = false;
        isInitialized = false;
    }
}
