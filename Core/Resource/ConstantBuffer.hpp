#pragma once
#include "Descriptor/Descriptor.hpp"
#include "Resource/Resource.hpp"

using Microsoft::WRL::ComPtr;


class ConstantBuffer : public Resoure
{
private: 
    UINT alignedSize = 0;
    void* mapAdderss = nullptr;
    std::shared_ptr<DescriptorHandle> cbvDescriptorHandle;

public:
    /// @brief 创建资源堆，映射资源堆内存地址
    /// @param device 
    /// @param size 
    /// @return 
    bool Init(ComPtr<ID3D12Device> device,UINT size)
    {
        alignedSize = (size + 255) & ~255;// 按照256字节对齐
        // 资源堆的创建属性描述符
        auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        // D3D12_HEAP_PROPERTIES uploadHeapProps{};
        // uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = alignedSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;//主要针对矩阵
        if(FAILED(device->CreateCommittedResource(
            &uploadHeapProps,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(resource.GetAddressOf())
        )))
        {
            std::print("Creating const buffer failly\n");
            return false;
        }

        return SUCCEEDED(resource->Map(0,nullptr,&mapAdderss));
    }

    /// @brief 更新常量缓冲区数据
    /// @param data 
    /// @param size 
    void Update(const void* data,UINT size)
    {
        if (mapAdderss == nullptr || size > alignedSize)
        {
            std::print("mapAdderss is null or size > alignedSize\n");
            return;
        }
        std::memcpy(mapAdderss,data,size);
    }

    D3D12_GPU_VIRTUAL_ADDRESS GPUAddress() const
    {
        return resource->GetGPUVirtualAddress();
    }

    UINT AlignedSize() const
    {
        return alignedSize;
    }

    /// @brief 在传入的描述符堆中创建CBV
    /// @param descriptorHeap 
    /// @return 
    bool CreateCBV(ComPtr<ID3D12Device> device,DescriptorHeap& descriptorHeap)
    {
        if(this->GPUAddress() == 0 )
            return false;
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
        desc.BufferLocation = this->GPUAddress();
        desc.SizeInBytes = alignedSize;
        cbvDescriptorHandle = descriptorHeap.GetFreeDescriptorHandle();
        if(!cbvDescriptorHandle)
            return false;
        device->CreateConstantBufferView(&desc, cbvDescriptorHandle->cpuHandle);
        return true;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetCBVGpuHandle() const
    {
        return cbvDescriptorHandle ? cbvDescriptorHandle->gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

};
