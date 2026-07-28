#pragma once
#include "global.h"

using Microsoft::WRL::ComPtr;

/// @brief 
class RootSignature
{
private:
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> tableRanges;
    std::vector<D3D12_ROOT_PARAMETER> parameters;
    std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
    ComPtr<ID3D12RootSignature> rootSignature = nullptr;
public:
    /// @brief 直接根常量：把少量 32-bit 常量直接塞进 root 对应 HLSL：少量常量寄存器
    /// @param num32BitValues 
    /// @param shaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddRootConstants(UINT num32BitValues,
                          UINT shaderRegister,
                          D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
                          UINT registerSpace = 0)
    {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.Num32BitValues  = num32BitValues;
        param.Constants.ShaderRegister = shaderRegister;
        param.Constants.RegisterSpace = registerSpace;
        param.ShaderVisibility = visibility;
        parameters.push_back(param);
    }

    /// @brief 直接根 CBV：把一个常量缓冲 GPU 地址直接绑定到 root 对应 HLSL：cbuffer xxx : register(b#)
    /// @param visibility 
    /// @param registerSpace 
    void AddRootCBV(UINT shaderRegister,
                    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
                    UINT registerSpace = 0)
    {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        param.Descriptor.ShaderRegister = shaderRegister;
        param.Descriptor.RegisterSpace = registerSpace;
        param.ShaderVisibility = visibility;
        parameters.push_back(param);
    }

    /// @brief 直接根 SRV：把一个只读资源 GPU 地址直接绑定到 root 对应 HLSL：特殊场景下的 SRV register(t#)
    /// @param shaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddRootSRV(
        UINT shaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        param.Descriptor.ShaderRegister = shaderRegister;
        param.Descriptor.RegisterSpace = registerSpace;
        param.ShaderVisibility = visibility;
        parameters.push_back(param);
    }

    /// @brief 直接根 UAV：把一个可写资源 GPU 地址直接绑定到 root 对应 HLSL：特殊场景下的 UAV register(u#)
    /// @param shaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddRootUAV(
        UINT shaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        param.Descriptor.ShaderRegister = shaderRegister;
        param.Descriptor.RegisterSpace = registerSpace;
        param.ShaderVisibility = visibility;
        parameters.push_back(param);
    }

    /// @brief 通用 Descriptor Table, rangeType 可以是 CBV / SRV / UAV / SAMPLER
    /// @param rangeType 
    /// @param numDescriptors 
    /// @param baseShaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddDescriptorTable(
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
        UINT numDescriptors,
        UINT baseShaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        tableRanges.push_back({});
        auto& ranges = tableRanges.back();
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = rangeType;
        range.NumDescriptors = numDescriptors;
        range.BaseShaderRegister = baseShaderRegister;
        range.RegisterSpace = registerSpace;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        ranges.push_back(range);

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.size());
        param.DescriptorTable.pDescriptorRanges = ranges.data();
        param.ShaderVisibility = visibility;
        parameters.push_back(param);
    }

    /// @brief CBV 表：对应 b# 起始的一段 CBV
    /// @param numDescriptors 
    /// @param baseShaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddCBVTable(
        UINT numDescriptors,
        UINT baseShaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        AddDescriptorTable(
            D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
            numDescriptors,
            baseShaderRegister,
            visibility,
            registerSpace);
    }

    /// @brief SRV 表：对应 t# 起始的一段 SRV
    /// @param numDescriptors 
    /// @param baseShaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddSRVTable(
        UINT numDescriptors,
        UINT baseShaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        AddDescriptorTable(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            numDescriptors,
            baseShaderRegister,
            visibility,
            registerSpace);
    }

    /// @brief UAV 表：对应 u# 起始的一段 UAV
    /// @param numDescriptors 
    /// @param baseShaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddUAVTable(
        UINT numDescriptors,
        UINT baseShaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        AddDescriptorTable(
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
            numDescriptors,
            baseShaderRegister,
            visibility,
            registerSpace);
    }

    /// @brief Sampler 表：对应 s# 起始的一段 Sampler
    /// @param numDescriptors 
    /// @param baseShaderRegister 
    /// @param visibility 
    /// @param registerSpace 
    void AddSamplerTable(
        UINT numDescriptors,
        UINT baseShaderRegister,
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
        UINT registerSpace = 0)
    {
        AddDescriptorTable(
            D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
            numDescriptors,
            baseShaderRegister,
            visibility,
            registerSpace);
    }

    /// @brief 静态采样器：不走 sampler heap，直接写死在 root signature 里
    /// @param samplerDesc 
    void AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC& samplerDesc)
    {
        staticSamplers.push_back(samplerDesc);
    }

    bool CreateRootSignature(ComPtr<ID3D12Device> device,D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)
    {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(parameters.size());
        desc.pParameters = parameters.empty() ? nullptr : parameters.data();
        desc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
        desc.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();
        desc.Flags = flags;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errBlob;
        
        if(FAILED(D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sigBlob,
            &errBlob)))
        {
            if(errBlob)
            {
                std::print("D3D12SerializeRootSignature failed: {}\n",
                           static_cast<const char*>(errBlob->GetBufferPointer()));
            }
            return false;
        }
        
        if(FAILED(device->CreateRootSignature(
            0,
            sigBlob->GetBufferPointer(),
            sigBlob->GetBufferSize(),
            IID_PPV_ARGS(rootSignature.GetAddressOf()))))
        {
            std::print("CreateRootSignature failed\n");
            return false;
        }
        return true;
    }

    const ComPtr<ID3D12RootSignature> GetRootSignatureComPtr() const
    {
        return rootSignature;
    }

    bool IsValid() const
    {
        return rootSignature != nullptr;
    }

    void Reset()
    {
        parameters.clear();
        tableRanges.clear();
        staticSamplers.clear();
        rootSignature.Reset();
    }


};
