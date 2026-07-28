#pragma once
#include "global.h"
using Microsoft::WRL::ComPtr;

class Fence
{
private:
    ComPtr<ID3D12Fence> fence;
    HANDLE eventHandle = nullptr;
    UINT64 currentValue = 0;
public: 
    Fence() = default;

    ~Fence()
    {
        if(eventHandle)
        {
            CloseHandle(eventHandle);
            eventHandle = nullptr;
        }
    }
    // 删除拷贝构造
    Fence(const Fence&) = delete;
    // 删除拷贝构造运算符
    Fence& operator=(const Fence&) = delete;

    Fence(Fence&& other) noexcept
        : fence(std::move(other.fence))
        , eventHandle(std::move(other.eventHandle))
        , currentValue(std::move(other.currentValue))
    {
        other.eventHandle = nullptr;
        other.currentValue = 0;
    }


    Fence& operator=(Fence&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        if (eventHandle)
        {
            CloseHandle(eventHandle);
        }

        fence = std::move(other.fence);
        eventHandle = other.eventHandle;
        currentValue = other.currentValue;

        other.eventHandle = nullptr;
        other.currentValue = 0;
        return *this;
    }

    bool Initialize(ComPtr<ID3D12Device> device,UINT64 initialValue = 0)
    {
        if(device == nullptr)
        {
            return false;
        }
        currentValue = initialValue;
        if(FAILED(device->CreateFence(
            currentValue,D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(fence.GetAddressOf())
        )))
        {
            std::print("Create Fence failly\n");
        }

        eventHandle = CreateEvent(nullptr,FALSE,FALSE,nullptr);

        return eventHandle != nullptr;
    }

    UINT64 Signal(ComPtr<ID3D12CommandQueue> commandQueue)
    {
        if(!commandQueue || !fence)
        {
            return 0;
        }

        const UINT64 signalValue = ++currentValue;
        if(FAILED(commandQueue->Signal(fence.Get(),signalValue)))
        {
            return 0;
        }
        return signalValue;
    }

    bool IsComplete(UINT64 value) const
    {
        return fence && fence->GetCompletedValue() >= value;
    }

    UINT64 GetCompletedValue() const
    {
        return fence ? fence->GetCompletedValue() : 0;
    }

    UINT64 GetCurrentValue() const
    {
        return currentValue;
    }


    bool Wait(uint64_t value)
    {
        if (!fence || !eventHandle)
        {
            return false;
        }

        if (IsComplete(value))
        {
            return true;
        }

        if (FAILED(fence->SetEventOnCompletion(value, eventHandle)))
        {
            return false;
        }

        WaitForSingleObject(eventHandle, INFINITE);
        return true;
    }

    ComPtr<ID3D12Fence> GetFenceComPtr() const
    {
        return fence;
    }
};

class CommandList
{
private:
    ComPtr<ID3D12GraphicsCommandList> commandList;
    D3D12_COMMAND_LIST_TYPE commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
public:
    bool Initialize(
        ComPtr<ID3D12Device> device,
        D3D12_COMMAND_LIST_TYPE commandListType,
        ComPtr<ID3D12CommandAllocator> commandAllocator
    )
    {
        if(!device || !commandAllocator)
        {
            std::print("Device or commandAllocator is nullptr\n");
            return false;
        }
        this->commandListType = commandListType;
        if(FAILED(device->CreateCommandList(
            0,commandListType,commandAllocator.Get(),nullptr,IID_PPV_ARGS(commandList.GetAddressOf())
        )))
        {
            return false;
        }
        return SUCCEEDED(this->commandList->Close());
    }

    bool Reset(ComPtr<ID3D12CommandAllocator> commandAllocator)
    {
        if(!this->commandList || !commandAllocator)
        {
            std::print("CommandAllocator or CommandList is nullptr\n");
            return false;
        }
        return SUCCEEDED(commandList->Reset(commandAllocator.Get(),nullptr));       
    }

    void SetPipelineState(ComPtr<ID3D12PipelineState> pso)
    {
        commandList->SetPipelineState(pso.Get());
    }

    void SetGraphicsRootSignature(ComPtr<ID3D12RootSignature> sig)
    {
        commandList->SetGraphicsRootSignature(sig.Get());
    }

    void SetComputeRootSignature(ComPtr<ID3D12RootSignature> sig)
    {
        commandList->SetComputeRootSignature(sig.Get());
    }
    
