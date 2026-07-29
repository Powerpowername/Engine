#pragma once
#include "global.h"
#include "Camera/QuaternionCamera.hpp"
#include "Shader/Shader.hpp"
#include "Resource/ConstantBuffer.hpp"
#include "PipelineState/PipelineState.hpp"
#include "Command/Command.hpp"
#include "Resource/Texture.hpp"

using Microsoft::WRL::ComPtr;

class CascadeShadowMapCalculate
{
public:
    bool Initialize(std::shared_ptr<QuaternionCamera> camera,DirectX::XMFLOAT3 lightDirection)
    {
        if (!camera) return false;
        cameraNearPlane = camera->GetNearZ();
        cameraFarPlane = camera->GetFarZ();
        XMStoreFloat4x4(&cameraViewMatrix, camera->GetView());
        shadowCascadeLevels = { cameraFarPlane / 50.0f, cameraFarPlane / 25.0f, cameraFarPlane / 10.0f, cameraFarPlane / 2.0f };
        XMStoreFloat3(&this->lightDirection, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&lightDirection)));
        UpdateLightSpaceMatrices(camera);
        return true;
    }

    void UpdateCameraConfig(std::shared_ptr<QuaternionCamera> camera)
    {
        if (!camera) return;
        cameraNearPlane = camera->GetNearZ();
        cameraFarPlane = camera->GetFarZ();
        XMStoreFloat4x4(&cameraViewMatrix, camera->GetView());
        UpdateLightSpaceMatrices(camera);
    }

    void UpdateCameraLightConfig(std::shared_ptr<QuaternionCamera> camera, DirectX::XMFLOAT3 lightDirection)
    {
        if (!camera) return;
        cameraNearPlane = camera->GetNearZ();
        cameraFarPlane = camera->GetFarZ();
        XMStoreFloat4x4(&cameraViewMatrix, camera->GetView());
        XMStoreFloat3(&this->lightDirection, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&lightDirection)));
        UpdateLightSpaceMatrices(camera);
    }

    std::vector<DirectX::XMFLOAT4X4>& GetLightViewMatrices()
    {
        return lightViewMatrices;
    }

    const std::vector<float>& GetShadowCascadeLevels() const
    {
        return shadowCascadeLevels;
    }

private:
    std::vector<DirectX::XMFLOAT4> GetFrustumCornersWorldSpace(const DirectX::XMFLOAT4X4& projview)
    {
        DirectX::XMMATRIX inv;
        inv = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&projview));
        std::vector<DirectX::XMFLOAT4> frustumCorners;
        for (unsigned int x = 0; x < 2; ++x)
        {
            for (unsigned int y = 0; y < 2; ++y)
            {
                for (unsigned int z = 0; z < 2; ++z)
                {
                    DirectX::XMVECTOR ndc = DirectX::XMVectorSet(
                        2.0f * x - 1.0f,
                        2.0f * y - 1.0f,
                        static_cast<float>(z),
                        1.0f);

                    DirectX::XMVECTOR ptVec = DirectX::XMVector4Transform(ndc, inv);
                    DirectX::XMVECTOR w = DirectX::XMVectorReplicate(DirectX::XMVectorGetW(ptVec));
                    DirectX::XMVECTOR worldPt = DirectX::XMVectorDivide(ptVec, w);

                    DirectX::XMFLOAT4 pt;
                    DirectX::XMStoreFloat4(&pt, worldPt);
                    frustumCorners.push_back(pt);
                }
            }
        }

        return frustumCorners;
    }

    std::vector<DirectX::XMFLOAT4> GetFrustumCornersWorldSpace(const DirectX::XMFLOAT4X4 proj, const DirectX::XMFLOAT4X4 view)
    {
        const DirectX::XMMATRIX projMatrix = DirectX::XMLoadFloat4x4(&proj);
        const DirectX::XMMATRIX viewMatrix = DirectX::XMLoadFloat4x4(&view);
        DirectX::XMFLOAT4X4 param;
        DirectX::XMStoreFloat4x4(&param, DirectX::XMMatrixMultiply(viewMatrix, projMatrix));
        return GetFrustumCornersWorldSpace(param);
    }

    DirectX::XMFLOAT4X4 UpdateLightSpaceMatrice(std::shared_ptr<QuaternionCamera> camera,const float nearPlane, const float farPlane)
    {
        const auto proj = DirectX::XMMatrixPerspectiveFovLH(
            camera->GetFovYRadians(),
            camera->GetAspectRatio(),
            nearPlane,
            farPlane);
        DirectX::XMFLOAT4X4 projFloat4x4;
        DirectX::XMStoreFloat4x4(&projFloat4x4, proj);
        const auto corners = GetFrustumCornersWorldSpace(projFloat4x4, cameraViewMatrix);
        DirectX::XMVECTOR center = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);

        for (const auto& corner : corners)
        {
            center = DirectX::XMVectorAdd(center, DirectX::XMLoadFloat4(&corner));
        }

        center = DirectX::XMVectorScale(center, 1.0f / static_cast<float>(corners.size()));
        DirectX::XMVECTOR lightDir = DirectX::XMLoadFloat3(&lightDirection);
        const auto lightView = DirectX::XMMatrixLookAtLH(
            DirectX::XMVectorSubtract(center, DirectX::XMVectorScale(lightDir, 100.0f)),
            center,
            DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for(const auto& v : corners)
        {
            const auto trf = DirectX::XMVector4Transform(DirectX::XMLoadFloat4(&v), lightView);
            DirectX::XMFLOAT4 trfBuffer;
            DirectX::XMStoreFloat4(&trfBuffer, trf);
            minX = std::min(minX, trfBuffer.x);
            maxX = std::max(maxX, trfBuffer.x);
            minY = std::min(minY, trfBuffer.y);
            maxY = std::max(maxY, trfBuffer.y);
            minZ = std::min(minZ, trfBuffer.z);
            maxZ = std::max(maxZ, trfBuffer.z);
        }

        constexpr float zMult = 10.0f;
        if (minZ < 0)
        {
            minZ *= zMult;
        }
        else
        {
            minZ /= zMult;
        }
        if (maxZ < 0)
        {
            maxZ /= zMult;
        }
        else
        {
            maxZ *= zMult;
        }
        const auto lightProjection = DirectX::XMMatrixOrthographicOffCenterLH(
            minX, maxX,
            minY, maxY,
            minZ, maxZ);
        DirectX::XMFLOAT4X4 ret;
        DirectX::XMStoreFloat4x4(&ret, DirectX::XMMatrixMultiply(lightView, lightProjection));
        return ret;
    }

    void UpdateLightSpaceMatrices(std::shared_ptr<QuaternionCamera> camera)
    {
        lightViewMatrices.clear();
        for(int i = 0; i < 5; i++)
        {
            if(i == 0)
            {
                lightViewMatrices.push_back(UpdateLightSpaceMatrice(camera, cameraNearPlane, shadowCascadeLevels[i]));
            }
            else if(i == 4)
            {
                lightViewMatrices.push_back(UpdateLightSpaceMatrice(camera, shadowCascadeLevels[i - 1], cameraFarPlane));
            }
            else
            {
                lightViewMatrices.push_back(UpdateLightSpaceMatrice(camera, shadowCascadeLevels[i - 1], shadowCascadeLevels[i]));
            }
        }
    }

