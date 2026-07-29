#include "ModelDemo.h"
#include "Asset/ModelAsset.hpp"
#include "Asset/ModelGpuResource.hpp"
#include "Camera/QuaternionCamera.hpp"
#include "Command/Command.hpp"
#include "Descriptor/Descriptor.hpp"
#include "Device/Device.hpp"
#include "PipelineState/PipelineState.hpp"
#include "Resource/ConstantBuffer.hpp"
#include "Resource/SwapChain.hpp"
#include "Resource/Texture.hpp"
#include "RootSignature/RootSignature.hpp"
#include "Shader/Shader.hpp"

#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

namespace TreeModelDemo
{
    struct SceneCB
    {
        DirectX::XMFLOAT4X4 world{};//0
        DirectX::XMFLOAT4X4 viewProj{};//16
        DirectX::XMFLOAT3 cameraPos{};//32
        float padding0 = 0.0f;//
        DirectX::XMFLOAT3 lightPos{};
        float padding1 = 0.0f;
        DirectX::XMFLOAT3 lightColor{};
        float exposure = 1.0f;
    };

    struct DemoGpuVertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{};
        DirectX::XMFLOAT3 tangent{};
        DirectX::XMFLOAT3 bitangent{};
        DirectX::XMFLOAT2 uv{};
    };

    struct InstanceData
    {
        DirectX::XMFLOAT4X4 world{};
        // info.x = 0  树
        // info.x = 1  平面
        DirectX::XMFLOAT4 info{};
    };

    static constexpr UINT TREE_INSTANCE_COUNT = 10;
    static constexpr float TREE_SCALE = 4.0f;

    static std::shared_ptr<Device> modelDevice = nullptr;
    static std::shared_ptr<CommandQueue> modelCommandQueue = nullptr;
    static std::shared_ptr<CommandAllocator> modelCommandAllocator = nullptr;
    static std::shared_ptr<CommandList> modelCommandList = nullptr;
    static std::shared_ptr<DescriptorHeap> modelRTVDescriptorHeap = nullptr;
    static std::shared_ptr<DescriptorHeap> modelDSVDescriptorHeap = nullptr;
    static std::shared_ptr<DescriptorHeap> modelSRVDescriptorHeap = nullptr;
    static std::shared_ptr<SwapChain> modelSwapchain = nullptr;
    static std::shared_ptr<DepthTexture> modelDepthTexture = nullptr;
    static std::shared_ptr<QuaternionCamera> modelCamera = nullptr;

    static Shader modelShader;
    static RootSignature modelRootSignature;
    static PipelineState modelPipelineState;
    static ConstantBuffer modelSceneConstantBuffer;
    static Model treeModel;
    static std::shared_ptr<ModelGpuResource> treeGpuResource = nullptr;
    static std::vector<ComPtr<ID3D12Resource1>> treeTextureUploads;
    static ComPtr<ID3D12Resource1> treeInstanceBuffer;
    static ComPtr<ID3D12Resource1> treeInstanceUpload;
    static D3D12_VERTEX_BUFFER_VIEW treeInstanceView{};

    static ComPtr<ID3D12Resource1> planeVertexBuffer;
    static ComPtr<ID3D12Resource1> planeVertexUpload;
    static ComPtr<ID3D12Resource1> planeIndexBuffer;
    static ComPtr<ID3D12Resource1> planeIndexUpload;
    static ComPtr<ID3D12Resource1> planeInstanceBuffer;
    static ComPtr<ID3D12Resource1> planeInstanceUpload;
    static D3D12_VERTEX_BUFFER_VIEW planeVertexView{};
    static D3D12_INDEX_BUFFER_VIEW planeIndexView{};
    static D3D12_VERTEX_BUFFER_VIEW planeInstanceView{};
    static UINT planeIndexCount = 0;

    static HWND modelHwnd = nullptr;
    static bool isInitialized = false;
    static D3D12_VIEWPORT modelViewport{};
    static D3D12_RECT modelScissor{};
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
        std::ofstream logFile(GetModuleDirectory() / "model_demo.log",std::ios::app);
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

    static bool CreateBuffer(
        const void* data,
        UINT64 byteSize,
        D3D12_RESOURCE_STATES finalState,
        ComPtr<ID3D12Resource1>& defaultResource,
        ComPtr<ID3D12Resource1>& uploadResource,
        ComPtr<ID3D12GraphicsCommandList> commandList)
    {
        if(!modelDevice || !modelDevice->GetDeviceComPtr() || !commandList || data == nullptr || byteSize == 0)
        {
            return false;
        }

        auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

        if(FAILED(modelDevice->GetDeviceComPtr()->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(defaultResource.ReleaseAndGetAddressOf()))))
        {
            return false;
        }

        if(FAILED(modelDevice->GetDeviceComPtr()->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(uploadResource.ReleaseAndGetAddressOf()))))
        {
            defaultResource.Reset();
            return false;
        }

        void* mappedData = nullptr;
        D3D12_RANGE readRange{0,0};
        if(FAILED(uploadResource->Map(0,&readRange,&mappedData)))
        {
            defaultResource.Reset();
            uploadResource.Reset();
            return false;
        }

        std::memcpy(mappedData,data,static_cast<size_t>(byteSize));
        uploadResource->Unmap(0,nullptr);

        commandList->CopyBufferRegion(defaultResource.Get(),0,uploadResource.Get(),0,byteSize);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            defaultResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            finalState);
        commandList->ResourceBarrier(1,&barrier);
        return true;
    }
    // 算出模型最小的y用来当作放在平面上的位置
    static float GetTreeModelMinY()
    {
        float minY = std::numeric_limits<float>::max();
        for(const std::shared_ptr<ModelMesh>& mesh : treeModel.modelMeshs)
        {
            if(!mesh)
            {
                continue;
            }

            for(const DirectX::XMFLOAT3& position : mesh->vertices)
            {
                minY = std::min(minY,position.y);
            }
        }

        if(minY == std::numeric_limits<float>::max())
        {
            return 0.0f;
        }
        return minY;
    }

    static bool CreateTreeInstances(ComPtr<ID3D12GraphicsCommandList> commandList)
    {
        const float treeBaseYOffset = -GetTreeModelMinY() * TREE_SCALE;
        std::array<InstanceData,TREE_INSTANCE_COUNT> instances{};

        for(UINT index = 0;index < TREE_INSTANCE_COUNT;index++)
        {
            const UINT column = index % 5;
            const UINT row = index / 5;
            const float x = (static_cast<float>(column) - 2.0f) * 4.0f;
            const float z = (static_cast<float>(row) - 0.5f) * 5.0f;

            DirectX::XMMATRIX instanceWorld =
                DirectX::XMMatrixScaling(TREE_SCALE,TREE_SCALE,TREE_SCALE) *
                DirectX::XMMatrixTranslation(x,treeBaseYOffset,z);
            DirectX::XMStoreFloat4x4(&instances[index].world,instanceWorld);
            instances[index].info = DirectX::XMFLOAT4(0.0f,0.0f,0.0f,0.0f);
        }

        const UINT64 byteSize = static_cast<UINT64>(sizeof(InstanceData) * instances.size());
        if(!CreateBuffer(
            instances.data(),
            byteSize,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            treeInstanceBuffer,
            treeInstanceUpload,
            commandList))
        {
            return false;
        }
        // 实例数据同样使用顶点缓冲
        treeInstanceView.BufferLocation = treeInstanceBuffer->GetGPUVirtualAddress();
        treeInstanceView.StrideInBytes = sizeof(InstanceData);
        treeInstanceView.SizeInBytes = static_cast<UINT>(byteSize);
        return true;
    }

    static bool CreatePlane(ComPtr<ID3D12GraphicsCommandList> commandList)
    {
        const float planeSize = 24.0f;
        std::array<DemoGpuVertex,4> vertices =
        {
            DemoGpuVertex{DirectX::XMFLOAT3(-planeSize,0.0f,-planeSize),DirectX::XMFLOAT3(0.0f,1.0f,0.0f),DirectX::XMFLOAT3(1.0f,0.0f,0.0f),DirectX::XMFLOAT3(0.0f,0.0f,1.0f),DirectX::XMFLOAT2(0.0f,0.0f)},
            DemoGpuVertex{DirectX::XMFLOAT3(-planeSize,0.0f, planeSize),DirectX::XMFLOAT3(0.0f,1.0f,0.0f),DirectX::XMFLOAT3(1.0f,0.0f,0.0f),DirectX::XMFLOAT3(0.0f,0.0f,1.0f),DirectX::XMFLOAT2(0.0f,1.0f)},
            DemoGpuVertex{DirectX::XMFLOAT3( planeSize,0.0f, planeSize),DirectX::XMFLOAT3(0.0f,1.0f,0.0f),DirectX::XMFLOAT3(1.0f,0.0f,0.0f),DirectX::XMFLOAT3(0.0f,0.0f,1.0f),DirectX::XMFLOAT2(1.0f,1.0f)},
            DemoGpuVertex{DirectX::XMFLOAT3( planeSize,0.0f,-planeSize),DirectX::XMFLOAT3(0.0f,1.0f,0.0f),DirectX::XMFLOAT3(1.0f,0.0f,0.0f),DirectX::XMFLOAT3(0.0f,0.0f,1.0f),DirectX::XMFLOAT2(1.0f,0.0f)}
        };

        std::array<UINT,6> indices = {0,1,2,0,2,3};
        InstanceData planeInstance{};
        DirectX::XMStoreFloat4x4(&planeInstance.world,DirectX::XMMatrixIdentity());
        planeInstance.info = DirectX::XMFLOAT4(1.0f,0.0f,0.0f,0.0f);

        const UINT64 vertexBytes = static_cast<UINT64>(sizeof(DemoGpuVertex) * vertices.size());
        const UINT64 indexBytes = static_cast<UINT64>(sizeof(UINT) * indices.size());
        if(!CreateBuffer(
            vertices.data(),
            vertexBytes,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            planeVertexBuffer,
            planeVertexUpload,
            commandList))
        {
            return false;
        }
        if(!CreateBuffer(
            indices.data(),
            indexBytes,
            D3D12_RESOURCE_STATE_INDEX_BUFFER,
            planeIndexBuffer,
            planeIndexUpload,
            commandList))
        {
            return false;
        }
        if(!CreateBuffer(
            &planeInstance,
            sizeof(InstanceData),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            planeInstanceBuffer,
            planeInstanceUpload,
            commandList))
        {
            return false;
        }

        planeVertexView.BufferLocation = planeVertexBuffer->GetGPUVirtualAddress();
        planeVertexView.StrideInBytes = sizeof(DemoGpuVertex);
        planeVertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
        planeIndexView.BufferLocation = planeIndexBuffer->GetGPUVirtualAddress();
        planeIndexView.SizeInBytes = static_cast<UINT>(indexBytes);
        planeIndexView.Format = DXGI_FORMAT_R32_UINT;
        planeInstanceView.BufferLocation = planeInstanceBuffer->GetGPUVirtualAddress();
        planeInstanceView.StrideInBytes = sizeof(InstanceData);
        planeInstanceView.SizeInBytes = sizeof(InstanceData);
        planeIndexCount = static_cast<UINT>(indices.size());
        return true;
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

        auto depthHandle = modelDSVDescriptorHeap->GetFreeDescriptorHandle();
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
            modelDevice->GetDeviceComPtr(),
            depthCreateInfo,
            modelDepthTexture);
    }

    static bool CreatePipeline()
    {
        std::filesystem::path shaderPath = ResolveFilePath("resource/shaders/pbr/quiverTree.hlsl");
        if(shaderPath.empty())
        {
            LogLine("CreatePipeline failed: shader path not found");
            std::print("quiverTree.hlsl not found\n");
            return false;
        }
        LogLine("CreatePipeline shader: " + shaderPath.string());

        Shader::StageCompileDesc vsDesc{"VSMain","vs_5_1","VS"};
        Shader::StageCompileDesc psDesc{"PSMain","ps_5_1","PS"};
        std::error_code currentPathError;
        std::filesystem::path oldCurrentPath = std::filesystem::current_path(currentPathError);
        if(!currentPathError)
        {
            std::filesystem::current_path(shaderPath.parent_path(),currentPathError);
            if(currentPathError)
            {
                LogLine("CreatePipeline warning: set shader current path failed");
            }
        }

        if(!modelShader.CompileGraphicsFromFile(shaderPath.wstring(),vsDesc,psDesc))
        {
            if(!currentPathError)
            {
                std::error_code restoreError;
                std::filesystem::current_path(oldCurrentPath,restoreError);
            }
            LogLine("CreatePipeline failed: compile shader");
            LogLine(modelShader.GetError());
            return false;
        }
        if(!currentPathError)
        {
            std::error_code restoreError;
            std::filesystem::current_path(oldCurrentPath,restoreError);
        }
        LogLine("CreatePipeline shader compile ok");

        D3D12_STATIC_SAMPLER_DESC linearWrapSampler{};
        linearWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSampler.MipLODBias = 0.0f;
        linearWrapSampler.MaxAnisotropy = 1;
        linearWrapSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        linearWrapSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        linearWrapSampler.MinLOD = 0.0f;
        linearWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;
        linearWrapSampler.ShaderRegister = 0;
        linearWrapSampler.RegisterSpace = 0;
        linearWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        modelRootSignature.AddRootCBV(0);
        modelRootSignature.AddSRVTable(3,0,D3D12_SHADER_VISIBILITY_PIXEL);
        modelRootSignature.AddStaticSampler(linearWrapSampler);
        if(!modelRootSignature.CreateRootSignature(modelDevice->GetDeviceComPtr()))
        {
            LogLine("CreatePipeline failed: root signature");
            return false;
        }
        LogLine("CreatePipeline root signature ok");

        D3D12_INPUT_ELEMENT_DESC inputElements[10]{};
        inputElements[0] = {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        inputElements[1] = {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        inputElements[2] = {"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        inputElements[3] = {"BITANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,36,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        inputElements[4] = {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        inputElements[5] = {"INSTANCEWORLD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1};
        inputElements[6] = {"INSTANCEWORLD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1};
        inputElements[7] = {"INSTANCEWORLD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1};
        inputElements[8] = {"INSTANCEWORLD",3,DXGI_FORMAT_R32G32B32A32_FLOAT,1,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1};
        inputElements[9] = {"INSTANCEINFO",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,64,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1};
        D3D12_INPUT_LAYOUT_DESC inputLayout{inputElements,10};

        GraphicsPipelineDesc psoDesc{};
        psoDesc.rootSignature = modelRootSignature.GetRootSignatureComPtr();
        psoDesc.vs = modelShader.GetVsShaderByteCode();
        psoDesc.ps = modelShader.GetPsShaderByteCode();
        psoDesc.inputLayout = inputLayout;
        psoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.rasterizer.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.rtvFormats[0] = modelSwapchain->GetFormat();
        psoDesc.numRenderTargets = 1;
        psoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        if(!modelPipelineState.CreateGraphicsPipelineState(modelDevice->GetDeviceComPtr(),psoDesc))
        {
            LogLine("CreatePipeline failed: graphics pipeline state");
            return false;
        }

        LogLine("CreatePipeline pso ok");
        return true;
    }

    static bool LoadTreeModel(ComPtr<ID3D12GraphicsCommandList> commandListObj)
    {
        std::filesystem::path modelPath = ResolveFilePath("quiver_tree_02_1k.gltf/quiver_tree_02_1k.gltf");
        if(modelPath.empty())
        {
            std::print("quiver tree gltf not found\n");
            return false;
        }

        std::string modelPathString = modelPath.generic_string();
        treeModel.ModelInit(modelPathString,false);
        if(treeModel.modelMeshs.empty() || treeModel.textures_load.size() < 3)
        {
            return false;
        }

        if(!treeModel.LoadTextureResources(
            modelDevice->GetDeviceComPtr(),
            commandListObj,
            *modelSRVDescriptorHeap,
            treeTextureUploads))
        {
            return false;
        }

        treeGpuResource = CreateModelGpuResource();
        if(!CreateModelGpuResourceFromModel(
            *treeGpuResource,
            treeModel,
            modelDevice->GetDeviceComPtr().Get(),
            commandListObj.Get()))
        {
            return false;
        }

        return true;
    }

    static void UpdateSceneConstantBuffer()
    {
        SceneCB sceneData{};
        DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX viewProj = modelCamera->GetViewProjection();

        DirectX::XMStoreFloat4x4(&sceneData.world,world);
        DirectX::XMStoreFloat4x4(&sceneData.viewProj,viewProj);
        sceneData.cameraPos = modelCamera->GetPosition();
        sceneData.lightPos = DirectX::XMFLOAT3(0.0f,9.0f,-6.0f);
        sceneData.lightColor = DirectX::XMFLOAT3(85.0f,80.0f,70.0f);
        sceneData.exposure = 1.0f;
        modelSceneConstantBuffer.Update(&sceneData,sizeof(sceneData));
    }

    bool initialize(HWND hwnd)
    {
        LogLine("initialize begin");
        if(isInitialized)
        {
            LogLine("initialize skipped: already initialized");
            return true;
        }
        if(hwnd == nullptr)
        {
            LogLine("initialize failed: hwnd is null");
            return false;
        }

        modelHwnd = hwnd;
        modelDevice = std::make_shared<Device>();
        if(!modelDevice->Initialize())
        {
            LogLine("initialize failed: device");
            return false;
        }
        LogLine("initialize device ok");

        modelCommandQueue = std::make_shared<CommandQueue>();
        if(!modelCommandQueue->Initialize(modelDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT))
        {
            LogLine("initialize failed: command queue");
            return false;
        }
        LogLine("initialize command queue ok");

        modelCommandAllocator = std::make_shared<CommandAllocator>();
        if(!modelCommandAllocator->Initialize(modelDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT))
        {
            LogLine("initialize failed: command allocator");
            return false;
        }
        LogLine("initialize command allocator ok");

        modelCommandList = std::make_shared<CommandList>();
        if(!modelCommandList->Initialize(
            modelDevice->GetDeviceComPtr(),
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            modelCommandAllocator->GetCommandAllocatorComPtr()))
        {
            LogLine("initialize failed: command list");
            return false;
        }
        LogLine("initialize command list ok");

        modelRTVDescriptorHeap = std::make_shared<DescriptorHeap>();
        modelDSVDescriptorHeap = std::make_shared<DescriptorHeap>();
        modelSRVDescriptorHeap = std::make_shared<DescriptorHeap>();
        if(!modelRTVDescriptorHeap->CreateDescriptorHeap(
            modelDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            2))
        {
            LogLine("initialize failed: rtv heap");
            return false;
        }
        if(!modelDSVDescriptorHeap->CreateDescriptorHeap(
            modelDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            1))
        {
            LogLine("initialize failed: dsv heap");
            return false;
        }
        if(!modelSRVDescriptorHeap->CreateDescriptorHeap(
            modelDevice->GetDeviceComPtr(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            8))
        {
            LogLine("initialize failed: srv heap");
            return false;
        }
        LogLine("initialize descriptor heaps ok");

        std::vector<DescriptorHandle> swapChainHandles;
        auto swapChainHandle0 = modelRTVDescriptorHeap->GetFreeDescriptorHandle();
        auto swapChainHandle1 = modelRTVDescriptorHeap->GetFreeDescriptorHandle();
        if(!swapChainHandle0 || !swapChainHandle1)
        {
            LogLine("initialize failed: swapchain rtv handles");
            return false;
        }
        swapChainHandles.push_back(*swapChainHandle0);
        swapChainHandles.push_back(*swapChainHandle1);

        modelSwapchain = std::make_shared<SwapChain>();
        if(!modelSwapchain->Create(
            modelDevice->GetFactoryComPtr(),
            modelCommandQueue->GetCommandQueueComPtr(),
            modelDevice->GetDeviceComPtr(),
            swapChainHandles,
            2,
            modelHwnd,
            WINDOW_WIDTH,
            WINDOW_HEIGHT))
        {
            LogLine("initialize failed: swapchain");
            return false;
        }
        LogLine("initialize swapchain ok");

        modelCamera = std::make_shared<QuaternionCamera>();
        modelCamera->SetPerspective(DirectX::XM_PIDIV4,static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),0.1f,100.0f);
        modelCamera->SetPosition(DirectX::XMFLOAT3(0.0f,7.0f,-20.0f));
        modelCamera->SetOrientationCameraLookAt(DirectX::XMFLOAT3(0.0f,2.5f,0.0f));

        if(!modelCommandAllocator->Reset())
        {
            LogLine("initialize failed: reset command allocator");
            return false;
        }
        if(!modelCommandList->Reset(modelCommandAllocator->GetCommandAllocatorComPtr()))
        {
            LogLine("initialize failed: reset command list");
            return false;
        }
        auto commandListObj = modelCommandList->GetGraphicsComPtr();

        if(!CreateDepthTexture())
        {
            LogLine("initialize failed: depth texture");
            return false;
        }
        LogLine("initialize depth texture ok");
        if(!CreatePipeline())
        {
            LogLine("initialize failed: pipeline");
            return false;
        }
        LogLine("initialize pipeline ok");
        if(!modelSceneConstantBuffer.Init(modelDevice->GetDeviceComPtr(),sizeof(SceneCB)))
        {
            LogLine("initialize failed: scene constant buffer");
            return false;
        }
        LogLine("initialize scene constant buffer ok");
        if(!LoadTreeModel(commandListObj))
        {
            LogLine("initialize failed: tree model");
            return false;
        }
        LogLine("initialize tree model ok");
        if(!CreateTreeInstances(commandListObj))
        {
            LogLine("initialize failed: tree instances");
            return false;
        }
        LogLine("initialize tree instances ok");
        if(!CreatePlane(commandListObj))
        {
            LogLine("initialize failed: plane");
            return false;
        }
        LogLine("initialize plane ok");

        modelViewport.TopLeftX = 0.0f;
        modelViewport.TopLeftY = 0.0f;
        modelViewport.Width = static_cast<float>(WINDOW_WIDTH);
        modelViewport.Height = static_cast<float>(WINDOW_HEIGHT);
        modelViewport.MinDepth = 0.0f;
        modelViewport.MaxDepth = 1.0f;
        modelScissor.left = 0;
        modelScissor.top = 0;
        modelScissor.right = WINDOW_WIDTH;
        modelScissor.bottom = WINDOW_HEIGHT;

        if(FAILED(commandListObj->Close()))
        {
            LogLine("initialize failed: close command list");
            return false;
        }

        ID3D12CommandList* initCommandLists[] = {commandListObj.Get()};
        modelCommandQueue->Execute(initCommandLists,1);
        modelCommandQueue->Flush();
        ReleaseModelGpuResourceUploadResources(*treeGpuResource);
        treeTextureUploads.clear();
        treeInstanceUpload.Reset();
        planeVertexUpload.Reset();
        planeIndexUpload.Reset();
        planeInstanceUpload.Reset();

        isInitialized = true;
        LogLine("initialize ok");
        return true;
    }

    void InputCallBack()
    {
        if(!isInitialized || !modelCamera || modelHwnd == nullptr)
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
            modelCamera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,moveSpeed));
        }
        if((GetAsyncKeyState('S') & 0x8000) != 0)
        {
            modelCamera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,-moveSpeed));
        }
        if((GetAsyncKeyState('A') & 0x8000) != 0)
        {
            modelCamera->MoveLocal(DirectX::XMFLOAT3(-moveSpeed,0.0f,0.0f));
        }
        if((GetAsyncKeyState('D') & 0x8000) != 0)
        {
            modelCamera->MoveLocal(DirectX::XMFLOAT3(moveSpeed,0.0f,0.0f));
        }
        if((GetAsyncKeyState('Q') & 0x8000) != 0)
        {
            modelCamera->MoveWorld(DirectX::XMFLOAT3(0.0f,-moveSpeed,0.0f));
        }
        if((GetAsyncKeyState('E') & 0x8000) != 0)
        {
            modelCamera->MoveWorld(DirectX::XMFLOAT3(0.0f,moveSpeed,0.0f));
        }

        POINT currentMousePosition{};
        if(GetCursorPos(&currentMousePosition))
        {
            ScreenToClient(modelHwnd,&currentMousePosition);
            if((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
            {
                if(hasLastMousePosition)
                {
                    float yawRadians = static_cast<float>(currentMousePosition.x - lastMousePosition.x) * 0.005f;
                    float pitchRadians = static_cast<float>(currentMousePosition.y - lastMousePosition.y) * 0.005f;
                    modelCamera->AddYawPitch(yawRadians,pitchRadians);
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
        if(!isInitialized || !modelCommandAllocator || !modelCommandList || !modelSwapchain || !modelDepthTexture ||
            !treeInstanceBuffer || !planeVertexBuffer || !planeIndexBuffer || !planeInstanceBuffer)
        {
            LogLine("render skipped: not initialized");
            return;
        }

        if(!modelCommandAllocator->Reset())
        {
            LogLine("render failed: reset command allocator");
            return;
        }
        if(!modelCommandList->Reset(modelCommandAllocator->GetCommandAllocatorComPtr()))
        {
            LogLine("render failed: reset command list");
            return;
        }

        auto commandListObj = modelCommandList->GetGraphicsComPtr();
        if(commandListObj == nullptr)
        {
            LogLine("render failed: command list is null");
            return;
        }

        UpdateSceneConstantBuffer();

        auto backBufferResource = modelSwapchain->GetCurrentBackBufferResource();
        auto backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            backBufferResource.Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandListObj->ResourceBarrier(1,&backBufferTransition);

        commandListObj->RSSetViewports(1,&modelViewport);
        commandListObj->RSSetScissorRects(1,&modelScissor);

        const FLOAT clearColor[4] = {0.12f,0.16f,0.18f,1.0f};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = modelSwapchain->GetCurrentRTVCPUHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = modelDepthTexture->GetDsvCpuHandle();
        commandListObj->ClearRenderTargetView(rtv,clearColor,0,nullptr);
        commandListObj->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);
        commandListObj->OMSetRenderTargets(1,&rtv,FALSE,&dsv);

        ID3D12DescriptorHeap* descriptorHeaps[] = {modelSRVDescriptorHeap->GetDescriptorHeapComPtr().Get()};
        commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
        commandListObj->SetGraphicsRootSignature(modelRootSignature.GetRootSignatureComPtr().Get());
        commandListObj->SetPipelineState(modelPipelineState.GetPipelineStateComPtr().Get());
        commandListObj->SetGraphicsRootConstantBufferView(0,modelSceneConstantBuffer.GPUAddress());
        commandListObj->SetGraphicsRootDescriptorTable(1,treeModel.textures[0]->GetSrvGpuHandle());

        D3D12_VERTEX_BUFFER_VIEW planeViews[] = {planeVertexView,planeInstanceView};
        commandListObj->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandListObj->IASetVertexBuffers(0,2,planeViews);
        commandListObj->IASetIndexBuffer(&planeIndexView);
        commandListObj->DrawIndexedInstanced(planeIndexCount,1,0,0,0);

        commandListObj->IASetVertexBuffers(1,1,&treeInstanceView);
        DrawModelGpuResourceInstanced(*treeGpuResource,commandListObj.Get(),TREE_INSTANCE_COUNT);

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
        modelCommandQueue->Execute(commandLists,1);
        modelSwapchain->Present();
        modelCommandQueue->Flush();
    }

    void Shutdown()
    {
        LogLine("shutdown begin");
        if(modelCommandQueue)
        {
            modelCommandQueue->Flush();
        }

        if(treeGpuResource)
        {
            treeGpuResource.reset();
        }
        treeTextureUploads.clear();
        treeInstanceBuffer.Reset();
        treeInstanceUpload.Reset();
        treeInstanceView = {};
        planeVertexBuffer.Reset();
        planeVertexUpload.Reset();
        planeIndexBuffer.Reset();
        planeIndexUpload.Reset();
        planeInstanceBuffer.Reset();
        planeInstanceUpload.Reset();
        planeVertexView = {};
        planeIndexView = {};
        planeInstanceView = {};
        planeIndexCount = 0;
        treeModel.textures.clear();
        treeModel.textures_load.clear();
        treeModel.modelMeshs.clear();
        treeModel.modelNodes.clear();
        treeModel.rootNode.reset();
        modelDepthTexture.reset();
        modelSwapchain.reset();
        modelCamera.reset();
        modelSRVDescriptorHeap.reset();
        modelDSVDescriptorHeap.reset();
        modelRTVDescriptorHeap.reset();
        modelCommandList.reset();
        modelCommandAllocator.reset();
        modelCommandQueue.reset();
        modelDevice.reset();
        modelHwnd = nullptr;
        hasLastMousePosition = false;
        isInitialized = false;
        LogLine("shutdown end");
    }
}

bool InitializeTreeModelDemo(void* hwnd)
{
    return TreeModelDemo::initialize(static_cast<HWND>(hwnd));
}

void InputTreeModelDemo()
{
    TreeModelDemo::InputCallBack();
}

void RenderTreeModelDemo()
{
    TreeModelDemo::RenderCallBack();
}

void ShutdownTreeModelDemo()
{
    TreeModelDemo::Shutdown();
}
