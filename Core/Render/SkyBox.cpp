#include "Render/SkyBox.hpp"

#include "Resource/LoadImage.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    std::filesystem::path GetModuleDirectory()
    {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(nullptr,modulePath,MAX_PATH);
        if(length == 0 || length >= MAX_PATH)
        {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(modulePath).parent_path();
    }

    void LogLine(const std::string& text)
    {
        std::ofstream logFile(GetModuleDirectory() / "skybox_resource.log",std::ios::app);
        logFile << text << "\n";
    }

    bool CreateBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        const void* data,
        UINT64 byteSize,
        D3D12_RESOURCE_STATES finalState,
        ComPtr<ID3D12Resource1>& defaultResource,
        ComPtr<ID3D12Resource1>& uploadResource)
    {
        if(!device || !commandList || data == nullptr || byteSize == 0)
        {
            return false;
        }

        auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

        if(FAILED(device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(defaultResource.ReleaseAndGetAddressOf()))))
        {
            return false;
        }

        if(FAILED(device->CreateCommittedResource(
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

        void* mapped = nullptr;
        D3D12_RANGE readRange{0,0};
        if(FAILED(uploadResource->Map(0,&readRange,&mapped)))
        {
            defaultResource.Reset();
            uploadResource.Reset();
            return false;
        }

        std::memcpy(mapped,data,static_cast<size_t>(byteSize));
        uploadResource->Unmap(0,nullptr);

        commandList->CopyBufferRegion(defaultResource.Get(),0,uploadResource.Get(),0,byteSize);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            defaultResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            finalState);
        commandList->ResourceBarrier(1,&barrier);
        return true;
    }

    bool LoadCubeImages(
        const std::array<const char*,6>& facePaths,
        std::array<Imgae,6>& images)
    {
        for(size_t i = 0;i < facePaths.size();i++)
        {
            if(facePaths[i] == nullptr || facePaths[i][0] == '\0')
            {
                LogLine("LoadCubeImages failed: empty face path");
                return false;
            }

            if(!LoadTexture(facePaths[i],images[i],false))
            {
                LogLine(std::string("LoadCubeImages failed: ") + facePaths[i]);
                std::print("Load skybox face failed: {}\n",facePaths[i]);
                return false;
            }

            if(images[i].isHdr || images[i].width <= 0 || images[i].height <= 0)
            {
                LogLine(std::string("LoadCubeImages failed: invalid image ") + facePaths[i]);
                return false;
            }
        }

        const int width = images[0].width;
        const int height = images[0].height;
        for(size_t i = 1;i < images.size();i++)
        {
            if(images[i].width != width || images[i].height != height)
            {
                LogLine("LoadCubeImages failed: size mismatch");
                std::print("Skybox faces must have same size.\n");
                return false;
            }
        }

        return true;
    }
}

SkyBox::SkyBox() = default;

SkyBox::~SkyBox() = default;

bool SkyBox::Initialize(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    DescriptorHeap* srvDescriptorHeap,
    const char* positiveXPath,
    const char* negativeXPath,
    const char* positiveYPath,
    const char* negativeYPath,
    const char* positiveZPath,
    const char* negativeZPath)
{
    if(!device || !commandList || !srvDescriptorHeap)
    {
        LogLine("InitializeSkyBox failed: null parameter");
        return false;
    }

    if(!CreateCubeTexture(
        device,
        commandList,
        *srvDescriptorHeap,
        positiveXPath,
        negativeXPath,
        positiveYPath,
        negativeYPath,
        positiveZPath,
        negativeZPath))
    {
        LogLine("InitializeSkyBox failed: cube texture");
        return false;
    }

    if(!CreateCubeGeometry(device,commandList))
    {
        LogLine("InitializeSkyBox failed: cube geometry");
        return false;
    }

    initialized = true;
    LogLine("InitializeSkyBox ok");
    return true;
}

void SkyBox::ReleaseUploadResources()
{
    cubeTextureUpload.Reset();
    vertexUpload.Reset();
    indexUpload.Reset();
}

D3D12_GPU_DESCRIPTOR_HANDLE SkyBox::GetSrvGpuHandle() const
{
    return srvHandle ? srvHandle->gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

void SkyBox::Draw(ID3D12GraphicsCommandList* commandList) const
{
    if(!commandList || !initialized)
    {
        return;
    }

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0,1,&vertexView);
    commandList->IASetIndexBuffer(&indexView);
    commandList->DrawIndexedInstanced(indexCount,1,0,0,0);
}

