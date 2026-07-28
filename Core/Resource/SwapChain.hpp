#pragma once
#include "global.h"
#include "Descriptor/Descriptor.hpp"
using Microsoft::WRL::ComPtr;

struct BackBuffer
{
    ComPtr<ID3D12Resource1> resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PRESENT;

    void Reset()
    {
        resource.Reset();
        cpuHandle = {};
        gpuHandle = {};
        currentState = D3D12_RESOURCE_STATE_PRESENT;
    }

    bool Transition(
        const ComPtr<ID3D12GraphicsCommandList>& commandList,
        D3D12_RESOURCE_STATES afterState
    )
    {
        if (!commandList || !resource)
        {
            return false;
        }

        if (currentState == afterState)
        {
            return true;
        }

        CD3DX12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(
                resource.Get(),
                currentState,
                afterState);

        commandList->ResourceBarrier(1, &barrier);
        currentState = afterState;
        return true;
    }
};


class SwapChain
{
private:
    static constexpr UINT BufferCount = 2;

    ComPtr<IDXGISwapChain3> swapChain = nullptr;
    //          类型        数量
    std::array<BackBuffer, BufferCount> backBuffers{};

    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT currentBackBufferIndex = 0;
    HWND hwnd = nullptr;

private:
    bool CreateBackBuffersAndRTV(ComPtr<ID3D12Device> device,std::vector<DescriptorHandle> rtvDescriptorHandle)
    {
        if(!swapChain || !device || rtvDescriptorHandle.size() != BufferCount)
        {
            std::printf("CreateBackBuffersAndRTV error");
            return false;
        }
        const UINT rtvDescriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for(UINT i = 0;i < BufferCount;i++)
        {
            backBuffers[i].Reset();
            if (FAILED(swapChain->GetBuffer(
                i,
                IID_PPV_ARGS(backBuffers[i].resource.GetAddressOf()))))
            {
                std::print("SwapChain::GetBuffer failed, index = {}\n", i);
                return false;
            }

            device->CreateRenderTargetView(
                backBuffers[i].resource.Get(),
                nullptr,
                rtvDescriptorHandle[i].cpuHandle);
            backBuffers[i].cpuHandle = rtvDescriptorHandle[i].cpuHandle;
            backBuffers[i].gpuHandle = rtvDescriptorHandle[i].gpuHandle;
            backBuffers[i].currentState = D3D12_RESOURCE_STATE_PRESENT;//这个状态可能要修改一下
            // cpuHandle.ptr += static_cast<SIZE_T>(rtvDescriptorSize);
            // if (gpuHandle.ptr != 0)
            // {
            //     gpuHandle.ptr += static_cast<UINT64>(rtvDescriptorSize);
            // }
        }
        currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();
        return true;
    }
    void ReleaseBackBuffers()
    {
        for (auto& buffer : backBuffers)
        {
            buffer.Reset();
        }
    }
public:
    bool Create(
        ComPtr<IDXGIFactory4> factory,
        ComPtr<ID3D12CommandQueue> commandQueue,
        ComPtr<ID3D12Device> device,
        std::vector<DescriptorHandle> rtvDescriptorHandle,
        UINT freeDescriptorCount,
        HWND hwnd,
        UINT width,
        UINT height,
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
        bool enableTearing = false)
    {
        if (!commandQueue || !device || hwnd == nullptr || width == 0 || height == 0)
        {
            return false;
        }

        this->hwnd = hwnd;
        this->width = width;
        this->height = height;
        this->format = format;

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1; // swapchain backbuffer 基本都是非 MSAA
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = freeDescriptorCount;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        desc.Flags = enableTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> tempSwapChain = nullptr;
        if (FAILED(factory->CreateSwapChainForHwnd(
            commandQueue.Get(),
            hwnd,
            &desc,
            nullptr,
            nullptr,
            tempSwapChain.GetAddressOf())))
        {
            std::print("SwapChain::CreateSwapChainForHwnd failed\n");
            return false;
        }

        if (FAILED(tempSwapChain.As(&swapChain)))
        {
            std::print("SwapChain::Query IDXGISwapChain4 failed\n");
            return false;
        }
        // 将factory和窗口绑定
        if (FAILED(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)))
        {
            std::print("SwapChain::MakeWindowAssociation failed\n");
        }

        return CreateBackBuffersAndRTV(device, rtvDescriptorHandle);
    }

    bool Resize(
        ComPtr<ID3D12Device> device,
        std::vector<DescriptorHandle> rtvDescriptorHandle,
        UINT newWidth,
        UINT newHeight)
    {
        if (!swapChain || !device || newWidth == 0 || newHeight == 0)
        {
            return false;
        }

        ReleaseBackBuffers();
        if (FAILED(swapChain->ResizeBuffers(
            BufferCount,
            newWidth,
            newHeight,
            format,
            0)))
        {
            std::print("SwapChain::ResizeBuffers failed\n");
            return false;
        }
        width = newWidth;
        height = newHeight;

        return CreateBackBuffersAndRTV(device, rtvDescriptorHandle);
    }

    bool Present(UINT syncInterval = 1, UINT flags = 0)
    {
        if (!swapChain)
        {
            return false;
        }
        if(FAILED(swapChain->Present(syncInterval, flags)))
        {
            std::print("SwapChain::Present failed\n");
            return false;
        }

        currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    BackBuffer GetCurrentBackBuffer()
    {
        return backBuffers[currentBackBufferIndex];
    }
    const BackBuffer GetCurrentBackBuffer() const
    {
        return backBuffers[currentBackBufferIndex];
    }

    BackBuffer GetBackBuffer(UINT index)
    {
        return backBuffers[index];
    }

    const BackBuffer GetBackBuffer(UINT index) const
    {
        return backBuffers[index];
    }

    ComPtr<IDXGISwapChain3> GetSwapChainComPtr() const
    {
        return swapChain;
    }

    ComPtr<ID3D12Resource1> GetCurrentBackBufferResource() const
    {
        return backBuffers[currentBackBufferIndex].resource;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVCPUHandle() const
    {
        return backBuffers[currentBackBufferIndex].cpuHandle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVCPUHandle(UINT index) const
    {
        return backBuffers[index].cpuHandle;
    }

    UINT GetCurrentBackBufferIndex() const
    {
        return currentBackBufferIndex;
    }

    UINT GetWidth() const
    {
        return width;
    }

    UINT GetHeight() const
    {
        return height;
    }

    DXGI_FORMAT GetFormat() const
    {
        return format;
    }

    UINT GetBufferCount() const
    {
        return BufferCount;
    }


};

