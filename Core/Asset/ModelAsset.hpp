#pragma once

#include "global.h"
#include "Shader/Shader.hpp"
#include "PipelineState/PipelineState.hpp"
#include "RootSignature/RootSignature.hpp"
#include <utility>

struct aiNode;
struct aiScene;
struct aiMesh;
struct aiMaterial;
class Texture;
class DescriptorHeap;

struct ModelVertex
{
    static constexpr int MaxBoneInfluence = 4;

    // tangent
    DirectX::XMFLOAT3 Tangent;
    // bitangent
    DirectX::XMFLOAT3 Bitangent;
    // bone indexes which will influence this vertex
    int m_BoneIDs[MaxBoneInfluence];
    // weights from each bone
    float m_Weights[MaxBoneInfluence];
};

struct TextureConfig
{
    UINT id;
    std::string type;
    std::string path;
};

// Mesh的封装只保存数据，渲染操作单独封装接口
struct ModelMesh
{
public:
    // vertices
    std::vector<DirectX::XMFLOAT3> vertices;
    std::vector<DirectX::XMFLOAT3> normal;
    std::vector<DirectX::XMFLOAT3> tangent;
    std::vector<DirectX::XMFLOAT3> bitangent;
    // texCoords
    std::vector<std::vector<DirectX::XMFLOAT3>> texCoords;
    std::vector<UINT> indices;
    UINT materialIndex;
    // vector<TextureConfig> textures;

    void ModelMeshInit(
        std::vector<DirectX::XMFLOAT3>&& vertices,
        std::vector<DirectX::XMFLOAT3>&& normal,
        std::vector<DirectX::XMFLOAT3>&& tangent,
        std::vector<DirectX::XMFLOAT3>&& bitangent,
        std::vector<std::vector<DirectX::XMFLOAT3>>&& texCoords,
        std::vector<UINT>&& indices,
        UINT materialIndex)
    {
        this->vertices = std::move(vertices);
        this->normal = std::move(normal);
        this->tangent = std::move(tangent);
        this->bitangent = std::move(bitangent);
        this->texCoords = std::move(texCoords);
        this->indices = std::move(indices);
        this->materialIndex = materialIndex;
    };
};

struct ModelNode
{
    std::string nodeName;
    std::weak_ptr<ModelNode> parent; // 子持有父的弱指针
    std::vector<std::shared_ptr<ModelNode>> children;
    std::vector<UINT> meshes; // 存的是mesh索引
    DirectX::XMFLOAT4X4 transformer; // 相对与父节点的偏移，我在想可不可以直接使用相对于scene的
};

class Model
{
public:
    // vector<Texture> textures;
    std::vector<std::shared_ptr<TextureConfig>> textures_load;
    std::vector<std::shared_ptr<Texture>> textures;
    std::vector<std::shared_ptr<ModelMesh>> modelMeshs;
    std::vector<std::shared_ptr<ModelNode>> modelNodes;
    std::shared_ptr<ModelNode> rootNode;
    std::string directory;
    bool gammaCorrection;

    virtual ~Model() = default;
    void ModelInit(std::string const path, bool gamma = false);
    bool LoadTextureResources(
        Microsoft::WRL::ComPtr<ID3D12Device> device,
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList,
        DescriptorHeap& srvHeap,
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource1>>& pendingUploads);

protected:
    virtual void processTextures(const aiScene* scene);
    virtual void processTexCoords(
        const aiMesh* mesh,
        const aiMaterial* material,
        ModelMesh& modelMesh);

private:
    bool LoadModel(std::string const& path);
    void processMesh(const aiScene* scene);
    void processNode(
        aiNode* node,
        const aiScene* scene,
        const std::shared_ptr<ModelNode>& parent = nullptr);
};


// //----------------------------RenderPass-------------------------------
// extern Shader defaultModelShader;
// extern RootSignature defaultModelRootSignature;
// extern PipelineState defaultModelPipelineState;
// extern void RenderPassModel(Model& model);