private:
    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 1000.0f;
    std::vector<float> shadowCascadeLevels;
    DirectX::XMFLOAT3 lightDirection;
    std::vector<DirectX::XMFLOAT4X4> lightViewMatrices;
    DirectX::XMFLOAT4X4 cameraViewMatrix;
};

struct CascadeRenderConfig
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator> commandAllocator;

    D3D12_GPU_VIRTUAL_ADDRESS lightSpaceMatricesCbAddress = 0;

    FLOAT clearColor[4]{0.0f,0.0f,0.0f,0.0f};
    UINT numRects = 0;
    D3D12_RECT* rects = nullptr;
};

struct RenderResourceConifg
{
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;

    DirectX::XMFLOAT4X4 model{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    UINT indexCountPerInstance;
    UINT instanceCount;
    UINT startIndexLocation;
    UINT baseVertexLocation;
    UINT startInstanceLocation;
};

class CascadeShadowMap
{
private:
    std::shared_ptr<DepthTexture> cascadeShadowMap;
    UINT cascadeCount = 5;

    std::shared_ptr<DescriptorHandle> dsvDescriptor;
    std::shared_ptr<DescriptorHandle> srvDescriptor;
    CascadeRenderConfig renderConfig;

public:
    virtual bool intialize(CascadeRenderConfig renderConfig,D3D12_RESOURCE_DESC depthResourceDesc,DescriptorHeap& dsvHeap,DescriptorHeap& srvHeap,
        UINT width,UINT height,D3D12_CLEAR_VALUE clearValue)
    {
        this->renderConfig = renderConfig;
        (void)depthResourceDesc;

        DepthTextureCreateInfo depthTextureCreateInfo;
        depthTextureCreateInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS, width, height, static_cast<UINT16>(cascadeCount), 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );
        depthTextureCreateInfo.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        depthTextureCreateInfo.hasClearValue = true;
        depthTextureCreateInfo.clearValue = clearValue;

        dsvDescriptor = dsvHeap.GetFreeDescriptorHandle();
        if(!dsvDescriptor)
            return false;

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Flags = D3D12_DSV_FLAG_NONE;
        dsv.Texture2DArray.MipSlice = 0;
        dsv.Texture2DArray.FirstArraySlice = 0;
        dsv.Texture2DArray.ArraySize = cascadeCount;
        depthTextureCreateInfo.mainDsvDesc = dsv;
        depthTextureCreateInfo.mainDsvCpuHandle = dsvDescriptor->cpuHandle;

