#include "ModelAsset.hpp"

#include "assimp/Importer.hpp"
#include "assimp/GltfMaterial.h"
#include "assimp/material.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "Descriptor/Descriptor.hpp"
#include "Resource/LoadImage.hpp"
#include "Resource/Texture.hpp"

using namespace DirectX;

namespace
{
    XMFLOAT4X4 ConvertMatrix(const aiMatrix4x4 &m)
    {
        return XMFLOAT4X4(
            m.a1, m.a2, m.a3, m.a4,
            m.b1, m.b2, m.b3, m.b4,
            m.c1, m.c2, m.c3, m.c4,
            m.d1, m.d2, m.d3, m.d4);
    }

    UINT ReadTexCoordIndex(const aiMaterial *material, aiTextureType textureType, UINT textureIndex)
    {
        if (!material)
            return 0;

        unsigned int uvIndex = 0;
        if (material->Get(AI_MATKEY_UVWSRC(textureType, textureIndex), uvIndex) == AI_SUCCESS)
            return uvIndex;

        return 0;
    }

    std::shared_ptr<TextureConfig> ReadTextureConfig(
        const aiMaterial *material,
        aiTextureType textureType,
        UINT textureIndex,
        UINT id,
        const std::string &type,
        const std::string &directory)
    {
        auto texture = std::make_shared<TextureConfig>();
        texture->id = id;
        texture->type = type;

        if (!material)
            return texture;

        aiString texturePath;
        if (material->GetTexture(textureType, textureIndex, &texturePath) != AI_SUCCESS)
            return texture;

        const std::string relativePath = texturePath.C_Str();
        if (relativePath.empty())
            return texture;

        texture->path = directory.empty() ? relativePath : directory + "/" + relativePath;
        return texture;
    }

    std::vector<XMFLOAT3> CopyTexCoordSet(const aiMesh *mesh, UINT uvIndex)
    {
        std::vector<XMFLOAT3> texCoords;
        texCoords.reserve(mesh->mNumVertices);

        if (!mesh->HasTextureCoords(uvIndex))
            uvIndex = 0;

        if (!mesh->HasTextureCoords(uvIndex))
        {
            for (UINT vertexIndex = 0; vertexIndex < mesh->mNumVertices; vertexIndex++)
            {
                texCoords.push_back(XMFLOAT3(0.0f, 0.0f, 0.0f));
            }

            return texCoords;
        }

        for (UINT vertexIndex = 0; vertexIndex < mesh->mNumVertices; vertexIndex++)
        {
            const aiVector3D &texCoord = mesh->mTextureCoords[uvIndex][vertexIndex];
            texCoords.push_back(XMFLOAT3(texCoord.x, texCoord.y, texCoord.z));
        }

        return texCoords;
    }