    void SetDescriptorHeaps(std::vector<ComPtr<ID3D12DescriptorHeap>>& heaps)
    {
        std::vector<ID3D12DescriptorHeap*> raw;
        raw.reserve(heaps.size());
        for (auto& h : heaps)
            raw.push_back(h.Get());
        commandList->SetDescriptorHeaps((UINT)raw.size(), raw.data());
    }

    void SetDescriptorHeaps(ComPtr<ID3D12DescriptorHeap> heap)
    {
        ID3D12DescriptorHeap* h = heap.Get();
        commandList->SetDescriptorHeaps(1, &h);
    }
    void SetViewport(const D3D12_VIEWPORT& vp)
    {
        commandList->RSSetViewports(1,&vp);
    }

    void SetScissorRect(const D3D12_RECT& rect)
    {
        commandList->RSSetScissorRects(1, &rect);
    }

    void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topo)
    {
        commandList->IASetPrimitiveTopology(topo);
    }

    void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& vb)
    {
        commandList->IASetVertexBuffers(0, 1, &vb);
    }

    void SetVertexBuffers(UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW* views)
    {
        commandList->IASetVertexBuffers(startSlot, count, views);
    }

    void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& ib)
    {
        commandList->IASetIndexBuffer(&ib);
    }

    void SetGraphicsRootCBV(UINT i, D3D12_GPU_VIRTUAL_ADDRESS addr)
    {
        commandList->SetGraphicsRootConstantBufferView(i, addr);
    }

    void SetGraphicsRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h)
    {
        commandList->SetGraphicsRootDescriptorTable(i, h);
    }

    void OMSetRenderTargets(
        UINT numRenderTargetDescriptors,
        const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors,
        BOOL rtvsSingleHandleToDescriptorRange,
        const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor)
    {
        commandList->OMSetRenderTargets(
            numRenderTargetDescriptors,
            renderTargetDescriptors,
            rtvsSingleHandleToDescriptorRange,
            depthStencilDescriptor);
    }

    void ClearRenderTargetView(
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
        const FLOAT colorRGBA[4],
        UINT numRects = 0,
        const D3D12_RECT* rects = nullptr)
    {
        commandList->ClearRenderTargetView(renderTargetView, colorRGBA, numRects, rects);
    }

    void ClearDepthStencilView(
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
        D3D12_CLEAR_FLAGS clearFlags,
        FLOAT depth,
        UINT8 stencil,
        UINT numRects = 0,
        const D3D12_RECT* rects = nullptr)
    {
        commandList->ClearDepthStencilView(depthStencilView, clearFlags, depth, stencil, numRects, rects);
    }

    void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers)
    {
        commandList->ResourceBarrier(numBarriers, barriers);
    }

    void Transition(ComPtr<ID3D12Resource> resoure,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
    {
        Transition(resoure.Get(), before, after);
    }

    void Transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource  = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;// 一般都是用这个
        commandList->ResourceBarrier(1, &barrier);
    }

    void UAVBarrier(ID3D12Resource* resource)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        commandList->ResourceBarrier(1, &barrier);
    }

    void Draw(UINT vertexCount, UINT startVertex = 0)
    {
        commandList->DrawInstanced(vertexCount, 1, startVertex, 0);
    }

    void DrawIndexed(UINT indexCount, UINT startIndex = 0, INT baseVertex = 0)
    {
        commandList->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
    }

    void DrawIndexedInstanced(
        UINT indexCount,
        UINT instanceCount,
        UINT startIndex = 0,
        INT baseVertex = 0,
        UINT startInstance = 0)
    {
        commandList->DrawIndexedInstanced(
            indexCount,
            instanceCount,
            startIndex,
            baseVertex,
            startInstance);
    }
    // Compute shader config
    void SetComputeRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h)
    {
        commandList->SetComputeRootDescriptorTable(i, h);
    }

    void SetComputeRootCBV(UINT i, D3D12_GPU_VIRTUAL_ADDRESS addr)
    {
        commandList->SetComputeRootConstantBufferView(i, addr);
    }

    void SetComputeRootSRV(UINT i, D3D12_GPU_VIRTUAL_ADDRESS addr)
    {
        commandList->SetComputeRootShaderResourceView(i, addr);
    }

    void SetComputeRootUAV(UINT i, D3D12_GPU_VIRTUAL_ADDRESS addr)
    {
        commandList->SetComputeRootUnorderedAccessView(i, addr);
    }

    void SetComputeRoot32BitConstants(
        UINT i,
        UINT num32BitValues,
        const void* data,
        UINT destOffsetIn32BitValues = 0)
    {
        commandList->SetComputeRoot32BitConstants(
            i,
            num32BitValues,
            data,
            destOffsetIn32BitValues);
    }

    void Dispatch(UINT x, UINT y, UINT z = 1)
    {
        commandList->Dispatch(x, y, z);
    }

    void SetComputePipelineState(ComPtr<ID3D12PipelineState> pso)
    {
        commandList->SetPipelineState(pso.Get());
    }

    
    bool Close()
    {
        return commandList && SUCCEEDED(commandList->Close());
    }

    const ComPtr<ID3D12GraphicsCommandList> GetGraphicsComPtr() const
    {
        return commandList;
    }

};

