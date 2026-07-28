#pragma once
#include "global.h"
#include <algorithm>
#include <queue>
#include <unordered_map>
using Microsoft::WRL::ComPtr;
enum class DescriptorType
{
    DEFAULT = 0,
    CBV,
    SRV,
    UAV,
    RTV,
    DSV,
    SAMPLER
};

struct DescriptorHandle
{
    // static UINT base;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    UINT index = 0;
    bool isFree = true;
    void Create(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleStart,D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleStart,UINT index,UINT descriptroSize)
    {
        this->index = index;
        cpuHandle.ptr = index * descriptroSize + cpuHandleStart.ptr;
        gpuHandle.ptr = index * descriptroSize + gpuHandleStart.ptr;
        isFree = true;
    }
    void Use()
    {
        isFree = false;
    }
    void Release()
    {
        isFree = true;
    }

};
// 需要考虑资源的开辟与回收机制
class DescriptorHeap
{
private:
    ComPtr<ID3D12DescriptorHeap> heap = nullptr;
    std::queue<std::shared_ptr<DescriptorHandle>> FreeHandleRoom; // 使用队列的原因是让他可以直接出队就是空闲描述符句柄
    std::unordered_map<UINT,std::shared_ptr<DescriptorHandle>> UsedHandleRoom; // 使用vector的原因是可以直接提取数据
    D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType;
    UINT descriptroSize = 0;
    UINT number = 0;
    std::mutex mutex;

public:
    bool CreateDescriptorHeap(ComPtr<ID3D12Device> device,D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType,UINT numDescriptors)
    {
        if(heap != nullptr || numDescriptors == 0 || device == nullptr)
        {
            std::print("DescriptorHeap has been created or parameter is fault\n");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = descriptorHeapType;
        this->descriptorHeapType = descriptorHeapType;
        desc.NumDescriptors = numDescriptors;
        this->number = numDescriptors;
        switch(descriptorHeapType)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;break;
        }
        
        desc.NodeMask = 0;
        HRESULT hr = device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(heap.GetAddressOf()));
        if(FAILED(hr))
        {
            std::print("DescriptorHeap::CreateCbvSrvUavDescriptorHeap filed\n");
            return false;
        }
        descriptroSize = device->GetDescriptorHandleIncrementSize(descriptorHeapType);
        // 清空队列
        std::queue<std::shared_ptr<DescriptorHandle>> emptyFreeHandleRoom;
        FreeHandleRoom.swap(emptyFreeHandleRoom);
        // 创建空闲描述符指针队列
        const bool shaderVisible =
            descriptorHeapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            descriptorHeapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        for(UINT i = 0;i < numDescriptors;++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleStart = heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleStart = {};
            if (shaderVisible)
            {
                gpuHandleStart = heap->GetGPUDescriptorHandleForHeapStart();
            }
            auto tempDescriptor = std::make_shared<DescriptorHandle>();
            tempDescriptor->Create(cpuHandleStart,gpuHandleStart,i,descriptroSize);
            FreeHandleRoom.push(tempDescriptor);
        }
        UsedHandleRoom.clear();
        return true;
    }

    // 自动分配
    std::shared_ptr<DescriptorHandle> GetFreeDescriptorHandle()
    {
        // 考虑线程安全 
        std::unique_lock lock(mutex);
        if(FreeHandleRoom.empty())
            return nullptr;
        std::shared_ptr<DescriptorHandle> descriptorHandle;
        descriptorHandle = FreeHandleRoom.front();
        FreeHandleRoom.pop();
        descriptorHandle->Use();
        UsedHandleRoom[descriptorHandle->index] = descriptorHandle;
        return descriptorHandle;
    }

    /// @brief 根据索引释放对应位置的descriptor handle
    /// @param index 
    void ReleaseDescriptorHandle(UINT index)
    {        
        // 考虑线程安全 
        std::unique_lock lock(mutex);
        auto itrator = UsedHandleRoom.find(index);
        if(itrator != UsedHandleRoom.end())
        {
            itrator->second->Release();
            FreeHandleRoom.push(itrator->second);
            UsedHandleRoom.erase(itrator);
        }
        

    }

    ComPtr<ID3D12DescriptorHeap> GetDescriptorHeapComPtr()
    {
        return heap;
    }

};