    DXGI_FORMAT ChooseTextureFormat(const TextureConfig &config)
    {
        if (config.type == "normal" || config.type == "metallicRoughness")
            return DXGI_FORMAT_R8G8B8A8_UNORM;

        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
}

void Model::ModelInit(std::string const path, bool gamma)
{
    gammaCorrection = gamma;
    LoadModel(path);
}

bool Model::LoadTextureResources(
    Microsoft::WRL::ComPtr<ID3D12Device> device,
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList,
    DescriptorHeap& srvHeap,
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource1>>& pendingUploads)
{
    textures.clear();
    textures.resize(textures_load.size());

    TextureGenerator textureGenerator;
    bool success = true;

    for (size_t textureIndex = 0; textureIndex < textures_load.size(); ++textureIndex)
    {
        const auto& textureConfig = textures_load[textureIndex];
        if (!textureConfig || textureConfig->path.empty())
        {
            success = false;
            continue;
        }

        Imgae image;
        if (!LoadTexture(textureConfig->path, image, false) ||
            image.data.empty() ||
            image.width <= 0 ||
            image.height <= 0 ||
            image.isHdr)
        {
            success = false;
            continue;
        }

        auto srvDescriptor = srvHeap.GetFreeDescriptorHandle();
        if (!srvDescriptor)
        {
            success = false;
            continue;
        }

        const DXGI_FORMAT textureFormat = ChooseTextureFormat(*textureConfig);
        Texture2DCreateInfo createInfo{};
        createInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            textureFormat,
            static_cast<UINT64>(image.width),
            static_cast<UINT>(image.height),
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_NONE);
        createInfo.initialSubresource.pData = image.data.data();
        createInfo.initialSubresource.RowPitch = static_cast<LONG_PTR>(image.width * image.channels);
        createInfo.initialSubresource.SlicePitch = static_cast<LONG_PTR>(image.data.size());
        createInfo.srvCpuHandle = srvDescriptor->cpuHandle;
        createInfo.srvGpuHandle = srvDescriptor->gpuHandle;
        createInfo.finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        createInfo.srvDesc.Format = textureFormat;
        createInfo.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        createInfo.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        createInfo.srvDesc.Texture2D.MostDetailedMip = 0;
        createInfo.srvDesc.Texture2D.MipLevels = 1;
        createInfo.srvDesc.Texture2D.PlaneSlice = 0;
        createInfo.srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        TextureCreateResult createResult;
        if (!textureGenerator.CreateTexture2D(device, commandList, createInfo, createResult))
        {
            srvHeap.ReleaseDescriptorHandle(srvDescriptor->index);
            success = false;
            continue;
        }

        textures[textureIndex] = std::make_shared<Texture>(std::move(createResult.texture));
        if (createResult.uploadResource)
            pendingUploads.push_back(std::move(createResult.uploadResource));
    }

    return success;
}

bool Model::LoadModel(std::string const& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_ConvertToLeftHanded);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
        std::printf(std::format("ERROR::ASSIMP::{}\n", importer.GetErrorString()).c_str());
        return false;
    }

    // retrieve the directory path of the filepath
    directory = path.substr(0, path.find_last_of('/')); // 这个处理可能是由问题的，后面应该是要修改的

    textures_load.clear();
    textures.clear();
    modelMeshs.clear();
    modelNodes.clear();
    rootNode.reset();

    processTextures(scene);
    processMesh(scene);
    processNode(scene->mRootNode, scene);

    return true;
}

void Model::processMesh(const aiScene* scene)
{
    for (UINT meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++)
    {
        auto& mesh = scene->mMeshes[meshIndex];
        std::shared_ptr<ModelMesh> modelMeshP = std::make_shared<ModelMesh>();
        modelMeshP->vertices.reserve(mesh->mNumVertices);
        modelMeshP->normal.reserve(mesh->mNumVertices);
        modelMeshP->tangent.reserve(mesh->mNumVertices);
        modelMeshP->bitangent.reserve(mesh->mNumVertices);
        modelMeshP->indices.reserve(mesh->mNumFaces * 3);

        const bool hasNormals = mesh->HasNormals();
        const bool hasTangentsAndBitangents = mesh->HasTangentsAndBitangents();
        for (UINT vIndex = 0; vIndex < mesh->mNumVertices; vIndex++)
        {
            // vertex
            const aiVector3D& vertex = mesh->mVertices[vIndex];
            modelMeshP->vertices.push_back(XMFLOAT3(vertex.x, vertex.y, vertex.z));
            // normal
            if (hasNormals)
            {
                const aiVector3D& normal = mesh->mNormals[vIndex];
                modelMeshP->normal.push_back(XMFLOAT3(normal.x, normal.y, normal.z));
            }
            else
            {
                modelMeshP->normal.push_back(XMFLOAT3(0.0f, 0.0f, 0.0f));
            }

            if (hasTangentsAndBitangents)
            {
                const aiVector3D& tangent = mesh->mTangents[vIndex];
                const aiVector3D& bitangent = mesh->mBitangents[vIndex];
                modelMeshP->tangent.push_back(XMFLOAT3(tangent.x, tangent.y, tangent.z));
                modelMeshP->bitangent.push_back(XMFLOAT3(bitangent.x, bitangent.y, bitangent.z));
            }
            else
            {
                modelMeshP->tangent.push_back(XMFLOAT3(0.0f, 0.0f, 0.0f));
                modelMeshP->bitangent.push_back(XMFLOAT3(0.0f, 0.0f, 0.0f));
            }
        }

        const aiMaterial* material = nullptr;
        if (mesh->mMaterialIndex < scene->mNumMaterials)
            material = scene->mMaterials[mesh->mMaterialIndex];

        processTexCoords(mesh, material, *modelMeshP);

        // index
        for (UINT fIndex = 0; fIndex < mesh->mNumFaces; fIndex++)
        {
            auto& face = mesh->mFaces[fIndex];
            for (UINT iIndex = 0; iIndex < face.mNumIndices; iIndex++)
            {
                modelMeshP->indices.push_back(face.mIndices[iIndex]);
            }
        }
        modelMeshP->materialIndex = mesh->mMaterialIndex;
        modelMeshs.push_back(modelMeshP);
    }
}

