#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

inline std::uint64_t AlignConstantBufferByteSize(std::uint64_t byteSize) {
    return (std::max<std::uint64_t>(byteSize, 1u) + 255u) & ~255ull;
}

class MappedUploadBuffer {
public:
    MappedUploadBuffer() = default;

    ~MappedUploadBuffer() {
        Release();
    }

    MappedUploadBuffer(const MappedUploadBuffer&) = delete;
    MappedUploadBuffer& operator=(const MappedUploadBuffer&) = delete;

    MappedUploadBuffer(MappedUploadBuffer&& other) noexcept {
        MoveFrom(other);
    }

    MappedUploadBuffer& operator=(MappedUploadBuffer&& other) noexcept {
        if (this != &other) {
            Release();
            MoveFrom(other);
        }
        return *this;
    }

    bool Initialize(ID3D12Device* device, std::uint64_t requestedByteSize) {
        Release();
        if (device == nullptr || requestedByteSize == 0u) {
            return false;
        }

        byteSize = AlignConstantBufferByteSize(requestedByteSize);

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1u;
        heapProperties.VisibleNodeMask = 1u;

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = byteSize;
        resourceDesc.Height = 1u;
        resourceDesc.DepthOrArraySize = 1u;
        resourceDesc.MipLevels = 1u;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1u;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(resource.GetAddressOf()))))
        {
            Release();
            return false;
        }

        const D3D12_RANGE readRange = {0u, 0u};
        if (FAILED(resource->Map(0u, &readRange, reinterpret_cast<void**>(&mappedData)))) {
            Release();
            return false;
        }

        std::memset(mappedData, 0, static_cast<std::size_t>(byteSize));
        return true;
    }

    void Release() {
        if (mappedData != nullptr && resource != nullptr) {
            resource->Unmap(0u, nullptr);
        }
        mappedData = nullptr;
        resource.Reset();
        byteSize = 0u;
    }

    bool Update(const void* data, std::uint64_t dataSize) {
        if (mappedData == nullptr || data == nullptr || dataSize > byteSize) {
            return false;
        }

        std::memcpy(mappedData, data, static_cast<std::size_t>(dataSize));
        return true;
    }

    template <typename T>
    bool Update(const T& data) {
        return Update(&data, sizeof(T));
    }

    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress() const {
        return resource != nullptr ? resource->GetGPUVirtualAddress() : 0u;
    }

    ID3D12Resource* Get() const {
        return resource.Get();
    }

    std::uint64_t ByteSize() const {
        return byteSize;
    }

private:
    void MoveFrom(MappedUploadBuffer& other) noexcept {
        resource = std::move(other.resource);
        mappedData = other.mappedData;
        byteSize = other.byteSize;

        other.mappedData = nullptr;
        other.byteSize = 0u;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    std::uint8_t* mappedData = nullptr;
    std::uint64_t byteSize = 0u;
};

struct FrameResource {
    bool Initialize(
        ID3D12Device* device,
        std::uint64_t sceneConstantsSize,
        std::uint64_t computeConstantsSize,
        D3D12_COMMAND_LIST_TYPE commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT) {
        Release();
        if (device == nullptr) {
            return false;
        }
        if (FAILED(device->CreateCommandAllocator(
                commandListType,
                IID_PPV_ARGS(commandAllocator.GetAddressOf()))))
        {
            return false;
        }
        if (!sceneConstants.Initialize(device, sceneConstantsSize)) {
            Release();
            return false;
        }
        if (!computeConstants.Initialize(device, computeConstantsSize)) {
            Release();
            return false;
        }
        return true;
    }

    void Release() {
        instanceData.Release();
        computeConstants.Release();
        sceneConstants.Release();
        commandAllocator.Reset();
        instanceBufferView = {};
        fenceValue = 0u;
        frameIndex = 0u;
    }

    bool ResetCommandAllocator() {
        return commandAllocator != nullptr && SUCCEEDED(commandAllocator->Reset());
    }

    const Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& CommandAllocator() const {
        return commandAllocator;
    }

    bool EnsureInstanceCapacity(ID3D12Device* device, std::uint64_t byteSize, UINT strideBytes) {
        const std::uint64_t safeByteSize = std::max<std::uint64_t>(byteSize, strideBytes);
        if (instanceData.Get() == nullptr || instanceData.ByteSize() < safeByteSize) {
            if (!instanceData.Initialize(device, safeByteSize)) {
                instanceBufferView = {};
                return false;
            }
        }

        instanceBufferView.BufferLocation = instanceData.GpuAddress();
        instanceBufferView.SizeInBytes = static_cast<UINT>(safeByteSize);
        instanceBufferView.StrideInBytes = strideBytes;
        return true;
    }

    bool UpdateInstanceData(const void* data, std::uint64_t byteSize) {
        return instanceData.Update(data, byteSize);
    }

    const D3D12_VERTEX_BUFFER_VIEW& InstanceBufferView() const {
        return instanceBufferView;
    }

    template <typename T>
    bool UpdateSceneConstants(const T& constants) {
        return sceneConstants.Update(constants);
    }

    template <typename T>
    bool UpdateComputeConstants(const T& constants) {
        return computeConstants.Update(constants);
    }

    D3D12_GPU_VIRTUAL_ADDRESS SceneConstantsGpuAddress() const {
        return sceneConstants.GpuAddress();
    }

    D3D12_GPU_VIRTUAL_ADDRESS ComputeConstantsGpuAddress() const {
        return computeConstants.GpuAddress();
    }

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    MappedUploadBuffer sceneConstants;
    MappedUploadBuffer computeConstants;
    MappedUploadBuffer instanceData;
    D3D12_VERTEX_BUFFER_VIEW instanceBufferView = {};
    UINT64 fenceValue = 0u;
    UINT frameIndex = 0u;
};
