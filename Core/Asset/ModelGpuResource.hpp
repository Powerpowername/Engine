#pragma once
#include "Asset/ModelAsset.hpp"
#include <cstring>
#include <memory>
#include <vector>

struct ModelGpuMesh
{
    Microsoft::WRL::ComPtr<ID3D12Resource1> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource1> indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource1> vertexUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource1> indexUpload;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    UINT indexCount = 0;
    UINT materialIndex = 0;
};

struct ModelGpuVertex
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 normal{};
    DirectX::XMFLOAT3 tangent{};
    DirectX::XMFLOAT3 bitangent{};
    DirectX::XMFLOAT2 uv{};
};
struct ModelGpuResource
{
    Model *sourceModel = nullptr;
    std::vector<ModelGpuMesh> meshes;
};

std::shared_ptr<ModelGpuResource> CreateModelGpuResource();
bool CreateModelGpuResourceFromModel(
    ModelGpuResource& gpuResource,
    Model& model,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList);
void ReleaseModelGpuResourceUploadResources(ModelGpuResource& gpuResource);
void DrawModelGpuResource(ModelGpuResource& gpuResource, ID3D12GraphicsCommandList* commandList);
void DrawModelGpuResourceInstanced(
    ModelGpuResource& gpuResource,
    ID3D12GraphicsCommandList* commandList,
    unsigned int instanceCount);
