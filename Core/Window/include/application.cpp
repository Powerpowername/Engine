#include "application.hpp"
#include "Auxi/Math.hpp"
// variable
shared_ptr<Device> appDevice;
shared_ptr<CommandAllocator> appCommandAllocator;
shared_ptr<CommandQueue> appCommandQueue;
shared_ptr<CommandList> appCommandList;
shared_ptr<SwapChain> appSwapChain;
shared_ptr<DescriptorHeap> appRtvDescHeap;
shared_ptr<DescriptorHeap> appDsvDescHeap;
shared_ptr<DescriptorHeap> appTerrianRenderCbvUavSrvDescHeap;
shared_ptr<DescriptorHeap> appComputeCbvUavSrvDescHeap;
shared_ptr<DepthTexture> appMainDepthTexture;
shared_ptr<QuaternionCamera> appCamera;
shared_ptr<DXEngine::WindowsSystem> windowsSystem;
shared_ptr<ConstantBuffer> appSceneCB;
shared_ptr<ConstantBuffer> appComputeCB;
shared_ptr<Shader> terrianShader;
shared_ptr<Shader> terrianComputeShader;
// rootSignature
shared_ptr<RootSignature> appTerrianRootSignature;
shared_ptr<RootSignature> appTerrianComputeRootSignature;
shared_ptr<RootSignature> appTerrianGridRootSignature;
// pso
shared_ptr<PipelineState> appTerrianPso;
shared_ptr<PipelineState> appTerrianComputePso;
shared_ptr<PipelineState> appTerrianGridPso;
//clipmap
shared_ptr<GroundMesh> appGroundMesh;
shared_ptr<Heightmap> appHeightmap;
shared_ptr<VertexBuffer<XMUINT2>> appTerrianVertex;
shared_ptr<IndexBuffer<uint16_t>> appTerrianIndex;
shared_ptr<DynamicInstaceBuffer<InstanceData>> appTerrianUpdateInstanceData;
shared_ptr<TerrainData> appTerrianData;
Texture appSourceHeightTexture;
Texture appClipmapHeightTexture;


SceneConstants sc;
ComputeConstants cc;
// UINT cvbNum = 0;
D3D12_VIEWPORT viewport{};
D3D12_RECT scissor{};
bool hasLastMousePosition = false;
POINT lastMousePosition = {};
uint32_t appPendingUpdateCount = 0;

// Model
shared_ptr<Model> appQuvierTree;
// PSO
// 我在考虑一个问题，可能Model不需要自己得Pso，因为他是应该直接在地形绘制的时候直接进去画就行了
shared_ptr<PipelineState> appModelPso;
// RootSignature
shared_ptr<RootSignature> appModelRootSignature;
// DescriptorHeap
shared_ptr<DescriptorHeap> appModeCbvUavSrvDescHeap;
// Shader
shared_ptr<Shader> appModelShader;
// ConstantBuffer
ModelSceneCB mscb;
shared_ptr<ConstantBuffer> appModelSceneCB;
// ModelGpuResource
shared_ptr<ModelGpuResource> appQuvierTreeGpuResource;
// QuvierInstanceData
shared_ptr<DynamicInstaceBuffer<QuvierTreeModelInstanceData>> appQuvierTreeInstanceData;
std::vector<Microsoft::WRL::ComPtr<ID3D12Resource1>> appQuvierTreeTextureUploads;
vector<QuvierTreeModelInstanceData> quvierTreeInstanceDatas;
vector<QuvierTreeModelInstanceData> filterTreeInstanceDatas;

// Light
shared_ptr<Light> light;

// Cascade Shadow
CascadeShadowConstants csc;
shared_ptr<ConstantBuffer> appCascadeShadowCB;
shared_ptr<CascadeShadowMapCalculate> cascadeShadowMapCalculate;
shared_ptr<CascadeShadowMap> cascadeShadowMap;
// 只存 cascade shadow depth texture array 的 SRV，后续主渲染采样阴影图用。
shared_ptr<DescriptorHeap> appCascadeShadowDepthSrvDescHeap; // 原始 cascade shadow SRV 所在 heap，作为 descriptor 拷贝源。
// 只存 cascade shadow depth texture array 的 DSV，shadow pass 写深度用。
shared_ptr<DescriptorHeap> appCascadeShadowDepthDsvDescHeap; // cascade shadow DSV 所在 heap，shadow pass 写深度用。
shared_ptr<DescriptorHandle> appTerrianCascadeShadowSrvDesc; // 原始 shadow SRV 拷贝到地形主渲染 heap 后的位置。
shared_ptr<DescriptorHandle> appModelCascadeShadowSrvDesc; // 原始 shadow SRV 拷贝到模型主渲染 heap 后的位置。
shared_ptr<PipelineState> appTerrianShadowPso;
shared_ptr<PipelineState> appQuvierTreeShadowPso;
shared_ptr<Shader> appTerrianCascadeShadowShader;
shared_ptr<Shader> appQuvierTreeCascadeShadowShader;
shared_ptr<RootSignature> appTerrianCascadeShadowRootSignature;
shared_ptr<RootSignature> appQuvierTreeCascadeShadowSignature;


function<void()> initQuvierTreeInstanceDatas = []{
    quvierTreeInstanceDatas.clear();
    quvierTreeInstanceDatas.reserve(QUVIER_TREE_NUM_DEFAULT);
    const DirectX::XMFLOAT2& terrainSize = appTerrianData->GetSize();
    const DirectX::XMFLOAT2& terrainCenter = appTerrianData->GetCenterOffset();
    // 求出QUVIER_TREE_NUM_DEFAULT个树实例的位置
    for(UINT i = 0;i < QUVIER_TREE_NUM_DEFAULT;i++)
    {
        auto sample = Hammersley(i,QUVIER_TREE_NUM_DEFAULT);
        float x = sample.x * terrainSize.x - terrainSize.x * 0.5f + terrainCenter.x;
        float z = sample.y * terrainSize.y - terrainSize.y * 0.5f + terrainCenter.y;
        auto y = appTerrianData->SampleHeight(x, z);   // 从 CPU 地形高度数据采样
        quvierTreeInstanceDatas.emplace_back(DirectX::XMFLOAT3(x, y, z),0.0f);
    }
};

function<void()> updateFilterModelFromDistance = []
{
    filterTreeInstanceDatas.clear();
    filterTreeInstanceDatas.reserve(quvierTreeInstanceDatas.size());

    const DirectX::XMFLOAT3 cameraPos = appCamera->GetPosition();
    DirectX::XMFLOAT3 cameraForward = appCamera->GetForward();
    DirectX::XMFLOAT2 cameraForwardXZ(cameraForward.x, cameraForward.z);
    float cameraForwardXZLengthSq =
        cameraForwardXZ.x * cameraForwardXZ.x +
        cameraForwardXZ.y * cameraForwardXZ.y;
    if(cameraForwardXZLengthSq > 0.000001f)
    {
        const float inverseLength = 1.0f / std::sqrt(cameraForwardXZLengthSq);
        cameraForwardXZ.x *= inverseLength;
        cameraForwardXZ.y *= inverseLength;
    }
    else
    {
        cameraForwardXZ = DirectX::XMFLOAT2(0.0f, 1.0f);
    }
    const float visibleDistanceSq = QUVIER_TREE_VISIBLE_DISTANCE * QUVIER_TREE_VISIBLE_DISTANCE;

    for(auto& it : quvierTreeInstanceDatas)
    {
        const float toTreeX = it.worldPositon.x - cameraPos.x;
        const float toTreeZ = it.worldPositon.z - cameraPos.z;
        it.deltaDistanceWithCamera = toTreeX * toTreeX + toTreeZ * toTreeZ;
        const float cameraTreeCos = toTreeX * cameraForwardXZ.x + toTreeZ * cameraForwardXZ.y;

        if(it.deltaDistanceWithCamera <= visibleDistanceSq && cameraTreeCos > 0.0f)
        {
            filterTreeInstanceDatas.emplace_back(it);
        }
    }
};

