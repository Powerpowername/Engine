#include "global.h"
using Microsoft::WRL::ComPtr;

class UploadResource
{
private:
    ComPtr<ID3D12Resource1> uploadResource;
    ComPtr<ID3D12Resource1> defaultResource;
    UINT64 UploadResourceSize = 0;
    // 记录 defaultResource 当前状态
    D3D12_RESOURCE_STATES defaultCurrentState = D3D12_RESOURCE_STATE_COMMON;


private:
    bool CreateBufferResource(
        ComPtr<ID3D12Device>& device,
        ComPtr<ID3D12Resource1>& outResource,
        UINT64 size,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_GENERIC_READ
    )
    {
        if (!device || size == 0)
        {
            return false;
        }
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

        return SUCCEEDED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(outResource.GetAddressOf())
        ));
    }
    bool CreateDefaultHeapResource(
        ComPtr<ID3D12Device>& device,
        ComPtr<ID3D12Resource1>& outResource,
        D3D12_RESOURCE_DESC desc,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST
        )
    {
        if (!device)
        {
            return false;
        }
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        return SUCCEEDED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(outResource.GetAddressOf())
        ));
    }
public:
    // 创建一对资源：
    // uploadResource: CPU 可写
    // defaultResource: GPU 正式使用
    // 如果是传普通的常量缓冲,上传堆和默认堆尺寸是一样大的,因为他不是texture那种高维资源
    bool CreateDefaultAndUploadHeapResource(ComPtr<ID3D12Device>& device,UINT64 UploadResourceSize,D3D12_RESOURCE_DESC defaultDesc)
    {
        if (!device || UploadResourceSize == 0)
        {
            return false;
        }

        ResetAll();

        this->UploadResourceSize = UploadResourceSize;
        defaultCurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
        
        if (!CreateBufferResource(
            device,
            uploadResource,
            this->UploadResourceSize,
            D3D12_RESOURCE_STATE_GENERIC_READ))
        {
            return false;
        }

        if (!CreateDefaultHeapResource(
            device,
            defaultResource,
            defaultDesc,
            D3D12_RESOURCE_STATE_COPY_DEST))
        {
            ResetAll();
            return false;
        }

        return true;
    }

    bool UploadData(const void* data, UINT64 size, UINT64 offset = 0)
    {
        if (!data || !uploadResource || size == 0)
        {
            return false;
        }

        if (offset + size > UploadResourceSize)
        {
            return false;
        }

        void* mappedData = nullptr;
        D3D12_RANGE readRange{};
        readRange.Begin = 0;
        readRange.End = 0;

        if (FAILED(uploadResource->Map(0, &readRange, &mappedData)))
        {
            return false;
        }

        std::memcpy(
            static_cast<unsigned char*>(mappedData) + offset,
            data,
            static_cast<size_t>(size));

        uploadResource->Unmap(0, nullptr);
        return true;
    }

    // 手动状态切换默认堆状态
    bool Transition(
        ComPtr<ID3D12GraphicsCommandList>& commandList,
        D3D12_RESOURCE_STATES beforeState,
        D3D12_RESOURCE_STATES afterState)
    {
        if (!commandList || !defaultResource)
        {
            return false;
        }

        if (beforeState == afterState)
        {
            defaultCurrentState = afterState;
            return true;
        }

        CD3DX12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                defaultResource.Get(),
                beforeState,
                afterState);

        commandList->ResourceBarrier(1, &barrier);
        defaultCurrentState = afterState;
        return true;
    }

    // 按当前状态自动切换默认堆状态
    bool Transition(
        ComPtr<ID3D12GraphicsCommandList>& commandList,
        D3D12_RESOURCE_STATES afterState)
    {
        if(Transition(commandList, defaultCurrentState, afterState))
        {
            defaultCurrentState = afterState;
            return true;
        }
        else
            return false;
    }

    
    // 首次或后续都能用：
    // 1. 如果当前不是 COPY_DEST，先切到 COPY_DEST
    // 2. 执行 CopyBufferRegion
    // 3. 再切到 finalState
    bool CopyToDefault(
        ComPtr<ID3D12GraphicsCommandList>& commandList,
        D3D12_RESOURCE_STATES finalState,
        UINT64 size = 0,
        UINT64 srcOffset = 0,
        UINT64 dstOffset = 0)
    {
        if (!commandList || !uploadResource || !defaultResource)
        {
            return false;
        }

        UINT64 copySize = (size == 0) ? UploadResourceSize : size;

        if (srcOffset + copySize > UploadResourceSize || dstOffset + copySize > UploadResourceSize)
        {
            return false;
        }

        // 如果当前状态不是 COPY_DEST，先切过去
        if (defaultCurrentState != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            if (!Transition(commandList, defaultCurrentState, D3D12_RESOURCE_STATE_COPY_DEST))
            {
                return false;
            }
        }

        commandList->CopyBufferRegion(
            defaultResource.Get(),
            dstOffset,
            uploadResource.Get(),
            srcOffset,
            copySize
        );

        // 拷完后切到目标状态
        if (!Transition(commandList, D3D12_RESOURCE_STATE_COPY_DEST, finalState))
        {
            return false;
        }

        return true;

    }

    // 一步完成：CPU 写 + GPU 拷 + 状态切换
    bool UploadAndCopy(
        ComPtr<ID3D12GraphicsCommandList>& commandList,
        const void* data,
        UINT64 size,
        D3D12_RESOURCE_STATES finalState,
        UINT64 srcOffset = 0,
        UINT64 dstOffset = 0)
    {
        if (!UploadData(data, size, srcOffset))
        {
            return false;
        }

        return CopyToDefault(commandList, finalState, size, srcOffset, dstOffset);
    }

    void ResetUpload()
    {
        uploadResource.Reset();
    }

    void ResetAll()
    {
        uploadResource.Reset();
        defaultResource.Reset();
        defaultCurrentState = D3D12_RESOURCE_STATE_COMMON;
        this->UploadResourceSize = 0;
    }

    const ComPtr<ID3D12Resource1>& GetUploadResource() const
    {
        return uploadResource;
    }

    const ComPtr<ID3D12Resource1>& GetDefaultResource() const
    {
        return defaultResource;
    }

    UINT64 GetUploadResourceSize() const
    {
        return UploadResourceSize;
    }
};