void Model::processTextures(const aiScene* scene)
{
    textures_load.clear();
    textures_load.reserve(3);

    const aiMaterial* material = nullptr;
    if (scene->mNumMaterials > 0)
        material = scene->mMaterials[0];

    textures_load.push_back(ReadTextureConfig(material, aiTextureType_BASE_COLOR, 0, 0, "baseColor", directory));
    textures_load.push_back(ReadTextureConfig(material, aiTextureType_NORMALS, 0, 1, "normal", directory));
    textures_load.push_back(ReadTextureConfig(
        material,
        AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE,
        2,
        "metallicRoughness",
        directory));
}

void Model::processTexCoords(
    const aiMesh* mesh,
    const aiMaterial* material,
    ModelMesh& modelMesh)
{
    modelMesh.texCoords.clear();
    modelMesh.texCoords.reserve(3);

    const UINT baseColorUvIndex = ReadTexCoordIndex(material, aiTextureType_BASE_COLOR, 0);
    const UINT normalUvIndex = ReadTexCoordIndex(material, aiTextureType_NORMALS, 0);
    const UINT metallicRoughnessUvIndex = ReadTexCoordIndex(
        material,
        AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE);

    modelMesh.texCoords.push_back(CopyTexCoordSet(mesh, baseColorUvIndex));
    modelMesh.texCoords.push_back(CopyTexCoordSet(mesh, normalUvIndex));
    modelMesh.texCoords.push_back(CopyTexCoordSet(mesh, metallicRoughnessUvIndex));
}

void Model::processNode(
    aiNode* node,
    const aiScene* scene,
    const std::shared_ptr<ModelNode>& parent)
{
    auto modelNode = std::make_shared<ModelNode>();

    modelNode->nodeName = node->mName.C_Str();
    modelNode->parent = parent;
    modelNode->transformer = ConvertMatrix(node->mTransformation);

    modelNode->meshes.reserve(node->mNumMeshes);
    for (UINT i = 0; i < node->mNumMeshes; ++i)
    {
        UINT meshIndex = node->mMeshes[i];

        if (meshIndex < scene->mNumMeshes)
            modelNode->meshes.push_back(meshIndex);
    }

    modelNode->children.reserve(node->mNumChildren);

    if (parent)
        parent->children.push_back(modelNode);
    else
        rootNode = modelNode;

    modelNodes.push_back(modelNode);

    for (UINT i = 0; i < node->mNumChildren; ++i)
    {
        processNode(node->mChildren[i], scene, modelNode);
    }
}



//----------------------------RenderPass-------------------------------
void RenderPassModel(Model& model);