void appCascadeShadowInit()
{
    auto engineRoot = Engine::PathUtils::GetExecutableParentPath(6);
    auto quvierTreeShadowShaderPath = engineRoot / L"resource/shaders/appShader/appModelCascadeShadow.hlsl";
    auto terrianShadowShaderPath = engineRoot / L"resource/shaders/appShader/appTerrianCascadeShadow.hlsl";

    cascadeShadowMapCalculate = make_shared<CascadeShadowMapCalculate>();
    cascadeShadowMap = make_shared<CascadeShadowMap>();
    appCascadeShadowDepthSrvDescHeap = make_shared<DescriptorHeap>();
    appCascadeShadowDepthDsvDescHeap = make_shared<DescriptorHeap>();
    appCascadeShadowCB = make_shared<ConstantBuffer>();
    appTerrianShadowPso = make_shared<PipelineState>();
    appQuvierTreeShadowPso = make_shared<PipelineState>();
    appTerrianCascadeShadowShader = make_shared<Shader>();
    appQuvierTreeCascadeShadowShader = make_shared<Shader>();
    
    appTerrianCascadeShadowRootSignature = make_shared<RootSignature>();
    appQuvierTreeCascadeShadowSignature = make_shared<RootSignature>();

    // DescriptorHeap
    if(!appCascadeShadowDepthSrvDescHeap->CreateDescriptorHeap(
        appDevice->GetDeviceComPtr(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        1))
    {
        std::print("appCascadeShadowDepthSrvDescHeap CreateDescriptorHeap Failed\n");
        return;
    }
    if(!appCascadeShadowDepthDsvDescHeap->CreateDescriptorHeap(
        appDevice->GetDeviceComPtr(),
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        1))
    {
        std::print("appCascadeShadowDepthDsvDescHeap CreateDescriptorHeap Failed\n");
        return;
    }

    // ConstantBuffer
    if(!appCascadeShadowCB->Init(appDevice->GetDeviceComPtr(),sizeof(CascadeShadowConstants)))
    {
        std::print("appCascadeShadowCB Init Failed\n");
        return;
    }

    // Shader
    if(!appTerrianCascadeShadowShader->CompileGraphicsFromFile(terrianShadowShaderPath.wstring(),
        Shader::StageCompileDesc("VSMain", "vs_5_1", "VS"),
        Shader::StageCompileDesc("GSMain", "gs_5_1", "GS"),
        Shader::StageCompileDesc("PSMain", "ps_5_1", "PS")))
    {
        std::print("appTerrianCascadeShadowShader CompileGraphicsFromFile Failed\n");
        return;
    }
    
    if(!appQuvierTreeCascadeShadowShader->CompileGraphicsFromFile(quvierTreeShadowShaderPath.wstring(),
        Shader::StageCompileDesc("VSMain", "vs_5_1", "VS"),
        Shader::StageCompileDesc("GSMain", "gs_5_1", "GS"),
        Shader::StageCompileDesc("PSMain", "ps_5_1", "PS")))
    {
        std::print("appQuvierTreeCascadeShadowShader CompileGraphicsFromFile Failed\n");
        return;
    }
    
    D3D12_STATIC_SAMPLER_DESC shadowWrapSampler{};
    shadowWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    shadowWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    shadowWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    shadowWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    shadowWrapSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    shadowWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;
    shadowWrapSampler.ShaderRegister = 0u;
    shadowWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    appTerrianCascadeShadowRootSignature->AddRootCBV(0);
    appTerrianCascadeShadowRootSignature->AddRootCBV(1);
    appTerrianCascadeShadowRootSignature->AddSRVTable(1,0);
    appTerrianCascadeShadowRootSignature->AddStaticSampler(shadowWrapSampler);
    if(!appTerrianCascadeShadowRootSignature->CreateRootSignature(appDevice->GetDeviceComPtr()))
    {
        std::print("appTerrianCascadeShadowRootSignature CreateRootSignature Failed\n");
        return;
    }

    appQuvierTreeCascadeShadowSignature->AddRootCBV(0);
    appQuvierTreeCascadeShadowSignature->AddRootCBV(1);
    if(!appQuvierTreeCascadeShadowSignature->CreateRootSignature(appDevice->GetDeviceComPtr()))
    {
        std::print("appQuvierTreeCascadeShadowSignature CreateRootSignature Failed\n");
        return;
    }


    // appTerrianCascadeShadowRootSignature
    GraphicsPipelineDesc terrianShadowPsoDesc;
    terrianShadowPsoDesc.rootSignature = appTerrianCascadeShadowRootSignature->GetRootSignatureComPtr();
    terrianShadowPsoDesc.vs = appTerrianCascadeShadowShader->GetVsShaderByteCode();
    terrianShadowPsoDesc.gs = appTerrianCascadeShadowShader->GetGsShaderByteCode();
    terrianShadowPsoDesc.ps = appTerrianCascadeShadowShader->GetPsShaderByteCode();
    D3D12_INPUT_ELEMENT_DESC inputElements[]{
        {"GRID",0,DXGI_FORMAT_R32G32_UINT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        
        {"INSTANCE_OFFSET",0,DXGI_FORMAT_R32G32_SINT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_LEVEL",0,DXGI_FORMAT_R32_UINT,1,8,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_ID",0,DXGI_FORMAT_R32_UINT,1,12,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
    };
    terrianShadowPsoDesc.inputLayout = {inputElements,_countof(inputElements)};
    terrianShadowPsoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    terrianShadowPsoDesc.rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    terrianShadowPsoDesc.rasterizer.DepthBias = 64;
    terrianShadowPsoDesc.rasterizer.SlopeScaledDepthBias = 0.5f;
    terrianShadowPsoDesc.blend = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    terrianShadowPsoDesc.depthStencil= CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
    terrianShadowPsoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    terrianShadowPsoDesc.numRenderTargets = 0;
    terrianShadowPsoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    terrianShadowPsoDesc.sampleDesc = {1,0};
    terrianShadowPsoDesc.sampleMask = UINT_MAX;
    terrianShadowPsoDesc.ibStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;

    if(!appTerrianShadowPso->CreateGraphicsPipelineState(appDevice->GetDeviceComPtr(),terrianShadowPsoDesc))
    {
        std::print("appTerrianShadowPso CreateGraphicsPipelineState Failed\n");
        return;
    }

    // appQuvierTreeCascadeShadowSignature
    GraphicsPipelineDesc quvierTreeShadowPsoDesc;
    quvierTreeShadowPsoDesc.rootSignature = appQuvierTreeCascadeShadowSignature->GetRootSignatureComPtr();
    quvierTreeShadowPsoDesc.vs = appQuvierTreeCascadeShadowShader->GetVsShaderByteCode();
    quvierTreeShadowPsoDesc.gs = appQuvierTreeCascadeShadowShader->GetGsShaderByteCode();
    quvierTreeShadowPsoDesc.ps = appQuvierTreeCascadeShadowShader->GetPsShaderByteCode();
    D3D12_INPUT_ELEMENT_DESC inputElements1[]{
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(ModelGpuVertex, position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        
        {"INSTANCE_POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,1,offsetof(QuvierTreeModelInstanceData,worldPositon),D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
    };
    quvierTreeShadowPsoDesc.inputLayout = {inputElements1,_countof(inputElements1)};
    quvierTreeShadowPsoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    quvierTreeShadowPsoDesc.rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    quvierTreeShadowPsoDesc.rasterizer.DepthBias = 64;
    quvierTreeShadowPsoDesc.rasterizer.SlopeScaledDepthBias = 0.5f;
    quvierTreeShadowPsoDesc.blend = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    quvierTreeShadowPsoDesc.depthStencil= CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
    quvierTreeShadowPsoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    quvierTreeShadowPsoDesc.numRenderTargets = 0;
    quvierTreeShadowPsoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    quvierTreeShadowPsoDesc.sampleDesc = {1,0};
    quvierTreeShadowPsoDesc.sampleMask = UINT_MAX;

    if(!appQuvierTreeShadowPso->CreateGraphicsPipelineState(appDevice->GetDeviceComPtr(),quvierTreeShadowPsoDesc))
    {
        std::print("appQuvierTreeShadowPso CreateGraphicsPipelineState Failed\n");
        return;
    }

    if(!light || !cascadeShadowMapCalculate->Initialize(appCamera,light->lightDir))
    {
        std::print("cascadeShadowMapCalculate Initialize Failed\n");
        return;
    }

    D3D12_CLEAR_VALUE shadowClearValue{};
    shadowClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    shadowClearValue.DepthStencil.Depth = 1.0f;
    shadowClearValue.DepthStencil.Stencil = 0;

    CascadeRenderConfig cascadeRenderConfig{};
    cascadeRenderConfig.device = appDevice->GetDeviceComPtr();
    cascadeRenderConfig.commandList = appCommandList->GetGraphicsComPtr();
    cascadeRenderConfig.lightSpaceMatricesCbAddress = appCascadeShadowCB->GPUAddress();
    if(!cascadeShadowMap->intialize(
        cascadeRenderConfig,
        CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R32_TYPELESS,
            SHADOW_MAP_WIDTH,
            SHADOW_MAP_HEIGHT,
            SHADOW_CASCADE_COUNT,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        *appCascadeShadowDepthDsvDescHeap,
        *appCascadeShadowDepthSrvDescHeap,
        SHADOW_MAP_WIDTH,
        SHADOW_MAP_HEIGHT,
        shadowClearValue))
    {
        std::print("cascadeShadowMap Initialize Failed\n");
        return;
    }

    appTerrianCascadeShadowSrvDesc = appTerrianRenderCbvUavSrvDescHeap->GetFreeDescriptorHandle();
    appModelCascadeShadowSrvDesc = appModeCbvUavSrvDescHeap->GetFreeDescriptorHandle();
    if(!appTerrianCascadeShadowSrvDesc || !appModelCascadeShadowSrvDesc)
    {
        std::print("cascade shadow SRV copy descriptor alloc failed\n");
        return;
    }

    auto device = appDevice->GetDeviceComPtr();
    device->CopyDescriptorsSimple(
        1,
        appTerrianCascadeShadowSrvDesc->cpuHandle,
        cascadeShadowMap->GetSrvCpuHandle(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(
        1,
        appModelCascadeShadowSrvDesc->cpuHandle,
        cascadeShadowMap->GetSrvCpuHandle(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    appCascadeShadowUpdate();

}




void appModelInit()
{

    auto engineRoot = Engine::PathUtils::GetExecutableParentPath(6);
    auto shaderPath = engineRoot / L"resource/shaders/appShader/appModel.hlsl";
    auto modelPath = engineRoot / L"resource/model/quiver_tree/quiver_tree_02_1k.gltf";

    auto& commandListObj = appCommandList->GetGraphicsComPtr();

    appQuvierTree = make_shared<Model>();
    appModelPso = make_shared<PipelineState>();
    appModelRootSignature = make_shared<RootSignature>();
    appModeCbvUavSrvDescHeap = make_shared<DescriptorHeap>(); 
    appModelShader = make_shared<Shader>();
    appModelSceneCB = make_shared<ConstantBuffer>();
    appQuvierTreeGpuResource = make_shared<ModelGpuResource>();
    appQuvierTreeInstanceData = make_shared<DynamicInstaceBuffer<QuvierTreeModelInstanceData>>();
    light = make_shared<Light>();

    // ------------------------DescriptorHeap------------------------
    if(!appModeCbvUavSrvDescHeap->CreateDescriptorHeap(
        appDevice->GetDeviceComPtr(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        4))
    {
        std::print("appModeCbvUavSrvDescHeap CreateDescriptorHeap Failed\n");
        return;
    }

    // ------------------------ModelAsset------------------------
    appQuvierTree->ModelInit(modelPath.generic_string(),false);
    if(appQuvierTree->modelMeshs.empty() || appQuvierTree->textures_load.size() < 3)
    {
        std::print("appQuvierTree ModelInit Failed\n");
        return;
    }
    appQuvierTreeTextureUploads.clear();
    if(!appQuvierTree->LoadTextureResources(
        appDevice->GetDeviceComPtr(),
        commandListObj,
        *appModeCbvUavSrvDescHeap,
        appQuvierTreeTextureUploads))
    {
        std::print("appQuvierTree LoadTextureResources Failed\n");
        return;
    }
    if(!CreateModelGpuResourceFromModel(
        *appQuvierTreeGpuResource,
        *appQuvierTree,
        appDevice->GetDeviceComPtr().Get(),
        commandListObj.Get()))
    {
        std::print("CreateModelGpuResourceFromModel appQuvierTree Failed\n");
        return;
    }

    // ------------------------InstanceBuffer------------------------
    initQuvierTreeInstanceDatas();
    updateFilterModelFromDistance();
    if(!appQuvierTreeInstanceData->Init(appDevice->GetDeviceComPtr(),QUVIER_TREE_NUM_DEFAULT))
    {
        std::print("appQuvierTreeInstanceData Init Failed\n");
        return;
    }
    if(!appQuvierTreeInstanceData->Update(filterTreeInstanceDatas))
    {
        std::print("appQuvierTreeInstanceData Update Failed\n");
        return;
    }

    // ------------------------Shader------------------------
    if(!appModelShader->CompileGraphicsFromFile(shaderPath.wstring(),
        Shader::StageCompileDesc("VSMain", "vs_5_1", "VS"),
        Shader::StageCompileDesc("PSMain", "ps_5_1", "PS")))
    {
        std::print("appModelShader CompileGraphicsFromFile Failed\n");
        return;
    }

    // ------------------------RootSignature------------------------
    D3D12_STATIC_SAMPLER_DESC modelWrapSampler{};
    modelWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    modelWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    modelWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    modelWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    modelWrapSampler.MipLODBias = 0.0f;
    modelWrapSampler.MaxAnisotropy = 1;
    modelWrapSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    modelWrapSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    modelWrapSampler.MinLOD = 0.0f;
    modelWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;
    modelWrapSampler.ShaderRegister = 0;
    modelWrapSampler.RegisterSpace = 0;
    modelWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC modelShadowSampler{};
    modelShadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    modelShadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    modelShadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    modelShadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    modelShadowSampler.MipLODBias = 0.0f;
    modelShadowSampler.MaxAnisotropy = 1;
    modelShadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    modelShadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    modelShadowSampler.MinLOD = 0.0f;
    modelShadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
    modelShadowSampler.ShaderRegister = 1;
    modelShadowSampler.RegisterSpace = 0;
    modelShadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    appModelRootSignature->AddRootCBV(0);
    appModelRootSignature->AddSRVTable(3,0,D3D12_SHADER_VISIBILITY_PIXEL);
    appModelRootSignature->AddRootCBV(1,D3D12_SHADER_VISIBILITY_PIXEL);
    appModelRootSignature->AddSRVTable(1,3,D3D12_SHADER_VISIBILITY_PIXEL);
    appModelRootSignature->AddStaticSampler(modelWrapSampler);
    appModelRootSignature->AddStaticSampler(modelShadowSampler);
    if(!appModelRootSignature->CreateRootSignature(appDevice->GetDeviceComPtr()))
    {
        std::print("appModelRootSignature CreateRootSignature Failed\n");
        return;
    }

    // ------------------------PSO------------------------
    GraphicsPipelineDesc gpModelSurfacePsoDesc;
    gpModelSurfacePsoDesc.rootSignature = appModelRootSignature->GetRootSignatureComPtr();
    gpModelSurfacePsoDesc.vs = appModelShader->GetVsShaderByteCode();
    gpModelSurfacePsoDesc.gs = {};
    gpModelSurfacePsoDesc.ps = appModelShader->GetPsShaderByteCode();
    // inputLayout
    D3D12_INPUT_ELEMENT_DESC inputElement[]{
        { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelGpuVertex, position),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelGpuVertex, normal),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelGpuVertex, tangent),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelGpuVertex, bitangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ModelGpuVertex, uv),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "INSTANCE_POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    1, offsetof(QuvierTreeModelInstanceData, worldPositon),D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };
    gpModelSurfacePsoDesc.inputLayout = {inputElement, _countof(inputElement)};
    gpModelSurfacePsoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    gpModelSurfacePsoDesc.rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    gpModelSurfacePsoDesc.blend = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    gpModelSurfacePsoDesc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
    gpModelSurfacePsoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gpModelSurfacePsoDesc.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gpModelSurfacePsoDesc.numRenderTargets = RENDER_TARGET_NUM;
    gpModelSurfacePsoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    gpModelSurfacePsoDesc.sampleDesc = {1,0};
    gpModelSurfacePsoDesc.sampleMask = UINT_MAX;
    if(!appModelPso->CreateGraphicsPipelineState(appDevice->GetDeviceComPtr(),gpModelSurfacePsoDesc))
    {
        std::print("appModelPso CreateGraphicsPipelineState Failed\n");
        return;
    }


    // ------------------------ConstantBuffer------------------------
    appModelSceneCB->Init(appDevice->GetDeviceComPtr(),sizeof(ModelSceneCB));
    // ------------------------Light------------------------
    light->lightPos= {10000.0f,10000.0f,10000.0f};
    light->lightColor= {1,1,1};
    light->lightDir = {-0.45f,-0.85f,-0.25f};


}

void appInitial(HWND hwnd)
{
    appDevice = std::make_shared<Device>();
    appCommandAllocator = std::make_shared<CommandAllocator>();
    appCommandQueue = std::make_shared<CommandQueue>();
    appCommandList = std::make_shared<CommandList>();
    appSwapChain = std::make_shared<SwapChain>();
    // PSO
    appTerrianPso = std::make_shared<PipelineState>();
    appTerrianComputePso = std::make_shared<PipelineState>();
    appTerrianGridPso = std::make_shared<PipelineState>();
    // rootSignature
    appTerrianRootSignature = std::make_shared<RootSignature>();
    appTerrianComputeRootSignature = std::make_shared<RootSignature>();
    appTerrianGridRootSignature = std::make_shared<RootSignature>();

    appRtvDescHeap = std::make_shared<DescriptorHeap>();//目前还没想好怎么分配
    appDsvDescHeap = std::make_shared<DescriptorHeap>();
    appTerrianRenderCbvUavSrvDescHeap = std::make_shared<DescriptorHeap>();
    appComputeCbvUavSrvDescHeap = std::make_shared<DescriptorHeap>();
    appMainDepthTexture = std::make_shared<DepthTexture>();
    appCamera = std::make_shared<QuaternionCamera>();
    appSceneCB = std::make_shared<ConstantBuffer>();
    appComputeCB = std::make_shared<ConstantBuffer>();
    terrianShader = std::make_shared<Shader>();
    terrianComputeShader = std::make_shared<Shader>();
    appGroundMesh = std::make_shared<GroundMesh>(CLIPMAP_BLOCK_UNIT,CLIPMAP_LEVEL_NUM,CLIPMAP_LEVEL0_SCALE);
    appHeightmap = std::make_shared<Heightmap>();
    appTerrianVertex = std::make_shared<VertexBuffer<XMUINT2>>();
    appTerrianIndex = std::make_shared<IndexBuffer<uint16_t>>();
    appTerrianUpdateInstanceData = std::make_shared<DynamicInstaceBuffer<InstanceData>>();
    appTerrianData = std::make_shared<TerrainData>();
    // ------------------------Device------------------------
    appDevice->Initialize();
    const UINT maxClipmapInstanceCount =
        1u + 16u + ((CLIPMAP_LEVEL_NUM - 1u) * 12u) +
        (4u * CLIPMAP_LEVEL_NUM) + (8u * (CLIPMAP_LEVEL_NUM - 1u));
    if(!appTerrianUpdateInstanceData->Init(appDevice->GetDeviceComPtr(),maxClipmapInstanceCount))
    {
        std::print("appTerrianUpdateInstanceData Init Failed\n");
        return;
    }
    // ------------------------CommandAllocator------------------------
    appCommandAllocator->Initialize(appDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT);
    // ------------------------CommandQueue------------------------
    appCommandQueue->Initialize(appDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT);
    // ------------------------CommandList------------------------
    appCommandList->Initialize(appDevice->GetDeviceComPtr(),D3D12_COMMAND_LIST_TYPE_DIRECT,appCommandAllocator->GetCommandAllocatorComPtr());
    appCommandAllocator->Reset();
    appCommandList->GetGraphicsComPtr()->Reset(appCommandAllocator->GetCommandAllocatorComPtr().Get(),nullptr);
    // ------------------------Descriptor------------------------
    appRtvDescHeap->CreateDescriptorHeap(appDevice->GetDeviceComPtr(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV,SWAP_CHIAN_BCAK_BUFFER_NUM);
    appDsvDescHeap->CreateDescriptorHeap(appDevice->GetDeviceComPtr(),D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1);
    appTerrianRenderCbvUavSrvDescHeap->CreateDescriptorHeap(appDevice->GetDeviceComPtr(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3);
    appComputeCbvUavSrvDescHeap->CreateDescriptorHeap(appDevice->GetDeviceComPtr(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3);
    // ------------------------SwapChain------------------------
    vector<DescriptorHandle> tempDescBuffer;
    tempDescBuffer.reserve(SWAP_CHIAN_BCAK_BUFFER_NUM);
    for(UINT i = 0;i < SWAP_CHIAN_BCAK_BUFFER_NUM;i++)
    {
        auto tempDesc = appRtvDescHeap->GetFreeDescriptorHandle();
        if(!tempDesc)
        {
            std::print("SwapChain RTV descriptor alloc failed\n");
            return;
        }
        tempDescBuffer.push_back(*tempDesc);
    }
    appSwapChain->Create(appDevice->GetFactoryComPtr(),appCommandQueue->GetCommandQueueComPtr(),appDevice->GetDeviceComPtr(),
        tempDescBuffer,SWAP_CHIAN_BCAK_BUFFER_NUM,hwnd,WIN_WIDTH,WIN_HEIGHT);
    // ------------------------DepthTexture------------------------
    auto terrianDepthDsv = appDsvDescHeap->GetFreeDescriptorHandle();
    if(!terrianDepthDsv)
    {
        std::print("appMainDepthTexture DSV descriptor alloc failed\n");
        return;
    }
    DepthTextureCreateInfo terrianDepthInfo{};
    terrianDepthInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT,
        WIN_WIDTH,
        WIN_HEIGHT,
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    terrianDepthInfo.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    terrianDepthInfo.hasClearValue = true;
    terrianDepthInfo.clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    terrianDepthInfo.clearValue.DepthStencil.Depth = 1.0f;
    terrianDepthInfo.clearValue.DepthStencil.Stencil = 0;
    terrianDepthInfo.mainDsvCpuHandle = terrianDepthDsv->cpuHandle;
    terrianDepthInfo.mainDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    terrianDepthInfo.mainDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    terrianDepthInfo.mainDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    terrianDepthInfo.mainDsvDesc.Texture2D.MipSlice = 0;
    if(!DepthTextureGenerator::CreateDepthTexture(
        appDevice->GetDeviceComPtr(),
        terrianDepthInfo,
        appMainDepthTexture))
    {
        std::print("appMainDepthTexture CreateDepthTexture Failed\n");
        return;
    }
    // ------------------------Terrian------------------------    
    // 1. 配置源地形数据
    Engine::TerrainNoiseSettings settings;
    settings.resolution = TERRIAN_RESOLUTION;                 // 原始高度图分辨率
    settings.terrainSize = Engine::MakeFloat2(TERRIAN_SIZE, TERRIAN_SIZE); // 世界尺寸
    settings.heightScale = 42.0f;
    settings.noiseScale = 0.0045f;
    settings.noiseOctaves = 7;

    appTerrianData->GenerateFromNoise(settings);
    // ------------------------SourceHeightTexture------------------------
    TextureGenerator sourceHeightTextureGenerator;
    TextureCreateResult sourceHeightTextureResult;
    auto sourceHeightSrv = appComputeCbvUavSrvDescHeap->GetFreeDescriptorHandle();
    if(!sourceHeightSrv)
    {
        std::print("appSourceHeightTexture SRV descriptor alloc failed");
    }
    Texture2DCreateInfo sourceHeightTextureInfo{};
    sourceHeightTextureInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_FLOAT,
        static_cast<UINT64>(appTerrianData->GetResolution()),
        static_cast<UINT>(appTerrianData->GetResolution()),
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_NONE);
    const auto& sourceHeightData = appTerrianData->GetHeightmap();
    sourceHeightTextureInfo.initialSubresource.pData = sourceHeightData.data();
    sourceHeightTextureInfo.initialSubresource.RowPitch = static_cast<LONG_PTR>(appTerrianData->GetResolution() * sizeof(float));
    sourceHeightTextureInfo.initialSubresource.SlicePitch = static_cast<LONG_PTR>(sourceHeightData.size() * sizeof(float));
    if(sourceHeightSrv)
    {
        sourceHeightTextureInfo.srvCpuHandle = sourceHeightSrv->cpuHandle;
        sourceHeightTextureInfo.srvGpuHandle = sourceHeightSrv->gpuHandle;
    }
    sourceHeightTextureInfo.finalState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    sourceHeightTextureInfo.srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    sourceHeightTextureInfo.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sourceHeightTextureInfo.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceHeightTextureInfo.srvDesc.Texture2D.MostDetailedMip = 0;
    sourceHeightTextureInfo.srvDesc.Texture2D.MipLevels = 1;
    sourceHeightTextureInfo.srvDesc.Texture2D.PlaneSlice = 0;
    sourceHeightTextureInfo.srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    if(!sourceHeightTextureGenerator.CreateTexture2D(
        appDevice->GetDeviceComPtr(),
        appCommandList->GetGraphicsComPtr(),
        sourceHeightTextureInfo,
        sourceHeightTextureResult))
    {
        std::print("appSourceHeightTexture CreateTexture2D Failed");
    }
    appSourceHeightTexture = std::move(sourceHeightTextureResult.texture);

    // ------------------------ClipmapHeightTexture------------------------
    TextureGenerator clipmapHeightTextureGenerator;
    TextureCreateResult clipmapHeightTextureResult;
    auto clipmapHeightSrv = appTerrianRenderCbvUavSrvDescHeap->GetFreeDescriptorHandle();
    if(!clipmapHeightSrv)
    {
        std::print("appClipmapHeightTexture SRV descriptor alloc failed");
    }
    auto clipmapHeightUav = appComputeCbvUavSrvDescHeap->GetFreeDescriptorHandle();
    if(!clipmapHeightUav)
    {
        std::print("appClipmapHeightTexture UAV descriptor alloc failed");
    }
    Texture2DCreateInfo clipmapHeightTextureInfo{};
    clipmapHeightTextureInfo.resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        static_cast<UINT64>(appGroundMesh->get_clipmap_resolution()),
        static_cast<UINT>(appGroundMesh->get_clipmap_resolution()),
        static_cast<UINT16>(appGroundMesh->get_clipmap_level_count()),
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if(clipmapHeightSrv)
    {
        clipmapHeightTextureInfo.srvCpuHandle = clipmapHeightSrv->cpuHandle;
        clipmapHeightTextureInfo.srvGpuHandle = clipmapHeightSrv->gpuHandle;
    }
    if(clipmapHeightUav)
    {
        clipmapHeightTextureInfo.uavCpuHandle = clipmapHeightUav->cpuHandle;
        clipmapHeightTextureInfo.uavGpuHandle = clipmapHeightUav->gpuHandle;
    }
    clipmapHeightTextureInfo.finalState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    clipmapHeightTextureInfo.srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    clipmapHeightTextureInfo.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    clipmapHeightTextureInfo.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    clipmapHeightTextureInfo.srvDesc.Texture2DArray.MostDetailedMip = 0;
    clipmapHeightTextureInfo.srvDesc.Texture2DArray.MipLevels = 1;
    clipmapHeightTextureInfo.srvDesc.Texture2DArray.FirstArraySlice = 0;
    clipmapHeightTextureInfo.srvDesc.Texture2DArray.ArraySize = appGroundMesh->get_clipmap_level_count();
    clipmapHeightTextureInfo.srvDesc.Texture2DArray.PlaneSlice = 0;
    clipmapHeightTextureInfo.srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    clipmapHeightTextureInfo.uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    clipmapHeightTextureInfo.uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    clipmapHeightTextureInfo.uavDesc.Texture2DArray.MipSlice = 0;
    clipmapHeightTextureInfo.uavDesc.Texture2DArray.FirstArraySlice = 0;
    clipmapHeightTextureInfo.uavDesc.Texture2DArray.ArraySize = appGroundMesh->get_clipmap_level_count();
    clipmapHeightTextureInfo.uavDesc.Texture2DArray.PlaneSlice = 0;
    if(!clipmapHeightTextureGenerator.CreateTexture2D(
        appDevice->GetDeviceComPtr(),
        appCommandList->GetGraphicsComPtr(),
        clipmapHeightTextureInfo,
        clipmapHeightTextureResult))
    {
        std::print("appClipmapHeightTexture CreateTexture2D Failed");
    }
    appClipmapHeightTexture = std::move(clipmapHeightTextureResult.texture);
    // terrain.SetCenterOffset(...); // 默认是 0,0，地形中心在世界原点
        
    
    // ------------------------Heightmap------------------------    
    appHeightmap->init(appGroundMesh->get_clipmap_resolution(),appGroundMesh->get_clipmap_level_count());
    // ------------------------Vertex------------------------
    appTerrianVertex->Init(appDevice->GetDeviceComPtr(),appCommandList->GetGraphicsComPtr(),appGroundMesh->get_static_mesh_vertex());
    appTerrianIndex->Init(appDevice->GetDeviceComPtr(),appCommandList->GetGraphicsComPtr(),appGroundMesh->get_static_mesh_intex());
    
    
    // ------------------------Constant------------------------
    appSceneCB->Init(appDevice->GetDeviceComPtr(),sizeof(SceneConstants));
    appComputeCB->Init(appDevice->GetDeviceComPtr(),sizeof(ComputeConstants));
    // 第一个CBV
    appSceneCB->CreateCBV(appDevice->GetDeviceComPtr(),*appTerrianRenderCbvUavSrvDescHeap);
    appComputeCB->CreateCBV(appDevice->GetDeviceComPtr(),*appComputeCbvUavSrvDescHeap);
    // 第二个CBV
    // 还没想好怎么写
    // ------------------------RootSignature------------------------
    // Terrian Render
    appTerrianRootSignature->AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0);
    appTerrianRootSignature->AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);
    D3D12_STATIC_SAMPLER_DESC renderWrapSampler{};
    renderWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    renderWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    renderWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    renderWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    renderWrapSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    renderWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;
    renderWrapSampler.ShaderRegister = 0u;
    renderWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC renderShadowSampler{};
    renderShadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    renderShadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    renderShadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    renderShadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    renderShadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    renderShadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    renderShadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
    renderShadowSampler.ShaderRegister = 1u;
    renderShadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    appTerrianRootSignature->AddRootCBV(1,D3D12_SHADER_VISIBILITY_PIXEL);
    appTerrianRootSignature->AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,1,D3D12_SHADER_VISIBILITY_PIXEL);
    appTerrianRootSignature->AddStaticSampler(renderWrapSampler);
    appTerrianRootSignature->AddStaticSampler(renderShadowSampler);
    appTerrianRootSignature->CreateRootSignature(appDevice->GetDeviceComPtr());
    // Terrian Compute
    appTerrianComputeRootSignature->AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0);
    appTerrianComputeRootSignature->AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);
    appTerrianComputeRootSignature->AddDescriptorTable(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0);
    D3D12_STATIC_SAMPLER_DESC computeClampSampler{};
    computeClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    computeClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    computeClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    computeClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    computeClampSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    computeClampSampler.MaxLOD = D3D12_FLOAT32_MAX;
    computeClampSampler.ShaderRegister = 0u;
    computeClampSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    appTerrianComputeRootSignature->AddStaticSampler(computeClampSampler);
    appTerrianComputeRootSignature->CreateRootSignature(appDevice->GetDeviceComPtr(),D3D12_ROOT_SIGNATURE_FLAG_NONE);


    // ------------------------Shader------------------------
    // 着色器的构建有点问题，后面要修改一下
    const std::wstring terrianShaderPath =
        PathUtils::ResolveResourcePathWString(L"resource/shaders/appShader/appTerrian.hlsl");
    terrianShader->CompileGraphicsFromFile(terrianShaderPath,
        Shader::StageCompileDesc("VSMain","vs_5_1","VS"),
        Shader::StageCompileDesc("PSSurface","ps_5_1","PS"));

    terrianComputeShader->CompileComputeFromFile(terrianShaderPath,
        Shader::StageCompileDesc("CSMain","cs_5_1","CS"));
    // ------------------------PSO------------------------
    // TerrianSurface
    GraphicsPipelineDesc gpTerrianSurfacePsoDesc;
    gpTerrianSurfacePsoDesc.rootSignature = appTerrianRootSignature->GetRootSignatureComPtr();
    gpTerrianSurfacePsoDesc.vs = terrianShader->GetVsShaderByteCode();
    gpTerrianSurfacePsoDesc.gs = {};
    gpTerrianSurfacePsoDesc.ps = terrianShader->GetPsShaderByteCode();
    D3D12_INPUT_ELEMENT_DESC inputElement[]{
        {"GRID",0,DXGI_FORMAT_R32G32_UINT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        
        {"INSTANCE_OFFSET",0,DXGI_FORMAT_R32G32_SINT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_LEVEL",0,DXGI_FORMAT_R32_UINT,1,8,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"INSTANCE_ID",0,DXGI_FORMAT_R32_UINT,1,12,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
    };
    D3D12_INPUT_LAYOUT_DESC inputLayout{
        inputElement,4
    };
    gpTerrianSurfacePsoDesc.inputLayout = inputLayout;
    // 先做不渲染网格的Pso
    gpTerrianSurfacePsoDesc.rasterizer = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    gpTerrianSurfacePsoDesc.blend = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    gpTerrianSurfacePsoDesc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());// 默认不适用模板处理
    gpTerrianSurfacePsoDesc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gpTerrianSurfacePsoDesc.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gpTerrianSurfacePsoDesc.numRenderTargets = RENDER_TARGET_NUM;
    gpTerrianSurfacePsoDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    gpTerrianSurfacePsoDesc.sampleDesc = {1,0};
    gpTerrianSurfacePsoDesc.sampleMask = UINT_MAX;
    gpTerrianSurfacePsoDesc.ibStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;//要启用顶点索引的结束标志位
    if(!appTerrianPso->CreateGraphicsPipelineState(appDevice->GetDeviceComPtr(),gpTerrianSurfacePsoDesc))
    {
        std::print("appTerrianPso CreateGraphicsPipelineState Failed");
    }
    // TerrianCompute
    ComputePipelineDesc cpTerrianPsoDesc{};
    cpTerrianPsoDesc.rootSignature = appTerrianComputeRootSignature->GetRootSignatureComPtr();
    cpTerrianPsoDesc.cs = terrianComputeShader->GetCsShaderByteCode();
    if(!appTerrianComputePso->CreateComputePipelineState(appDevice->GetDeviceComPtr(),cpTerrianPsoDesc))
    {
        std::print("appTerrianComputePso CreateComputePipelineState Failed");
    }
    // TerrianGrid

    // ---------------- update vertex buffer ----------------
    appTerrianVertex->UploadToDefault(appCommandList->GetGraphicsComPtr());
    appTerrianIndex->UploadToDefault(appCommandList->GetGraphicsComPtr());


    // ------------------------Camera------------------------
    appCamera->SetPerspective(DirectX::XM_PIDIV4,static_cast<float>(WIN_WIDTH) / static_cast<float>(WIN_HEIGHT),0.1f,10000.0f);
    appCamera->SetPosition(DirectX::XMFLOAT3(0.0f,20.0f,0.0f));


    // ------------------------ClipmapTerrian------------------------
    appGroundMesh->update_draw_list();

    // ---------------- viewport / scissor ----------------
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(WIN_WIDTH);
    viewport.Height = static_cast<float>(WIN_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = WIN_WIDTH;
    scissor.bottom = WIN_HEIGHT;

    appModelInit();
    appCascadeShadowInit();

    //记得执行命令队列和等待完成
    // 录制完毕
    auto& commandListObj = appCommandList->GetGraphicsComPtr();
    commandListObj->Close();
    ID3D12CommandList* lists[] = {commandListObj.Get()};
    auto& commandQueue = appCommandQueue->GetCommandQueueComPtr();
    commandQueue->ExecuteCommandLists(1,lists);
    appCommandQueue->Flush();
    if(appQuvierTreeGpuResource)
    {
        ReleaseModelGpuResourceUploadResources(*appQuvierTreeGpuResource);
    }
    appQuvierTreeTextureUploads.clear();
}

void appCascadeShadowUpdate()
{
    if(!cascadeShadowMapCalculate || !appCascadeShadowCB || !appCamera || !light)
    {
        return;
    }

    cascadeShadowMapCalculate->UpdateCameraLightConfig(appCamera,light->lightDir);
    csc = {};

    const auto& lightViewProj = cascadeShadowMapCalculate->GetLightViewMatrices();
    for(size_t i = 0;i < lightViewProj.size() && i < std::size(csc.gLightViewProj);i++)
    {
        csc.gLightViewProj[i] = lightViewProj[i];
    }

    const auto& splits = cascadeShadowMapCalculate->GetShadowCascadeLevels();
    csc.gCascadeSplits = XMFLOAT4(
        splits.size() > 0 ? splits[0] : 0.0f,
        splits.size() > 1 ? splits[1] : 0.0f,
        splits.size() > 2 ? splits[2] : 0.0f,
        splits.size() > 3 ? splits[3] : 0.0f);
    csc.gLightDirAndCount = XMFLOAT4(light->lightDir.x,light->lightDir.y,light->lightDir.z,static_cast<float>(SHADOW_CASCADE_COUNT));
    csc.gShadowInfo = XMFLOAT4(appCamera->GetFarZ(),0.0005f,0.0f,static_cast<float>(SHADOW_MAP_WIDTH));

    appCascadeShadowCB->Update(&csc,sizeof(csc));
}

void appModelUpdate()
{
    if(!appModelSceneCB || !appCamera || !light || !appQuvierTreeInstanceData)
    {
        return;
    }

    updateFilterModelFromDistance();
    if(!appQuvierTreeInstanceData->Update(filterTreeInstanceDatas))
    {
        std::print("appQuvierTreeInstanceData Update Failed\n");
        return;
    }

    mscb.world = XMFLOAT4X4{
        QUVIER_TREE_MODEL_SCALE, 0.0f,  0.0f,  0.0f,
        0.0f,  QUVIER_TREE_MODEL_SCALE, 0.0f,  0.0f,
        0.0f,  0.0f,  QUVIER_TREE_MODEL_SCALE, 0.0f,
        0.0f,  0.0f,  0.0f,  1.0f
    };
    XMStoreFloat4x4(&mscb.viewProj,appCamera->GetViewProjection());
    mscb.cameraPos = appCamera->GetPosition();
    mscb.lightPos = light->lightPos;
    mscb.lightColor = light->lightColor;
    mscb.exposure = 1.0f;
    appModelSceneCB->Update(&mscb,sizeof(ModelSceneCB));

}

void appUpdate()
{
    // ---------------- update constant buffer ----------------
    // ---------------- Terrian Config ----------------
    // SceneConstants
    // 1. 当前相机对应的每层 clipmap offset
    appGroundMesh->update_level_offsets(DirectX::XMFLOAT2(appCamera->GetPosition().x,appCamera->GetPosition().z));
    // 2. 当前相机视锥体
    appGroundMesh->construct_frustum(DirectX::XMMatrixTranspose(appCamera->GetViewProjection()));
    // 3. 地形真实高度范围，只要地形高度范围变了才需要重新传
    appGroundMesh->set_frustum_height_range(appTerrianData->GetMinHeight(), appTerrianData->GetMaxHeight());
    // 4. 生成 draw_infos 和 instances，内部会自动做剔除
    appGroundMesh->update_draw_list();
    const auto& levelOffset = appGroundMesh->get_level_offsets();
    const auto& updates = appHeightmap->update(
        *appTerrianData,
        levelOffset,
        appGroundMesh->get_clipmap_scale());
    std::array<DrawInfo, BLOCK_COUNT> drawInfoArray;
    drawInfoArray = appGroundMesh->get_draw_infos();
    XMStoreFloat4x4(&sc.gViewProj,appCamera->GetViewProjection());
    sc.gCameraPos = XMFLOAT4(appCamera->GetPosition().x,appCamera->GetPosition().y,appCamera->GetPosition().z,1);
    for(UINT levelIndex = 0;levelIndex < CLIPMAP_LEVEL_NUM;levelIndex++)
    {
        sc.gLevelOffsets[levelIndex] = XMINT4(levelOffset[levelIndex].x,levelOffset[levelIndex].y,0,0);
    }
    float inverseLevelSize =
        1.0f / (appGroundMesh->get_clipmap_scale() * static_cast<float>(appGroundMesh->get_clipmap_resolution()));
    for (uint32_t i = 0u; i < CLIPMAP_LEVEL_NUM; ++i) {
        sc.gInvLevelSizes[i] = DirectX::XMFLOAT4(inverseLevelSize, 0.0f, 0.0f, 0.0f);
        inverseLevelSize *= 0.5f;
    }
    sc.gRenderClipmapParams = DirectX::XMFLOAT4(
        static_cast<float>(appGroundMesh->get_size()),//CLIPMAP_BLOCK_UNIT
        static_cast<float>(appGroundMesh->get_clipmap_resolution()),//一层clipmap的尺寸，即分辨率
        static_cast<float>(appGroundMesh->get_clipmap_level_count()),// clipmap的层数
        appGroundMesh->get_clipmap_scale());// level0的一个单位对应的世界单位
    const DirectX::XMFLOAT2 terrainSize = appTerrianData->GetSize();
    const DirectX::XMFLOAT2 terrainCenter = appTerrianData->GetCenterOffset();
    sc.gTerrainSizeCenter = XMFLOAT4(terrainSize.x,terrainSize.y,terrainCenter.x,terrainCenter.y);
    sc.gRenderTerrainSampleParams = XMFLOAT4(
        appTerrianData->GetMinHeight(),
        appTerrianData->GetMaxHeight(),
        1.0f / static_cast<float>(appGroundMesh->get_clipmap_resolution()),
        static_cast<float>(appTerrianData->GetResolution()));
    appSceneCB->Update(&sc,sizeof(sc));
    // ComputeConstants
    appPendingUpdateCount  = static_cast<uint32_t>(
        std::min<size_t>(updates.size(), Engine::Heightmap::MAX_UPDATE_COUNT));
    cc.gComputeClipmapUpdateParams = DirectX::XMUINT4(
        appGroundMesh->get_clipmap_resolution(),
        0u,
        static_cast<uint32_t>(appTerrianData->GetResolution()),
        appPendingUpdateCount );
    cc.gComputeTerrainParams = DirectX::XMFLOAT4(terrainSize.x,terrainSize.y,terrainCenter.x,terrainCenter.y);
    cc.gComputeScaleParams = DirectX::XMFLOAT4(appGroundMesh->get_clipmap_scale(),0.0f,0.0f,0.0f);
    for(uint32_t i = 0u;i < appPendingUpdateCount ;i++)
    {
        const Engine::HeightmapUpdateInfo& update = updates[i];
        cc.gUpdateTexSize[i] = DirectX::XMINT4(update.tex.x,update.tex.y,update.size.x,update.size.y);
        cc.gUpdateStartLevel[i] = DirectX::XMINT4(update.start.x,update.start.y,static_cast<int>(update.level),0);
    }
    appComputeCB->Update(&cc,sizeof(cc));
    appModelUpdate();


}

void appRenderCallBack()
{
    auto& commandListObj = appCommandList->GetGraphicsComPtr();
    if(!commandListObj || !appCommandAllocator)
    {
        return;
    }
    appCommandAllocator->Reset();
    commandListObj->Reset(appCommandAllocator->GetCommandAllocatorComPtr().Get(),nullptr);

    const auto& instances = appGroundMesh->get_instance_data();
    if(!appTerrianUpdateInstanceData->Update(instances))
    {
        std::print("appTerrianUpdateInstanceData Update Failed\n");
        return;
    }

    appTerrianComputePass();
    appCascadeShadowUpdate();
    appCascadeShadowPass();

    auto backBufferResource = appSwapChain->GetCurrentBackBufferResource();
    auto backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
        backBufferResource.Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandListObj->ResourceBarrier(1,&backBufferTransition);

    const FLOAT clearColor[4] = {0.20f,0.24f,0.30f,1.0f};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = appSwapChain->GetCurrentRTVCPUHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = appMainDepthTexture->GetDsvCpuHandle();
    commandListObj->ClearRenderTargetView(rtv,clearColor,0,nullptr);
    commandListObj->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);
    commandListObj->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    commandListObj->RSSetViewports(1,&viewport);
    commandListObj->RSSetScissorRects(1,&scissor);

    appTerrianRenderPass();
    appModelRenderPass();

    auto presentTransition = CD3DX12_RESOURCE_BARRIER::Transition(
        backBufferResource.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    commandListObj->ResourceBarrier(1,&presentTransition);

    commandListObj->Close();
    ID3D12CommandList* lists[] = {commandListObj.Get()};
    auto& commandQueue = appCommandQueue->GetCommandQueueComPtr();
    commandQueue->ExecuteCommandLists(1,lists);
    appSwapChain->Present(1,0);
    // cpu等待(后续考虑封装成帧资源)
    appCommandQueue->Flush();
}

void appTerrianComputePass()
{
    auto& commandListObj = appCommandList->GetGraphicsComPtr();
    if(!commandListObj || appPendingUpdateCount == 0u || !appClipmapHeightTexture.HasUav())
    {
        return;
    }

    if(appClipmapHeightTexture.GetCurrentState() != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            appClipmapHeightTexture.GetResource(),
            appClipmapHeightTexture.GetCurrentState(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandListObj->ResourceBarrier(1,&barrier);
        appClipmapHeightTexture.SetCurrentState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ID3D12DescriptorHeap* descriptorHeaps[] = {appComputeCbvUavSrvDescHeap->GetDescriptorHeapComPtr().Get()};
    commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
    commandListObj->SetComputeRootSignature(appTerrianComputeRootSignature->GetRootSignatureComPtr().Get());
    commandListObj->SetPipelineState(appTerrianComputePso->GetPipelineStateComPtr().Get());
    commandListObj->SetComputeRootDescriptorTable(0,appComputeCB->GetCBVGpuHandle());
    commandListObj->SetComputeRootDescriptorTable(1,appSourceHeightTexture.GetSrvGpuHandle());
    commandListObj->SetComputeRootDescriptorTable(2,appClipmapHeightTexture.GetUavGpuHandle());

    const UINT groupCount = (appGroundMesh->get_clipmap_resolution() + 15u) / 16u;
    commandListObj->Dispatch(groupCount,groupCount,appPendingUpdateCount);

    auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(appClipmapHeightTexture.GetResource());
    commandListObj->ResourceBarrier(1,&uavBarrier);

    auto srvBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        appClipmapHeightTexture.GetResource(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandListObj->ResourceBarrier(1,&srvBarrier);
    appClipmapHeightTexture.SetCurrentState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    appPendingUpdateCount = 0u;
}

void appTerrianRenderPass()
{
    auto& commandListObj = appCommandList->GetGraphicsComPtr();
    if(!commandListObj || !appCascadeShadowCB || !appTerrianCascadeShadowSrvDesc)
    {
        return;
    }


    // ------------------------PSO------------------------
    commandListObj->SetGraphicsRootSignature(appTerrianRootSignature->GetRootSignatureComPtr().Get());
    commandListObj->SetPipelineState(appTerrianPso->GetPipelineStateComPtr().Get());
    ID3D12DescriptorHeap* descriptorHeaps[] = {appTerrianRenderCbvUavSrvDescHeap->GetDescriptorHeapComPtr().Get()};
    commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
    commandListObj->SetGraphicsRootDescriptorTable(0,appSceneCB->GetCBVGpuHandle());
    commandListObj->SetGraphicsRootDescriptorTable(1,appClipmapHeightTexture.GetSrvGpuHandle());
    commandListObj->SetGraphicsRootConstantBufferView(2,appCascadeShadowCB->GPUAddress());
    commandListObj->SetGraphicsRootDescriptorTable(3,appTerrianCascadeShadowSrvDesc->gpuHandle);
    commandListObj->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = appTerrianVertex->GetVertexBufferView();
    D3D12_VERTEX_BUFFER_VIEW instanceBufferView = appTerrianUpdateInstanceData->GetVertexBufferView();
    D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = {vertexBufferView,instanceBufferView};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = appTerrianIndex->GetIndexBufferView();
    commandListObj->IASetVertexBuffers(0,2,vertexBufferViews);
    commandListObj->IASetIndexBuffer(&indexBufferView);
    const auto& drawInfos = appGroundMesh->get_draw_infos();
    // 根据实例绘制网格
    for(const auto& info : drawInfos)
    {
        commandListObj->DrawIndexedInstanced(
            info.index_count,
            info.instance_count,
            static_cast<UINT>(info.index_buffer_offset),
            0,
            static_cast<UINT>(info.first_instance));
    }

}

void appCascadeShadowPass()
{
    auto& commandListObj = appCommandList->GetGraphicsComPtr();
    if(!commandListObj || !cascadeShadowMap || !appCascadeShadowCB)
    {
        return;
    }
    // 这里必须先把 cascade shadow map 绑定成 DSV。
    // 不需要 RTV，因为 shadow pass 只写深度。
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = cascadeShadowMap->GetDsvCpuHandle();
    auto oldState = cascadeShadowMap->GetDepthCurrentState();
    if(oldState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            cascadeShadowMap->GetDepthResource(),
            oldState,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);

        commandListObj->ResourceBarrier(1, &barrier);
        cascadeShadowMap->SetDepthCurrentState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    commandListObj->ClearDepthStencilView(shadowDsv,D3D12_CLEAR_FLAG_DEPTH,1.0f,0.0f,0,nullptr);
    commandListObj->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);

    D3D12_VIEWPORT shadowViewport{};
    shadowViewport.TopLeftX = 0.0f;
    shadowViewport.TopLeftY = 0.0f;
    shadowViewport.Width = static_cast<float>(SHADOW_MAP_WIDTH);
    shadowViewport.Height = static_cast<float>(SHADOW_MAP_HEIGHT);
    shadowViewport.MinDepth = 0.0f;
    shadowViewport.MaxDepth = 1.0f;

    D3D12_RECT shadowScissor{};
    shadowScissor.left = 0;
    shadowScissor.top = 0;
    shadowScissor.right = SHADOW_MAP_WIDTH;
    shadowScissor.bottom = SHADOW_MAP_HEIGHT;

    commandListObj->RSSetViewports(1, &shadowViewport);
    commandListObj->RSSetScissorRects(1, &shadowScissor);

    commandListObj->SetGraphicsRootSignature(appTerrianCascadeShadowRootSignature->GetRootSignatureComPtr().Get());
    commandListObj->SetPipelineState(appTerrianShadowPso->GetPipelineStateComPtr().Get());
    ID3D12DescriptorHeap* descriptorHeaps[] = {appTerrianRenderCbvUavSrvDescHeap->GetDescriptorHeapComPtr().Get()};
    commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
    commandListObj->SetGraphicsRootConstantBufferView(0,appSceneCB->GPUAddress());
    commandListObj->SetGraphicsRootConstantBufferView(1,appCascadeShadowCB->GPUAddress());
    commandListObj->SetGraphicsRootDescriptorTable(2,appClipmapHeightTexture.GetSrvGpuHandle());
    commandListObj->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_VERTEX_BUFFER_VIEW terrianInstanceBufferView  = appTerrianUpdateInstanceData->GetVertexBufferView();
    D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = { appTerrianVertex->GetVertexBufferView(), terrianInstanceBufferView  };
    commandListObj->IASetVertexBuffers(0,2,vertexBufferViews);
    D3D12_INDEX_BUFFER_VIEW indexBufferView = appTerrianIndex->GetIndexBufferView();
    commandListObj->IASetIndexBuffer(&indexBufferView);

    for(const auto& info : appGroundMesh->get_draw_infos())
    {
        commandListObj->DrawIndexedInstanced(
            info.index_count,
            info.instance_count,
            static_cast<UINT>(info.index_buffer_offset),
            0,
            static_cast<UINT>(info.first_instance));
    }


    commandListObj->SetGraphicsRootSignature(appQuvierTreeCascadeShadowSignature->GetRootSignatureComPtr().Get());
    commandListObj->SetPipelineState(appQuvierTreeShadowPso->GetPipelineStateComPtr().Get());

    commandListObj->SetGraphicsRootConstantBufferView(0, appModelSceneCB->GPUAddress());
    commandListObj->SetGraphicsRootConstantBufferView(1, appCascadeShadowCB->GPUAddress());

    D3D12_VERTEX_BUFFER_VIEW quvierTreeInstanceBufferView  = appQuvierTreeInstanceData->GetVertexBufferView();
    commandListObj->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandListObj->IASetVertexBuffers(1, 1, &quvierTreeInstanceBufferView );

    DrawModelGpuResourceInstanced(
        *appQuvierTreeGpuResource,
        commandListObj.Get(),
        appQuvierTreeInstanceData->GetInstanceCount());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        cascadeShadowMap->GetDepthResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    commandListObj->ResourceBarrier(1, &barrier);
    cascadeShadowMap->SetDepthCurrentState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void appModelRenderPass()
{
    auto& commandListObj = appCommandList->GetGraphicsComPtr();
    if(!commandListObj || !appModelPso || !appModelRootSignature ||
        !appModelSceneCB || !appModeCbvUavSrvDescHeap ||
        !appCascadeShadowCB || !appModelCascadeShadowSrvDesc ||
        !appQuvierTree || !appQuvierTreeGpuResource || !appQuvierTreeInstanceData)
    {
        return;
    }

    if(appQuvierTree->textures.size() < 3 ||
        !appQuvierTree->textures[0] ||
        !appQuvierTree->textures[1] ||
        !appQuvierTree->textures[2])
    {
        return;
    }

    ID3D12DescriptorHeap* descriptorHeaps[] = {appModeCbvUavSrvDescHeap->GetDescriptorHeapComPtr().Get()};
    commandListObj->SetDescriptorHeaps(1,descriptorHeaps);
    commandListObj->SetGraphicsRootSignature(appModelRootSignature->GetRootSignatureComPtr().Get());
    commandListObj->SetPipelineState(appModelPso->GetPipelineStateComPtr().Get());
    commandListObj->SetGraphicsRootConstantBufferView(0,appModelSceneCB->GPUAddress());
    commandListObj->SetGraphicsRootDescriptorTable(1,appQuvierTree->textures[0]->GetSrvGpuHandle());
    commandListObj->SetGraphicsRootConstantBufferView(2,appCascadeShadowCB->GPUAddress());
    commandListObj->SetGraphicsRootDescriptorTable(3,appModelCascadeShadowSrvDesc->gpuHandle);

    const UINT instanceCount = appQuvierTreeInstanceData->GetInstanceCount();
    if(instanceCount == 0)
    {
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW instanceBufferView = appQuvierTreeInstanceData->GetVertexBufferView();
    commandListObj->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandListObj->IASetVertexBuffers(1,1,&instanceBufferView);
    DrawModelGpuResourceInstanced(
        *appQuvierTreeGpuResource,
        commandListObj.Get(),
        instanceCount);

}

void appInputCallBack()
{
    // 状态判断，待补充
    if(!appCamera)
        return;
    static float moveSpeed = 0.5f;
    if((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        moveSpeed = 10.0f;
    else
        moveSpeed = 0.5f;
    if((GetAsyncKeyState('W') & 0x8000) != 0)
        appCamera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,moveSpeed));
    if((GetAsyncKeyState('S') & 0x8000) != 0)
        appCamera->MoveLocal(DirectX::XMFLOAT3(0.0f,0.0f,-moveSpeed));
    if((GetAsyncKeyState('A') & 0x8000) != 0)
        appCamera->MoveLocal(DirectX::XMFLOAT3(-moveSpeed,0.0f,0.0f));
    if((GetAsyncKeyState('D') & 0x8000) != 0)
        appCamera->MoveLocal(DirectX::XMFLOAT3(moveSpeed,0.0f,0.0f));
    if((GetAsyncKeyState('Q') & 0x8000) != 0)
        appCamera->MoveWorld(DirectX::XMFLOAT3(0.0f,-moveSpeed,0.0f));
    if((GetAsyncKeyState('E') & 0x8000) != 0)
        appCamera->MoveWorld(DirectX::XMFLOAT3(0.0f,moveSpeed,0.0f));

    POINT currentMousePosition{};
    if(GetCursorPos(&currentMousePosition))
    {
        ScreenToClient(windowsSystem->GetWindowHwnd(),&currentMousePosition);
        if((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
        {
            if(hasLastMousePosition)
            {
                float yawRadians = static_cast<float>(currentMousePosition.x - lastMousePosition.x) * 0.005f;
                float pitchRadians = static_cast<float>(currentMousePosition.y - lastMousePosition.y) * 0.005f;
                appCamera->AddYawPitch(yawRadians,pitchRadians);
            }
            lastMousePosition = currentMousePosition;
            hasLastMousePosition = true;
        }
        else
        {
            lastMousePosition = currentMousePosition;
            hasLastMousePosition = false;
        }
    }
}
