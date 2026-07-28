#pragma once

#include "global.h"
#include "RootSignature/RootSignature.hpp"
#include "PipelineState/PipelineState.hpp"
#include "Shader/Shader.hpp"

using Microsoft::WRL::ComPtr;

struct MipGenCB
{
    UINT SrcWidth = 0;
    UINT SrcHeight = 0;
    UINT DstWidth = 0;
    UINT DstHeight = 0;
};

class Texture
{
private:
    ComPtr<ID3D12Resource1> resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle{};
    UINT width = 0;
    UINT height = 0;
    UINT arraySize = 1;
    UINT mipLevels = 0;
    DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT uavFormat = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;

public:
    Texture() = default;

    Texture(
        ComPtr<ID3D12Resource1> textureResource,
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle,
        UINT width,
        UINT height,
        UINT arraySize,
        UINT mipLevels,
        DXGI_FORMAT resourceFormat,
        DXGI_FORMAT srvFormat,
        DXGI_FORMAT uavFormat,
        D3D12_RESOURCE_STATES currentState)
        : srvCpuHandle(srvCpuHandle),
          srvGpuHandle(srvGpuHandle),
          uavCpuHandle(uavCpuHandle),
          uavGpuHandle(uavGpuHandle),
          width(width),
          height(height),
          arraySize(arraySize),
          mipLevels(mipLevels),
          resourceFormat(resourceFormat),
          srvFormat(srvFormat),
          uavFormat(uavFormat),
          currentState(currentState)
    {
        resource = std::move(textureResource);
    }

    ID3D12Resource1* GetResource() const
    {
        return resource.Get();
    }