class CommandAllocator
{
private:
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    D3D12_COMMAND_LIST_TYPE commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
public:
    CommandAllocator() = default;

    CommandAllocator(const CommandAllocator&) = delete;
    CommandAllocator& operator=(const CommandAllocator&) = delete;

    CommandAllocator(CommandAllocator&&) noexcept = default;
    CommandAllocator& operator=(CommandAllocator&&) noexcept = default;

    bool Initialize(ComPtr<ID3D12Device> device,D3D12_COMMAND_LIST_TYPE commandListType)
    {
        if (!device)
        {
            std::print("Device is nullptr\n");
            return false;
        }
        this->commandListType = commandListType;
        if (FAILED(device->CreateCommandAllocator(
            commandListType,
            IID_PPV_ARGS(commandAllocator.GetAddressOf())
        )))
        {
            std::print("Create CommandAllocator failly\n");
            return false;
        }

        return true;
    }

    bool Reset()
    {
        if (!commandAllocator)
        {
            std::print("CommandAllocator is nullptr\n");
            return false;
        }

        if (FAILED(commandAllocator->Reset()))
        {
            std::print("Reset CommandAllocator failly\n");
            return false;
        }

        return true;
    }

    const ComPtr<ID3D12CommandAllocator> GetCommandAllocatorComPtr() const
    {
        return commandAllocator;
    }

    D3D12_COMMAND_LIST_TYPE GetType() const
    {
        return commandListType;
    }
};

class CommandQueue
{
private:
    ComPtr<ID3D12CommandQueue> commandQueue;
    Fence fence;
    D3D12_COMMAND_LIST_TYPE commandType = D3D12_COMMAND_LIST_TYPE_DIRECT;

public:
    bool Initialize(ComPtr<ID3D12Device> device,D3D12_COMMAND_LIST_TYPE commandType)
    {   
        if(!device)
        {
            std::print("Device is nullptr\n");
            return false;
        }
        this->commandType = commandType;
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = commandType;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 0;

        if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(commandQueue.GetAddressOf()))))
        {
            return false;
        }
        return fence.Initialize(device);
    }

    UINT64 Execute(ComPtr<ID3D12CommandList> commandLists)
    {
        if(!this->commandQueue || !commandLists)
        {
            std::print("CommandQueue or commandLists is null\n");
        }
        ID3D12CommandList* lists[] = { commandLists.Get() };
        commandQueue->ExecuteCommandLists(1, lists);
        return fence.Signal(commandQueue);
    }

    UINT64 Execute(ID3D12CommandList* const* commandLists, uint32_t count)
    {
        if (!commandQueue || !commandLists || count == 0)
        {
            return 0;
        }

        commandQueue->ExecuteCommandLists(count, commandLists);
        return fence.Signal(commandQueue);
    }

    // 强制刷新
    void Flush()
    {
        const uint64_t value = fence.Signal(commandQueue);
        if (value != 0)
        {
            fence.Wait(value);
        }
    }

    // 预渲染的时候需要单独分开Signal和Wait操作，这样可以让CPU做预渲染
    UINT64 Signal()
    {
        return fence.Signal(commandQueue);
    }

    bool Wait(UINT64 value)
    {
        return fence.Wait(value);
    }


    const ComPtr<ID3D12CommandQueue> GetCommandQueueComPtr() const
    {
        return commandQueue;
    }

    D3D12_COMMAND_LIST_TYPE GetType() const
    {
        return commandType;
    }
    const Fence& GetFence() const
    {
        return fence;
    }
};