        depthTextureCreateInfo.srvDesc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2DArray(
            DXGI_FORMAT_R32_FLOAT, cascadeCount, 1, 0, 0, 0, 0, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
        );
        srvDescriptor = srvHeap.GetFreeDescriptorHandle();
        if(!srvDescriptor)
            return false;
        depthTextureCreateInfo.srvCpuHandle = srvDescriptor->cpuHandle;
        depthTextureCreateInfo.srvGpuHandle = srvDescriptor->gpuHandle;
        return DepthTextureGenerator::CreateCascadeDepthTextureArray(this->renderConfig.device, depthTextureCreateInfo, cascadeShadowMap, cascadeCount);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvCpuHandle() const
    {
        return cascadeShadowMap ? cascadeShadowMap->GetDsvCpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle() const
    {
        return cascadeShadowMap ? cascadeShadowMap->GetSrvCpuHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const
    {
        return cascadeShadowMap ? cascadeShadowMap->GetSrvGpuHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    bool IsValid() const
    {
        return cascadeShadowMap != nullptr;
    }

    ID3D12Resource1* GetDepthResource() const
    {
        return cascadeShadowMap ? cascadeShadowMap->GetResource() : nullptr;
    }

    D3D12_RESOURCE_STATES GetDepthCurrentState() const
    {
        return cascadeShadowMap ? cascadeShadowMap->GetCurrentState() : D3D12_RESOURCE_STATE_COMMON;
    }

    void SetDepthCurrentState(D3D12_RESOURCE_STATES state)
    {
        if(cascadeShadowMap)
        {
            cascadeShadowMap->SetCurrentState(state);
        }
    }

    virtual void Render(std::vector<RenderResourceConifg>& renderReosurceConfig)
    {
        if(!cascadeShadowMap)
            return;

        ComPtr<ID3D12GraphicsCommandList> commandList = renderConfig.commandList;
        ComPtr<ID3D12PipelineState> pso = renderConfig.pso;
        ComPtr<ID3D12RootSignature> rootSignature = renderConfig.rootSignature;
        if(cascadeShadowMap->GetCurrentState() != D3D12_RESOURCE_STATE_DEPTH_WRITE)
        {
            auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
                cascadeShadowMap->GetResource(),
                cascadeShadowMap->GetCurrentState(),
                D3D12_RESOURCE_STATE_DEPTH_WRITE);
            commandList->ResourceBarrier(1, &transition);
            cascadeShadowMap->SetCurrentState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
        commandList->ClearDepthStencilView(
            cascadeShadowMap->GetDsvCpuHandle(),
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            renderConfig.numRects,
            renderConfig.rects);

        D3D12_CPU_DESCRIPTOR_HANDLE dsv = cascadeShadowMap->GetDsvCpuHandle();
        commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->SetPipelineState(pso.Get());
        commandList->SetGraphicsRootConstantBufferView(1, renderConfig.lightSpaceMatricesCbAddress);
        for(const auto& it : renderReosurceConfig)
        {
            commandList->SetGraphicsRoot32BitConstants(0, 16, &it.model, 0);
            commandList->IASetVertexBuffers(0, 1, &it.vertexBufferView);
            commandList->IASetIndexBuffer(&it.indexBufferView);
            commandList->DrawIndexedInstanced(
                it.indexCountPerInstance,
                it.instanceCount,
                it.startIndexLocation,
                it.baseVertexLocation,
                it.startInstanceLocation);
        }
        auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
            cascadeShadowMap->GetResource(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &transition);
        cascadeShadowMap->SetCurrentState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
};

class CascadeShadowMapShader
{
private:
    Shader cascadeShadowMapShader;

public:
    bool Initialize(const std::wstring& shaderFilePath)
    {
        return cascadeShadowMapShader.CompileGraphicsFromFile(
            shaderFilePath,
            Shader::StageCompileDesc("VSMain", "vs_5_1", "VS"),
            Shader::StageCompileDesc("GSMain", "gs_5_1", "GS"),
            Shader::StageCompileDesc("PSMain", "ps_5_1", "PS"));
    }

    bool Initialize(
        const std::wstring& shaderFilePath,
        const Shader::StageCompileDesc& vsStage,
        const Shader::StageCompileDesc& gsStage,
        const Shader::StageCompileDesc& psStage)
    {
        return cascadeShadowMapShader.CompileGraphicsFromFile(shaderFilePath, vsStage, gsStage, psStage);
    }

    D3D12_SHADER_BYTECODE GetVsShaderByteCode()
    {
        return cascadeShadowMapShader.GetVsShaderByteCode();
    }

    D3D12_SHADER_BYTECODE GetGsShaderByteCode()
    {
        return cascadeShadowMapShader.GetGsShaderByteCode();
    }

    D3D12_SHADER_BYTECODE GetPsShaderByteCode()
    {
        return cascadeShadowMapShader.GetPsShaderByteCode();
    }
};
