#include "ModelGpuResource.hpp"
#include <memory>


DirectX::XMFLOAT3 ReadFloat3(
    const std::vector<DirectX::XMFLOAT3> &values,
    size_t index,
    DirectX::XMFLOAT3 fallback)
{
    return index < values.size() ? values[index] : fallback;
}

DirectX::XMFLOAT2 ReadUv(const ModelMesh &mesh, size_t index)
{
    if (mesh.texCoords.empty() || index >= mesh.texCoords[0].size())
        return DirectX::XMFLOAT2(0.0f, 0.0f);

    const DirectX::XMFLOAT3 &uv = mesh.texCoords[0][index];
    return DirectX::XMFLOAT2(uv.x, uv.y);
}

bool CreateBuffer(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *commandList,
    const void *data,
    UINT64 byteSize,
    D3D12_RESOURCE_STATES finalState,
    Microsoft::WRL::ComPtr<ID3D12Resource1> &defaultResource,
    Microsoft::WRL::ComPtr<ID3D12Resource1> &uploadResource)
{
    if (!device || !commandList || !data || byteSize == 0)
        return false;

    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    if (FAILED(device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(defaultResource.ReleaseAndGetAddressOf()))))
    {
        return false;
    }

    if (FAILED(device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(uploadResource.ReleaseAndGetAddressOf()))))
    {
        defaultResource.Reset();
        return false;
    }

    void *mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(uploadResource->Map(0, &readRange, &mapped)))
    {
        defaultResource.Reset();
        uploadResource.Reset();
        return false;
    }

    std::memcpy(mapped, data, static_cast<size_t>(byteSize));
    uploadResource->Unmap(0, nullptr);

    commandList->CopyBufferRegion(defaultResource.Get(), 0, uploadResource.Get(), 0, byteSize);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultResource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        finalState);
    commandList->ResourceBarrier(1, &barrier);
    return true;
}

DirectX::XMMATRIX LoadNodeMatrix(const std::shared_ptr<ModelNode> &node)
{
    if (!node)
        return DirectX::XMMatrixIdentity();

    return DirectX::XMLoadFloat4x4(&node->transformer);
}

DirectX::XMFLOAT3 TransformPosition(const DirectX::XMFLOAT3 &value, DirectX::FXMMATRIX transform)
{
    DirectX::XMFLOAT3 result{};
    DirectX::XMStoreFloat3(
        &result,
        DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&value), transform));
    return result;
}

DirectX::XMFLOAT3 TransformDirection(
    const DirectX::XMFLOAT3 &value,
    DirectX::FXMMATRIX transform,
    const DirectX::XMFLOAT3 &fallback)
{
    DirectX::XMVECTOR direction = DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&value), transform);
    if (DirectX::XMVector3Equal(direction, DirectX::XMVectorZero()))
        return fallback;

    DirectX::XMFLOAT3 result{};
    DirectX::XMStoreFloat3(&result, DirectX::XMVector3Normalize(direction));
    return result;
}

bool CreateMesh(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *commandList,
    const ModelMesh &cpuMesh,
    DirectX::FXMMATRIX nodeTransform,
    ModelGpuMesh &gpuMesh)
{
    if (cpuMesh.vertices.empty() || cpuMesh.indices.empty())
        return false;

    std::vector<ModelGpuVertex> vertices;
    vertices.reserve(cpuMesh.vertices.size());

    for (size_t i = 0; i < cpuMesh.vertices.size(); ++i)
    {
        ModelGpuVertex vertex{};
        vertex.position = TransformPosition(cpuMesh.vertices[i], nodeTransform);
        vertex.normal = TransformDirection(
            ReadFloat3(cpuMesh.normal, i, DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)),
            nodeTransform,
            DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
        vertex.tangent = TransformDirection(
            ReadFloat3(cpuMesh.tangent, i, DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f)),
            nodeTransform,
            DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f));
        vertex.bitangent = TransformDirection(
            ReadFloat3(cpuMesh.bitangent, i, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)),
            nodeTransform,
            DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
        vertex.uv = ReadUv(cpuMesh, i);
        vertices.push_back(vertex);
    }

    const UINT64 vertexBytes = static_cast<UINT64>(vertices.size() * sizeof(ModelGpuVertex));
    const UINT64 indexBytes = static_cast<UINT64>(cpuMesh.indices.size() * sizeof(UINT));

    if (!CreateBuffer(
            device,
            commandList,
            vertices.data(),
            vertexBytes,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            gpuMesh.vertexBuffer,
            gpuMesh.vertexUpload))
    {
        return false;
    }

    if (!CreateBuffer(
            device,
            commandList,
            cpuMesh.indices.data(),
            indexBytes,
            D3D12_RESOURCE_STATE_INDEX_BUFFER,
            gpuMesh.indexBuffer,
            gpuMesh.indexUpload))
    {
        return false;
    }

    gpuMesh.vertexView.BufferLocation = gpuMesh.vertexBuffer->GetGPUVirtualAddress();
    gpuMesh.vertexView.StrideInBytes = sizeof(ModelGpuVertex);
    gpuMesh.vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);

    gpuMesh.indexView.BufferLocation = gpuMesh.indexBuffer->GetGPUVirtualAddress();
    gpuMesh.indexView.Format = DXGI_FORMAT_R32_UINT;
    gpuMesh.indexView.SizeInBytes = static_cast<UINT>(indexBytes);
    gpuMesh.indexCount = static_cast<UINT>(cpuMesh.indices.size());
    gpuMesh.materialIndex = cpuMesh.materialIndex;
    return true;
}