    const ComPtr<ID3D12Resource1> GetResourceComPtr() const
    {
        return resource;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle() const
    {
        return srvCpuHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const
    {
        return srvGpuHandle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetUavCpuHandle() const
    {
        return uavCpuHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetUavGpuHandle() const
    {
        return uavGpuHandle;
    }

    UINT GetWidth() const
    {
        return width;
    }

    UINT GetHeight() const
    {
        return height;
    }

    UINT GetArraySize() const
    {
        return arraySize;
    }

    UINT GetMipLevels() const
    {
        return mipLevels;
    }

    DXGI_FORMAT GetResourceFormat() const
    {
        return resourceFormat;
    }

    DXGI_FORMAT GetSrvFormat() const
    {
        return srvFormat;
    }

    DXGI_FORMAT GetUavFormat() const
    {
        return uavFormat;
    }

    D3D12_RESOURCE_STATES GetCurrentState() const
    {
        return currentState;
    }

    void SetCurrentState(D3D12_RESOURCE_STATES state)
    {
        currentState = state;
    }

    bool HasSrv() const
    {
        return srvCpuHandle.ptr != 0;
    }

    bool HasUav() const
    {
        return uavCpuHandle.ptr != 0;
    }

    bool IsValid() const
    {
        return resource != nullptr;
    }

    void Reset()
    {
        resource.Reset();
        srvCpuHandle = {};
        srvGpuHandle = {};
        uavCpuHandle = {};
        uavGpuHandle = {};
        width = 0;
        height = 0;
        arraySize = 1;
        mipLevels = 0;
        resourceFormat = DXGI_FORMAT_UNKNOWN;
        srvFormat = DXGI_FORMAT_UNKNOWN;
        uavFormat = DXGI_FORMAT_UNKNOWN;
        currentState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct TextureCreateResult
{
    Texture texture;
    ComPtr<ID3D12Resource1> uploadResource = nullptr;

    void Reset()
    {
        texture.Reset();
        uploadResource.Reset();
    }

    bool HasPendingUpload() const
    {
        return uploadResource != nullptr;
    }
};

struct Texture2DCreateInfo
{
    D3D12_RESOURCE_DESC resourceDesc{};
    D3D12_SUBRESOURCE_DATA initialSubresource{};
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    DXGI_FORMAT uavFormat = DXGI_FORMAT_UNKNOWN;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle{};
    ID3D12DescriptorHeap* shaderVisibleHeap = nullptr;
    const D3D12_CPU_DESCRIPTOR_HANDLE* mipUavCpuHandles = nullptr;
    const D3D12_GPU_DESCRIPTOR_HANDLE* mipUavGpuHandles = nullptr;
    UINT mipUavDescriptorCount = 0;
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_COMMON;
    bool generateMipChain = false;
};

class TextureGenerator
{
private:
    RootSignature mipGeneratorRootSignature;
    PipelineState mipGeneratorPipelineState;
    Shader mipGeneratorShader;
    bool mipGeneratorInitialized = false;
    std::wstring mipGeneratorShaderPath = L"D:/C++proj/Engine/shader/texture2DMipmapGenerator.hlsl";

    static UINT CalcMipDim(UINT base, UINT level)
    {
        UINT value = base >> level;
        return value == 0 ? 1 : value;
    }

    bool EnsureMipGenerator(ID3D12Device* device)
    {
        if (mipGeneratorInitialized)
        {
            return true;
        }
        return Initialize(device, mipGeneratorShaderPath);
    }

public:
    bool Initialize(
        ComPtr<ID3D12Device> device,
        const std::wstring& shaderFilePath = L"D:/C++proj/Engine/shader/texture2DMipmapGenerator.hlsl")
    {
        if (device == nullptr)
        {
            std::print("TextureGenerator::Initialize failed: device is null.\n");
            return false;
        }

        mipGeneratorShaderPath = shaderFilePath;
        mipGeneratorRootSignature.Reset();
        mipGeneratorPipelineState.Reset();

        mipGeneratorRootSignature.AddRootConstants(4, 0);
        mipGeneratorRootSignature.AddUAVTable(1, 0);
        mipGeneratorRootSignature.AddUAVTable(1, 1);
        if (!mipGeneratorRootSignature.CreateRootSignature(device, D3D12_ROOT_SIGNATURE_FLAG_NONE))
        {
            return false;
        }

        if (!mipGeneratorShader.CompileComputeFromFile(
                shaderFilePath,
                Shader::StageCompileDesc("CSMain", "cs_5_1", "CS")))
        {
            return false;
        }

        ComputePipelineDesc desc{};
        desc.rootSignature = mipGeneratorRootSignature.GetRootSignatureComPtr();
        desc.cs = mipGeneratorShader.GetCsShaderByteCode();
        mipGeneratorInitialized = mipGeneratorPipelineState.CreateComputePipelineState(device, desc);
        return mipGeneratorInitialized;
    }

    bool CreateTexture2D(
        ComPtr<ID3D12Device> device,
        ComPtr<ID3D12GraphicsCommandList> commandList,
        const Texture2DCreateInfo& createInfo,
        TextureCreateResult& outResult)
    {
        outResult.Reset();

        if (device == nullptr || commandList == nullptr)
        {
            std::print("TextureGenerator::CreateTexture2D failed: device or commandList is null.\n");
            return false;
        }

        D3D12_RESOURCE_DESC desc = createInfo.resourceDesc;
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.Width == 0 ||
            desc.Height == 0 ||
            desc.Format == DXGI_FORMAT_UNKNOWN ||
            desc.SampleDesc.Count == 0 ||
            desc.MipLevels == 0)
        {
            std::print("TextureGenerator::CreateTexture2D failed: invalid texture resource desc.\n");
            return false;
        }

        const bool hasInitialData = createInfo.initialSubresource.pData != nullptr;
        if (createInfo.generateMipChain)
        {
            if (!hasInitialData)
            {
                std::print("TextureGenerator::CreateTexture2D failed: mip generation requires initial data.\n");
                return false;
            }
            if (desc.MipLevels <= 1)
            {
                std::print("TextureGenerator::CreateTexture2D failed: mip generation requires explicit mip levels > 1.\n");
                return false;
            }
            if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
            {
                std::print("TextureGenerator::CreateTexture2D failed: mip generation requires D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS.\n");
                return false;
            }
            if (createInfo.uavFormat == DXGI_FORMAT_UNKNOWN)
            {
                std::print("TextureGenerator::CreateTexture2D failed: mip generation requires explicit UAV format.\n");
                return false;
            }
        }

        D3D12_RESOURCE_STATES currentState =
            hasInitialData ? D3D12_RESOURCE_STATE_COPY_DEST : createInfo.finalState;

        ComPtr<ID3D12Resource1> textureResource;
        CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(device->CreateCommittedResource(
                &defaultHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                currentState,
                nullptr,
                IID_PPV_ARGS(textureResource.ReleaseAndGetAddressOf()))))
        {
            std::print("TextureGenerator::CreateTexture2D failed: CreateCommittedResource(default) failed.\n");
            return false;
        }

        ComPtr<ID3D12Resource1> uploadResource;
        if (hasInitialData)
        {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            UINT numRows = 0;
            UINT64 rowSizeInBytes = 0;
            UINT64 uploadSize = 0;
            device->GetCopyableFootprints(
                &desc,
                0,
                1,
                0,
                &footprint,
                &numRows,
                &rowSizeInBytes,
                &uploadSize);

            D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
            if (FAILED(device->CreateCommittedResource(
                    &uploadHeapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(uploadResource.ReleaseAndGetAddressOf()))))
            {
                std::print("TextureGenerator::CreateTexture2D failed: CreateCommittedResource(upload) failed.\n");
                return false;
            }

            if (createInfo.initialSubresource.RowPitch <= 0)
            {
                std::print("TextureGenerator::CreateTexture2D failed: initialSubresource.RowPitch must be set explicitly.\n");
                return false;
            }

            const UINT64 sourceRowPitch = static_cast<UINT64>(createInfo.initialSubresource.RowPitch);
            if (sourceRowPitch < rowSizeInBytes)
            {
                std::print("TextureGenerator::CreateTexture2D failed: source row pitch is too small.\n");
                return false;
            }

            void* mappedData = nullptr;
            D3D12_RANGE readRange{0, 0};
            if (FAILED(uploadResource->Map(0, &readRange, &mappedData)))
            {
                std::print("TextureGenerator::CreateTexture2D failed: Map(upload) failed.\n");
                return false;
            }

            unsigned char* dstBase = static_cast<unsigned char*>(mappedData) + footprint.Offset;
            const unsigned char* srcBase =
                static_cast<const unsigned char*>(createInfo.initialSubresource.pData);
            for (UINT row = 0; row < numRows; ++row)
            {
                std::memcpy(
                    dstBase + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                    srcBase + static_cast<size_t>(row) * sourceRowPitch,
                    static_cast<size_t>(rowSizeInBytes));
            }
            uploadResource->Unmap(0, nullptr);

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = textureResource.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = uploadResource.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;

            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        const UINT mipLevels = desc.MipLevels;
        if (createInfo.generateMipChain && mipLevels > 1)
        {
            if (createInfo.shaderVisibleHeap == nullptr ||
                createInfo.mipUavCpuHandles == nullptr ||
                createInfo.mipUavGpuHandles == nullptr ||
                createInfo.mipUavDescriptorCount < mipLevels)
            {
                std::print("TextureGenerator::CreateTexture2D failed: mip UAV handles or descriptor heap are invalid.\n");
                return false;
            }

            if (!EnsureMipGenerator(device.Get()))
            {
                return false;
            }

            if (currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    textureResource.Get(),
                    currentState,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                commandList->ResourceBarrier(1, &barrier);
                currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            for (UINT mip = 0; mip < mipLevels; ++mip)
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = createInfo.uavFormat;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = mip;
                uavDesc.Texture2D.PlaneSlice = 0;

                device->CreateUnorderedAccessView(
                    textureResource.Get(),
                    nullptr,
                    &uavDesc,
                    createInfo.mipUavCpuHandles[mip]);
            }

            ID3D12DescriptorHeap* heaps[] = { createInfo.shaderVisibleHeap };
            commandList->SetPipelineState(mipGeneratorPipelineState.GetPipelineStateComPtr().Get());
            commandList->SetComputeRootSignature(mipGeneratorRootSignature.GetRootSignatureComPtr().Get());
            commandList->SetDescriptorHeaps(1, heaps);

            for (UINT mip = 1; mip < mipLevels; ++mip)
            {
                MipGenCB cb{};
                cb.SrcWidth = CalcMipDim(static_cast<UINT>(desc.Width), mip - 1);
                cb.SrcHeight = CalcMipDim(desc.Height, mip - 1);
                cb.DstWidth = CalcMipDim(static_cast<UINT>(desc.Width), mip);
                cb.DstHeight = CalcMipDim(desc.Height, mip);

                commandList->SetComputeRoot32BitConstants(0, 4, &cb, 0);
                commandList->SetComputeRootDescriptorTable(1, createInfo.mipUavGpuHandles[mip - 1]);
                commandList->SetComputeRootDescriptorTable(2, createInfo.mipUavGpuHandles[mip]);

                const UINT groupsX = (cb.DstWidth + 7) / 8;
                const UINT groupsY = (cb.DstHeight + 7) / 8;
                commandList->Dispatch(groupsX, groupsY, 1);

                auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(textureResource.Get());
                commandList->ResourceBarrier(1, &uavBarrier);
            }
        }

        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = createInfo.srvCpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = createInfo.srvGpuHandle;
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (srvCpuHandle.ptr != 0)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = createInfo.srvDesc;
            if (srvDesc.ViewDimension == D3D12_SRV_DIMENSION_UNKNOWN ||
                srvDesc.Format == DXGI_FORMAT_UNKNOWN)
            {
                std::print("TextureGenerator::CreateTexture2D failed: srvDesc must be set explicitly.\n");
                return false;
            }

            device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCpuHandle);
            srvFormat = srvDesc.Format;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle = createInfo.uavCpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle = createInfo.uavGpuHandle;
        DXGI_FORMAT textureUavFormat = DXGI_FORMAT_UNKNOWN;
        if (uavCpuHandle.ptr != 0)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = createInfo.uavDesc;
            if (uavDesc.ViewDimension == D3D12_UAV_DIMENSION_UNKNOWN ||
                uavDesc.Format == DXGI_FORMAT_UNKNOWN)
            {
                std::print("TextureGenerator::CreateTexture2D failed: uavDesc must be set explicitly.\n");
                return false;
            }

            device->CreateUnorderedAccessView(textureResource.Get(), nullptr, &uavDesc, uavCpuHandle);
            textureUavFormat = uavDesc.Format;
        }

        if (currentState != createInfo.finalState)
        {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                textureResource.Get(),
                currentState,
                createInfo.finalState);
            commandList->ResourceBarrier(1, &barrier);
            currentState = createInfo.finalState;
        }

        outResult.texture = Texture(
            textureResource,
            srvCpuHandle,
            srvGpuHandle,
            uavCpuHandle,
            uavGpuHandle,
            static_cast<UINT>(desc.Width),
            desc.Height,
            desc.DepthOrArraySize,
            mipLevels,
            desc.Format,
            srvFormat,
            textureUavFormat,
            currentState);
        outResult.uploadResource = std::move(uploadResource);
        return true;
    }

    bool CreateTexture2D(
        ComPtr<ID3D12Device> device,
        ComPtr<ID3D12GraphicsCommandList> commandList,
        const Texture2DCreateInfo& createInfo,
        Texture& outTexture,
        ComPtr<ID3D12Resource1>& outUploadResource)
    {
        TextureCreateResult result;
        if (!CreateTexture2D(device, commandList, createInfo, result))
        {
            outTexture.Reset();
            outUploadResource.Reset();
            return false;
        }

        outTexture = std::move(result.texture);
        outUploadResource = std::move(result.uploadResource);
        return true;
    }

};

class DepthTexture
{
private:
    ComPtr<ID3D12Resource1> resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvCpuHandle{};
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sliceDsvCpuHandles;
    UINT width = 0;
    UINT height = 0;
    UINT arraySize = 1;
    UINT mipLevels = 1;
    DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

public:
    DepthTexture() = default;

    DepthTexture(
        ComPtr<ID3D12Resource1> depthTextureResource,
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvCpuHandle,
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sliceDsvCpuHandles,
        UINT width,
        UINT height,
        UINT arraySize,
        UINT mipLevels,
        DXGI_FORMAT resourceFormat,
        DXGI_FORMAT dsvFormat,
        DXGI_FORMAT srvFormat,
        D3D12_RESOURCE_STATES currentState)
        : srvCpuHandle(srvCpuHandle),
          srvGpuHandle(srvGpuHandle),
          dsvCpuHandle(dsvCpuHandle),
          sliceDsvCpuHandles(std::move(sliceDsvCpuHandles)),
          width(width),
          height(height),
          arraySize(arraySize),
          mipLevels(mipLevels),
          resourceFormat(resourceFormat),
          dsvFormat(dsvFormat),
          srvFormat(srvFormat),
          currentState(currentState)
    {
        resource = std::move(depthTextureResource);
    }

    ID3D12Resource1* GetResource() const
    {
        return resource.Get();
    }

    const ComPtr<ID3D12Resource1> GetResourceComPtr() const
    {
        return resource;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle() const
    {
        return srvCpuHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const
    {
        return srvGpuHandle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvCpuHandle() const
    {
        return dsvCpuHandle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSliceDsvCpuHandle(UINT sliceIndex) const
    {
        if (sliceDsvCpuHandles.empty())
        {
            return sliceIndex == 0 ? dsvCpuHandle : D3D12_CPU_DESCRIPTOR_HANDLE{};
        }

        if (sliceIndex >= sliceDsvCpuHandles.size())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE{};
        }
        return sliceDsvCpuHandles[sliceIndex];
    }

    const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& GetSliceDsvCpuHandles() const
    {
        return sliceDsvCpuHandles;
    }

    UINT GetWidth() const
    {
        return width;
    }

    UINT GetHeight() const
    {
        return height;
    }

    UINT GetArraySize() const
    {
        return arraySize;
    }

    UINT GetMipLevels() const
    {
        return mipLevels;
    }

    DXGI_FORMAT GetResourceFormat() const
    {
        return resourceFormat;
    }

    DXGI_FORMAT GetDsvFormat() const
    {
        return dsvFormat;
    }

    DXGI_FORMAT GetSrvFormat() const
    {
        return srvFormat;
    }

    D3D12_RESOURCE_STATES GetCurrentState() const
    {
        return currentState;
    }

    void SetCurrentState(D3D12_RESOURCE_STATES state)
    {
        currentState = state;
    }

    bool HasSrv() const
    {
        return srvCpuHandle.ptr != 0;
    }

    bool IsArrayTexture() const
    {
        return arraySize > 1;
    }

    bool IsValid() const
    {
        return resource != nullptr;
    }

    void Reset()
    {
        resource.Reset();
        srvCpuHandle = {};
        srvGpuHandle = {};
        dsvCpuHandle = {};
        sliceDsvCpuHandles.clear();
        width = 0;
        height = 0;
        arraySize = 1;
        mipLevels = 1;
        resourceFormat = DXGI_FORMAT_UNKNOWN;
        dsvFormat = DXGI_FORMAT_UNKNOWN;
        srvFormat = DXGI_FORMAT_UNKNOWN;
        currentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
};

struct DepthTextureCreateInfo
{
    D3D12_RESOURCE_DESC resourceDesc{};
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    bool hasClearValue = false;
    D3D12_CLEAR_VALUE clearValue{};
    D3D12_DEPTH_STENCIL_VIEW_DESC mainDsvDesc{};
    D3D12_CPU_DESCRIPTOR_HANDLE mainDsvCpuHandle{};
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
    std::vector<D3D12_DEPTH_STENCIL_VIEW_DESC> sliceDsvDescs;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sliceDsvCpuHandles;
};

class DepthTextureGenerator
{
public:
    static bool CreateDepthTexture(
        ComPtr<ID3D12Device> device,
        const DepthTextureCreateInfo& createInfo,
        std::shared_ptr<DepthTexture>& outTexture)
    {
        outTexture = nullptr;

        if (device == nullptr)
        {
            std::print("DepthTextureGenerator::CreateDepthTexture failed: device is null.\n");
            return false;
        }

        const D3D12_RESOURCE_DESC desc = createInfo.resourceDesc;
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.Width == 0 ||
            desc.Height == 0 ||
            desc.Format == DXGI_FORMAT_UNKNOWN ||
            desc.SampleDesc.Count == 0 ||
            desc.MipLevels == 0)
        {
            std::print("DepthTextureGenerator::CreateDepthTexture failed: invalid depth resource desc.\n");
            return false;
        }

        ComPtr<ID3D12Resource1> depthResource;
        CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(device->CreateCommittedResource(
                &defaultHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                createInfo.initialState,
                createInfo.hasClearValue ? &createInfo.clearValue : nullptr,
                IID_PPV_ARGS(depthResource.ReleaseAndGetAddressOf()))))
        {
            std::print("DepthTextureGenerator::CreateDepthTexture failed: CreateCommittedResource failed.\n");
            return false;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC mainDsvDesc = createInfo.mainDsvDesc;
        if (createInfo.mainDsvCpuHandle.ptr != 0)
        {
            if (mainDsvDesc.ViewDimension == D3D12_DSV_DIMENSION_UNKNOWN ||
                mainDsvDesc.Format == DXGI_FORMAT_UNKNOWN)
            {
                std::print("DepthTextureGenerator::CreateDepthTexture failed: mainDsvDesc must be set explicitly.\n");
                return false;
            }

            device->CreateDepthStencilView(depthResource.Get(), &mainDsvDesc, createInfo.mainDsvCpuHandle);
        }

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sliceDsvCpuHandles;
        if (!createInfo.sliceDsvDescs.empty() || !createInfo.sliceDsvCpuHandles.empty())
        {
            if (createInfo.sliceDsvDescs.size() != createInfo.sliceDsvCpuHandles.size())
            {
                std::print("DepthTextureGenerator::CreateDepthTexture failed: slice DSV descriptors and handles must have the same count.\n");
                return false;
            }

            sliceDsvCpuHandles.reserve(createInfo.sliceDsvCpuHandles.size());
            for (size_t i = 0; i < createInfo.sliceDsvDescs.size(); ++i)
            {
                D3D12_DEPTH_STENCIL_VIEW_DESC sliceDesc = createInfo.sliceDsvDescs[i];
                if (sliceDesc.ViewDimension == D3D12_DSV_DIMENSION_UNKNOWN ||
                    sliceDesc.Format == DXGI_FORMAT_UNKNOWN)
                {
                    std::print("DepthTextureGenerator::CreateDepthTexture failed: each slice DSV desc must be set explicitly.\n");
                    return false;
                }

                device->CreateDepthStencilView(
                    depthResource.Get(),
                    &sliceDesc,
                    createInfo.sliceDsvCpuHandles[i]);
                sliceDsvCpuHandles.push_back(createInfo.sliceDsvCpuHandles[i]);
            }
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = createInfo.srvDesc;
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (createInfo.srvCpuHandle.ptr != 0)
        {
            if (srvDesc.ViewDimension == D3D12_SRV_DIMENSION_UNKNOWN ||
                srvDesc.Format == DXGI_FORMAT_UNKNOWN)
            {
                std::print("DepthTextureGenerator::CreateDepthTexture failed: srvDesc must be set explicitly.\n");
                return false;
            }

            device->CreateShaderResourceView(depthResource.Get(), &srvDesc, createInfo.srvCpuHandle);
            srvFormat = srvDesc.Format;
        }

        DXGI_FORMAT finalDsvFormat = DXGI_FORMAT_UNKNOWN;
        if (createInfo.mainDsvCpuHandle.ptr != 0)
        {
            finalDsvFormat = mainDsvDesc.Format;
        }
        else if (!createInfo.sliceDsvDescs.empty())
        {
            finalDsvFormat = createInfo.sliceDsvDescs.front().Format;
        }

        outTexture = std::make_shared<DepthTexture>(
            depthResource,
            createInfo.srvCpuHandle,
            createInfo.srvGpuHandle,
            createInfo.mainDsvCpuHandle,
            std::move(sliceDsvCpuHandles),
            static_cast<UINT>(desc.Width),
            desc.Height,
            desc.DepthOrArraySize,
            desc.MipLevels,
            desc.Format,
            finalDsvFormat,
            srvFormat,
            createInfo.initialState);
        return true;
    }

    static bool CreateCascadeDepthTextureArray(
        ComPtr<ID3D12Device> device,
        const DepthTextureCreateInfo& createInfo,
        std::shared_ptr<DepthTexture>& outTexture,
        UINT expectedCascadeCount)
    {
        if (expectedCascadeCount == 0)
        {
            std::print("DepthTextureGenerator::CreateCascadeDepthTextureArray failed: expectedCascadeCount is 0.\n");
            return false;
        }

        if (createInfo.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            createInfo.resourceDesc.DepthOrArraySize != expectedCascadeCount)
        {
            std::print("DepthTextureGenerator::CreateCascadeDepthTextureArray failed: resourceDesc must explicitly describe the cascade array.\n");
            return false;
        }

        if (createInfo.mainDsvDesc.ViewDimension != D3D12_DSV_DIMENSION_TEXTURE2DARRAY)
        {
            std::print("DepthTextureGenerator::CreateCascadeDepthTextureArray failed: mainDsvDesc must explicitly be TEXTURE2DARRAY.\n");
            return false;
        }

        if (createInfo.srvCpuHandle.ptr != 0 &&
            createInfo.srvDesc.ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
        {
            std::print("DepthTextureGenerator::CreateCascadeDepthTextureArray failed: srvDesc must explicitly be TEXTURE2DARRAY.\n");
            return false;
        }

        if (!createInfo.sliceDsvDescs.empty() &&
            createInfo.sliceDsvDescs.size() != expectedCascadeCount)
        {
            std::print("DepthTextureGenerator::CreateCascadeDepthTextureArray failed: sliceDsvDescs count must match expectedCascadeCount.\n");
            return false;
        }

        if (!createInfo.sliceDsvCpuHandles.empty() &&
            createInfo.sliceDsvCpuHandles.size() != expectedCascadeCount)
        {
            std::print("DepthTextureGenerator::CreateCascadeDepthTextureArray failed: sliceDsvCpuHandles count must match expectedCascadeCount.\n");
            return false;
        }

        return CreateDepthTexture(device, createInfo, outTexture);
    }
};
