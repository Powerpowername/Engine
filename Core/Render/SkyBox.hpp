#pragma once

#include "Descriptor/Descriptor.hpp"

#include <DirectXMath.h>
#include <d3d12.h>
#include <memory>

class SkyBox
{
public:
    SkyBox();
    ~SkyBox();

    SkyBox(const SkyBox&) = delete;
    SkyBox& operator=(const SkyBox&) = delete;

    bool Initialize(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        DescriptorHeap* srvDescriptorHeap,
        const char* positiveXPath,
        const char* negativeXPath,
        const char* positiveYPath,
        const char* negativeYPath,
        const char* positiveZPath,
        const char* negativeZPath);
    void ReleaseUploadResources();
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const;
    void Draw(ID3D12GraphicsCommandList* commandList) const;

private:
    struct SkyBoxVertex
    {
        DirectX::XMFLOAT3 position{};
    };

    bool CreateCubeGeometry(ID3D12Device* device,ID3D12GraphicsCommandList* commandList);
    bool CreateCubeTexture(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        DescriptorHeap& srvDescriptorHeap,
        const char* positiveXPath,
        const char* negativeXPath,
        const char* positiveYPath,
        const char* negativeYPath,
        const char* positiveZPath,
        const char* negativeZPath);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource1> cubeTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource1> cubeTextureUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource1> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource1> vertexUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource1> indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource1> indexUpload;

    std::shared_ptr<DescriptorHandle> srvHandle;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    UINT indexCount = 0;
    bool initialized = false;
};