bool SkyBox::CreateCubeGeometry(ID3D12Device* device,ID3D12GraphicsCommandList* commandList)
{
    constexpr float size = 500.0f;
    const std::array<SkyBoxVertex,8> vertices =
    {
        SkyBoxVertex{DirectX::XMFLOAT3(-size,-size,-size)},
        SkyBoxVertex{DirectX::XMFLOAT3(-size, size,-size)},
        SkyBoxVertex{DirectX::XMFLOAT3( size, size,-size)},
        SkyBoxVertex{DirectX::XMFLOAT3( size,-size,-size)},
        SkyBoxVertex{DirectX::XMFLOAT3(-size,-size, size)},
        SkyBoxVertex{DirectX::XMFLOAT3(-size, size, size)},
        SkyBoxVertex{DirectX::XMFLOAT3( size, size, size)},
        SkyBoxVertex{DirectX::XMFLOAT3( size,-size, size)}
    };

    const std::array<UINT,36> indices =
    {
        0,1,2, 0,2,3,
        4,6,5, 4,7,6,
        4,5,1, 4,1,0,
        3,2,6, 3,6,7,
        1,5,6, 1,6,2,
        4,0,3, 4,3,7
    };

    const UINT64 vertexBytes = static_cast<UINT64>(vertices.size() * sizeof(SkyBoxVertex));
    const UINT64 indexBytes = static_cast<UINT64>(indices.size() * sizeof(UINT));

    if(!CreateBuffer(
        device,
        commandList,
        vertices.data(),
        vertexBytes,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        vertexBuffer,
        vertexUpload))
    {
        return false;
    }

    if(!CreateBuffer(
        device,
        commandList,
        indices.data(),
        indexBytes,
        D3D12_RESOURCE_STATE_INDEX_BUFFER,
        indexBuffer,
        indexUpload))
    {
        return false;
    }

    vertexView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexView.StrideInBytes = sizeof(SkyBoxVertex);
    vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
    indexView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexView.Format = DXGI_FORMAT_R32_UINT;
    indexView.SizeInBytes = static_cast<UINT>(indexBytes);
    indexCount = static_cast<UINT>(indices.size());
    return true;
}

bool SkyBox::CreateCubeTexture(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    DescriptorHeap& srvDescriptorHeap,
    const char* positiveXPath,
    const char* negativeXPath,
    const char* positiveYPath,
    const char* negativeYPath,
    const char* positiveZPath,
    const char* negativeZPath)
{
    const std::array<const char*,6> facePaths =
    {
        positiveXPath,
        negativeXPath,
        positiveYPath,
        negativeYPath,
        positiveZPath,
        negativeZPath
    };

    std::array<Imgae,6> images{};
    if(!LoadCubeImages(facePaths,images))
    {
        LogLine("CreateCubeTexture failed: load images");
        return false;
    }

    const UINT width = static_cast<UINT>(images[0].width);
    const UINT height = static_cast<UINT>(images[0].height);
    const DXGI_FORMAT textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 6;
    textureDesc.MipLevels = 1;
    textureDesc.Format = textureFormat;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if(FAILED(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(cubeTexture.ReleaseAndGetAddressOf()))))
    {
        LogLine("CreateCubeTexture failed: create texture");
        return false;
    }

    std::array<D3D12_SUBRESOURCE_DATA,6> subresources{};
    for(size_t i = 0;i < images.size();i++)
    {
        subresources[i].pData = images[i].data.data();
        subresources[i].RowPitch = static_cast<LONG_PTR>(width * 4);
        subresources[i].SlicePitch = static_cast<LONG_PTR>(width * height * 4);
    }

    const UINT64 uploadSize = GetRequiredIntermediateSize(
        cubeTexture.Get(),
        0,
        static_cast<UINT>(subresources.size()));
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    if(FAILED(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(cubeTextureUpload.ReleaseAndGetAddressOf()))))
    {
        cubeTexture.Reset();
        LogLine("CreateCubeTexture failed: create upload");
        return false;
    }

    UpdateSubresources(
        commandList,
        cubeTexture.Get(),
        cubeTextureUpload.Get(),
        0,
        0,
        static_cast<UINT>(subresources.size()),
        subresources.data());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        cubeTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1,&barrier);

    srvHandle = srvDescriptorHeap.GetFreeDescriptorHandle();
    if(!srvHandle)
    {
        LogLine("CreateCubeTexture failed: srv handle");
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        cubeTexture.Get(),
        &srvDesc,
        srvHandle->cpuHandle);

    return true;
}