bool CreateMeshFromIndex(
    ModelGpuResource &resource,
    ID3D12Device *device,
    ID3D12GraphicsCommandList *commandList,
    const Model &cpuModel,
    UINT meshIndex,
    DirectX::FXMMATRIX nodeTransform)
{
    if (meshIndex >= cpuModel.modelMeshs.size() || !cpuModel.modelMeshs[meshIndex])
        return true;

    ModelGpuMesh gpuMesh;
    if (!CreateMesh(device, commandList, *cpuModel.modelMeshs[meshIndex], nodeTransform, gpuMesh))
        return false;

    resource.meshes.push_back(std::move(gpuMesh));
    return true;
}

bool CreateNodeMeshes(
    ModelGpuResource &resource,
    ID3D12Device *device,
    ID3D12GraphicsCommandList *commandList,
    const Model &cpuModel,
    const std::shared_ptr<ModelNode> &node,
    DirectX::FXMMATRIX parentTransform)
{
    if (!node)
        return true;

    DirectX::XMMATRIX nodeTransform = LoadNodeMatrix(node) * parentTransform;
    for (UINT meshIndex : node->meshes)
    {
        if (!CreateMeshFromIndex(resource, device, commandList, cpuModel, meshIndex, nodeTransform))
            return false;
    }

    for (const std::shared_ptr<ModelNode> &child : node->children)
    {
        if (!CreateNodeMeshes(resource, device, commandList, cpuModel, child, nodeTransform))
            return false;
    }

    return true;
}

void DrawMeshInstanced(
    const ModelGpuResource &resource,
    ID3D12GraphicsCommandList *cmd,
    UINT meshIndex,
    UINT instanceCount)
{
    if (meshIndex >= resource.meshes.size() || instanceCount == 0)
        return;

    const ModelGpuMesh &mesh = resource.meshes[meshIndex];
    cmd->IASetVertexBuffers(0, 1, &mesh.vertexView);
    cmd->IASetIndexBuffer(&mesh.indexView);
    cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, 0, 0, 0);
}

void DrawMeshesInstanced(const ModelGpuResource &resource, ID3D12GraphicsCommandList *cmd, UINT instanceCount)
{
    for (UINT meshIndex = 0; meshIndex < resource.meshes.size(); ++meshIndex)
    {
        DrawMeshInstanced(resource, cmd, meshIndex, instanceCount);
    }
}
std::shared_ptr<ModelGpuResource> CreateModelGpuResource()
{
    return std::make_shared<ModelGpuResource>();
}

bool CreateModelGpuResourceFromModel(
    ModelGpuResource& gpuResource,
    Model& model,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    if (!device || !commandList)
        return false;

    gpuResource.sourceModel = &model;
    gpuResource.meshes.clear();
    gpuResource.meshes.reserve(model.modelMeshs.size());

    if (model.rootNode)
    {
        if (!CreateNodeMeshes(
                gpuResource,
                device,
                commandList,
                model,
                model.rootNode,
                DirectX::XMMatrixIdentity()))
        {
            gpuResource.meshes.clear();
            return false;
        }
    }
    else
    {
        for (UINT meshIndex = 0; meshIndex < model.modelMeshs.size(); ++meshIndex)
        {
            if (!CreateMeshFromIndex(
                    gpuResource,
                    device,
                    commandList,
                    model,
                    meshIndex,
                    DirectX::XMMatrixIdentity()))
            {
                gpuResource.meshes.clear();
                return false;
            }
        }
    }

    return !gpuResource.meshes.empty();
}

void ReleaseModelGpuResourceUploadResources(ModelGpuResource& gpuResource)
{
    for (ModelGpuMesh& mesh : gpuResource.meshes)
    {
        mesh.vertexUpload.Reset();
        mesh.indexUpload.Reset();
    }
}

void DrawModelGpuResource(ModelGpuResource& gpuResource, ID3D12GraphicsCommandList* commandList)
{
    DrawModelGpuResourceInstanced(gpuResource, commandList, 1);
}

void DrawModelGpuResourceInstanced(
    ModelGpuResource& gpuResource,
    ID3D12GraphicsCommandList* commandList,
    unsigned int instanceCount)
{
    if (!commandList || instanceCount == 0)
        return;

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    DrawMeshesInstanced(gpuResource, commandList, instanceCount);
}
