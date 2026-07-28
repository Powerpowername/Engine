#pragma once
#include "Resource/Resource.hpp"
#include "Resource/UploadReosurce.hpp"
using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex
{
    //XMFLOAT3存数据,XMVECTOR算数据
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

class BaseGenerater
{
protected:
    std::vector<Vertex> vertexs;
    // Index index;
};

class CubeGenerater : BaseGenerater
{
    CubeGenerater()
    {
        auto mesh = open3d::geometry::TriangleMesh::CreateBox(1.0, 1.0, 1.0);
        mesh->ComputeVertexNormals();
        this->vertexs.resize(mesh->vertices_.size());
        for (size_t i = 0; i < mesh->vertices_.size(); ++i) 
        {
            const auto& p = mesh->vertices_[i];
            const auto& n = mesh->vertex_normals_[i];
            vertexs.push_back({
                XMFLOAT3(static_cast<float>(p.x()),static_cast<float>(p.y()),static_cast<float>(p.z())),
                XMFLOAT3(static_cast<float>(n.x()),static_cast<float>(n.y()),static_cast<float>(n.z())),
                XMFLOAT2(0.0,0.0)
            });
        }
    }
};
template <typename VertexTemplate>
class VertexBuffer
{
private:
    std::vector<VertexTemplate> vertices;
    D3D12_VERTEX_BUFFER_VIEW vbView;    
    UploadResource uploadResource;
public:
    bool Init(ComPtr<ID3D12Device> device,ComPtr<ID3D12GraphicsCommandList> commandList,const std::vector<VertexTemplate>& vertices)
    {
        if (!device || vertices.empty())
        {
            std::print("Device or vertices is null\n");
            return false;
        }
        // this->vertices = vertices;
        this->vertices = std::move(vertices);
        D3D12_RESOURCE_DESC defaultDesc = CD3DX12_RESOURCE_DESC::Buffer((this->vertices.size()) * sizeof(VertexTemplate));
        uploadResource.CreateDefaultAndUploadHeapResource(device,(this->vertices.size()) * sizeof(VertexTemplate),defaultDesc);
        
        // 应该用默认堆的地址还是上传堆的地址?
        vbView.BufferLocation = uploadResource.GetDefaultResource()->GetGPUVirtualAddress();
        vbView.StrideInBytes = sizeof(VertexTemplate);
        vbView.SizeInBytes = (UINT)(vertices.size() * sizeof(VertexTemplate));
        return true;
    }

    bool UploadToDefault(ComPtr<ID3D12GraphicsCommandList> commandList,D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
    {
        return uploadResource.UploadAndCopy(commandList,vertices.data(),vertices.size() * sizeof(VertexTemplate),finalState);
    }

    void Reset()
    {
        vertices.clear();
        uploadResource.ResetAll();//上传堆与默认堆都重置
    }
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView()
    {
        return vbView;
    }
};

struct Index
{
    uint32_t index;
};
template <typename IndexTemplate>
class IndexBuffer
{
private:
    std::vector<IndexTemplate> indexes;
    D3D12_INDEX_BUFFER_VIEW ibView;    
    UploadResource uploadResource;
public:
    bool Init(ComPtr<ID3D12Device> device,ComPtr<ID3D12GraphicsCommandList> commandList,const std::vector<IndexTemplate>& indexes)
    {
        if (!device || indexes.empty())
        {
            std::print("Device or vertices is null\n");
            return false;
        }
        this->indexes = indexes;
        D3D12_RESOURCE_DESC defaultDesc = CD3DX12_RESOURCE_DESC::Buffer((this->indexes.size()) * sizeof(IndexTemplate));
        uploadResource.CreateDefaultAndUploadHeapResource(device,(this->indexes.size()) * sizeof(IndexTemplate),defaultDesc);
        
        // 应该用默认堆的地址还是上传堆的地址?
        ibView.BufferLocation = uploadResource.GetDefaultResource()->GetGPUVirtualAddress();
        ibView.SizeInBytes = (UINT)(indexes.size() * sizeof(IndexTemplate));
        if constexpr (sizeof(IndexTemplate) == sizeof(uint16_t))
        {
            ibView.Format = DXGI_FORMAT_R16_UINT;
        }
        else
        {
            ibView.Format = DXGI_FORMAT_R32_UINT;
        }
        return true;
    }

    bool UploadToDefault(ComPtr<ID3D12GraphicsCommandList> commandList,D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_INDEX_BUFFER)
    {
        return uploadResource.UploadAndCopy(commandList,indexes.data(),indexes.size() * sizeof(IndexTemplate),finalState);
    }

    void Reset()
    {
        indexes.clear();
        uploadResource.ResetAll();//上传堆与默认堆都重置
    }

    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView()
    {
        return ibView;
    }
};

template <typename T>
class DynamicInstaceBuffer
{
private:
    ComPtr<ID3D12Resource1> uploadResource;
    D3D12_VERTEX_BUFFER_VIEW vbView{};
    UINT maxInstanceCount = 0;
    UINT instanceCount = 0;
    void* mappedData = nullptr;

public:
    bool Init(ComPtr<ID3D12Device> device, UINT maxCount)
    {
        if(!device || maxCount == 0)
        {
            std::print("DynamicInstaceBuffer Init parameter is invalid\n");
            return false;
        }

        Reset();
        maxInstanceCount = maxCount;

        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(T) * maxInstanceCount);

        if(FAILED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(uploadResource.GetAddressOf()))))
        {
            Reset();
            return false;
        }

        D3D12_RANGE readRange{0, 0};
        if(FAILED(uploadResource->Map(0, &readRange, &mappedData)))
        {
            Reset();
            return false;
        }

        vbView.BufferLocation = uploadResource->GetGPUVirtualAddress();
        vbView.StrideInBytes = sizeof(T);
        vbView.SizeInBytes = 0;
        return true;
    }

    bool Update(const std::vector<T>& data)
    {
        return Update(data.data(), static_cast<UINT>(data.size()));
    }

    bool Update(const T* data, UINT count)
    {
        if(!uploadResource || !mappedData || count > maxInstanceCount)
        {
            return false;
        }

        instanceCount = count;
        vbView.SizeInBytes = sizeof(T) * instanceCount;

        if(instanceCount == 0)
        {
            return true;
        }

        if(!data)
        {
            return false;
        }

        std::memcpy(mappedData, data, sizeof(T) * instanceCount);
        return true;
    }

    void Reset()
    {
        if(uploadResource && mappedData)
        {
            uploadResource->Unmap(0, nullptr);
        }

        uploadResource.Reset();
        vbView = {};
        maxInstanceCount = 0;
        instanceCount = 0;
        mappedData = nullptr;
    }

    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const
    {
        return vbView;
    }

    UINT GetInstanceCount() const
    {
        return instanceCount;
    }

};
