#pragma once
#include "global.h"
// #include ""
using Microsoft::WRL::ComPtr;

struct ComputePipelineDesc
{
    ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    D3D12_SHADER_BYTECODE cs{};
};

struct GraphicsPipelineDesc
{
    ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    D3D12_SHADER_BYTECODE vs{};
    D3D12_SHADER_BYTECODE gs{};
    D3D12_SHADER_BYTECODE ps{};
    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    D3D12_RASTERIZER_DESC rasterizer{};
    D3D12_BLEND_DESC blend{};
    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    DXGI_FORMAT rtvFormats[8]{};
    UINT numRenderTargets = 0;
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_SAMPLE_DESC sampleDesc{1, 0};
    UINT sampleMask = UINT_MAX;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ibStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
};

class PipelineState
{
private:
    ComPtr<ID3D12PipelineState> pipelineState = nullptr;
    // ComPtr<ID3D12RootSignature> rootSignature = nullptr;

public:
    bool CreateComputePipelineState(ComPtr<ID3D12Device> device, const ComputePipelineDesc desc)
    {
        if (!device || !desc.rootSignature || desc.cs.pShaderBytecode == nullptr || desc.cs.BytecodeLength == 0)
        {
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = desc.rootSignature.Get();
        psoDesc.CS = desc.cs;
        return SUCCEEDED(device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(pipelineState.GetAddressOf())
        ));
    }

    bool CreateGraphicsPipelineState(ComPtr<ID3D12Device> device,GraphicsPipelineDesc desc)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = desc.rootSignature.Get();
        psoDesc.VS = desc.vs;
        psoDesc.GS = desc.gs;
        psoDesc.PS = desc.ps;
        psoDesc.InputLayout = desc.inputLayout;
        psoDesc.RasterizerState = desc.rasterizer;
        psoDesc.BlendState = desc.blend;
        psoDesc.DepthStencilState = desc.depthStencil;
        psoDesc.PrimitiveTopologyType = desc.topologyType;
        psoDesc.NumRenderTargets = desc.numRenderTargets;
        psoDesc.DSVFormat = desc.dsvFormat;
        psoDesc.SampleDesc = desc.sampleDesc;
        psoDesc.SampleMask = desc.sampleMask;
        psoDesc.IBStripCutValue = desc.ibStripCutValue;

        for(UINT i = 0;i < desc.numRenderTargets; i++)
        {
            psoDesc.RTVFormats[i] = desc.rtvFormats[i];
        }
        return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(pipelineState.GetAddressOf())));
    }

    const ComPtr<ID3D12PipelineState> GetPipelineStateComPtr() const
    {
        return pipelineState;
    }

    bool IsValid() const
    {
        return pipelineState != nullptr;
    }

    void Reset()
    {
        pipelineState.Reset();
    }
};

