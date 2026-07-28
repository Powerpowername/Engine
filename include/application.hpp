#pragma once
#include "global.h"
#include "Window/WindowsSystem.hpp"
#include "Device/Device.hpp"
#include "Command/Command.hpp"
#include "Resource/SwapChain.hpp"
#include "PipelineState/PipelineState.hpp"
#include "RootSignature/RootSignature.hpp"
#include "Descriptor/Descriptor.hpp"
#include "Resource/Texture.hpp"
#include "Resource/ConstantBuffer.hpp"
#include "Camera/QuaternionCamera.hpp"
#include "Shader/Shader.hpp"
#include "Auxi/PathUtils.hpp"
#include "tools/clipmap_port/GroundMesh.h"
#include "tools/clipmap_port/Heightmap.h"
#include "Resource/Mesh.hpp"
#include "Asset/ModelAsset.hpp"
#include "Asset/ModelGpuResource.hpp"
#include "tools/CascadeShadowMap/CascadeShadowMap.hpp"
#include <functional>


#define WIN_WIDTH 800
#define WIN_HEIGHT 600
#define RENDER_TARGET_NUM 1 
#define SWAP_CHIAN_BCAK_BUFFER_NUM 2
#define CLIPMAP_LEVEL_NUM 10
#define CLIPMAP_BLOCK_UNIT 64 // 就是基础的一个quad的m*m尺寸
#define TERRIAN_RESOLUTION 4096
#define CLIPMAP_LEVEL0_SCALE 1.0f // level0层的单位尺寸对应的世界尺寸为1.0m
#define CLIPMAP_LEVEL_RESOLUTION ((4u * (CLIPMAP_BLOCK_UNIT)) - 1u)
#define CLIPMAP_MAX_LEVEL_INDEX ((CLIPMAP_LEVEL_NUM) - 1u)
#define CLIPMAP_MAX_LEVEL_STEP (1u << (CLIPMAP_MAX_LEVEL_INDEX))
#define TERRIAN_SIZE (((CLIPMAP_LEVEL_RESOLUTION) - 1u) * (CLIPMAP_MAX_LEVEL_STEP) * (CLIPMAP_LEVEL0_SCALE))
#define TERRIAN_RADIUS ((TERRIAN_SIZE) * 0.5f)
#define QUVIER_TREE_NUM_DEFAULT 3000000
#define QUVIER_TREE_MODEL_SCALE 10.0f
#define QUVIER_TREE_VISIBLE_DISTANCE 600.0f
#define SHADOW_MAP_WIDTH 2048
#define SHADOW_MAP_HEIGHT 2048
#define SHADOW_CASCADE_COUNT 5
// #define MAX_TERRIAN_HEIGHT 50000 // 设置最大高度为5000m
// #define MIN_TERRIAN_HEIGHT 0 // 设置最小高度为0m


using namespace std;
using namespace DirectX;
using namespace Engine;
extern shared_ptr<DXEngine::WindowsSystem> windowsSystem;
extern shared_ptr<Device> appDevice;
extern shared_ptr<CommandAllocator> appCommandAllocator;
extern shared_ptr<CommandQueue> appCommandQueue;
extern shared_ptr<CommandList> appCommandList;

extern shared_ptr<SwapChain> appSwapChain;

extern shared_ptr<DescriptorHeap> appRtvDescHeap;
extern shared_ptr<DescriptorHeap> appDsvDescHeap;

extern shared_ptr<DescriptorHeap> appTerrianRenderCbvUavSrvDescHeap;
extern shared_ptr<DescriptorHeap> appComputeCbvUavSrvDescHeap;

extern shared_ptr<DepthTexture> appTerrianDepthTexture;

extern shared_ptr<QuaternionCamera> appCamera;
// SceneConstants与ComputeConstants放在一起

extern shared_ptr<ConstantBuffer> appSceneCB;
extern shared_ptr<ConstantBuffer> appComputeCB;

extern shared_ptr<Shader> terrianShader;
extern shared_ptr<Shader> terrianComputeShader;
// pso
extern shared_ptr<PipelineState> appTerrianPso;
extern shared_ptr<PipelineState> appTerrianComputePso;
extern shared_ptr<PipelineState> appTerrianGridPso;
// rootSignature
extern shared_ptr<RootSignature> appTerrianRootSignature;
extern shared_ptr<RootSignature> appTerrianComputeRootSignature;
extern shared_ptr<RootSignature> appTerrianGridRootSignature;
// Clipmap
extern shared_ptr<GroundMesh> appGroundMesh;
extern shared_ptr<Heightmap> appHeightmap;
// ConstantBuffer
struct SceneConstants
{
    // CPU 已经准备好的视图投影矩阵，把世界坐标直接变换到裁剪空间。
    XMFLOAT4X4 gViewProj;
    // 相机世界坐标，xz 用来计算 LOD 混合和雾化距离。
    XMFLOAT4 gCameraPos;
    // 每层 clipmap 在全局 level 0 网格坐标中的左上角起点。
    XMINT4 gLevelOffsets[10];
    // 每层整张环形缓存的世界尺寸倒数，用来把离相机距离归一化到 0..1。
    XMFLOAT4 gInvLevelSizes[10];
    // x=size，y=clipmap_resolution，z=clipmap_level_count，w=clipmap_scale。
    XMFLOAT4 gRenderClipmapParams;
    // xy=地形世界尺寸，zw=地形中心偏移。
    XMFLOAT4 gTerrainSizeCenter;
    // x=最小高度，y=最大高度，z=1/clipmap_resolution，w=源高度图分辨率。
    XMFLOAT4 gRenderTerrainSampleParams;
    // 预留渲染开关，目前窗口/离线入口都保持默认值。
    XMFLOAT4 gRenderFlags;
};
extern SceneConstants sc;

struct ComputeConstants
{ 
    // x=clipmap_resolution，y=预留，z=源高度图分辨率，w=本帧更新矩形数量。
    XMUINT4 gComputeClipmapUpdateParams;
    // xy=地形世界尺寸，zw=地形中心偏移。
    XMFLOAT4 gComputeTerrainParams;
    // x=clipmap_scale，即 level 0 一个网格单位对应的世界距离。
    XMFLOAT4 gComputeScaleParams;
    // 每个更新任务：xy=写入 clipmap 纹理的起点，zw=更新区域大小。
    XMINT4 gUpdateTexSize[80];
    // 每个更新任务：xy=源地形逻辑 texel 起点，z=clipmap level。
    XMINT4 gUpdateStartLevel[80];
};
extern ComputeConstants cc;


extern D3D12_VIEWPORT viewport;
extern D3D12_RECT scissor;

extern shared_ptr<VertexBuffer<XMUINT2>> appTerrianVertex;
extern shared_ptr<IndexBuffer<uint16_t>> appTerrianIndex;
extern shared_ptr<DynamicInstaceBuffer<InstanceData>> appTerrianUpdateInstanceData;
extern shared_ptr<TerrainData> appTerrianData;
extern Texture appSourceHeightTexture;
extern Texture appClipmapHeightTexture;


extern bool hasLastMousePosition;
extern POINT lastMousePosition;

extern uint32_t appPendingUpdateCount;

// ------------------------Mode------------------------
extern shared_ptr<Model> appQuvierTree;
// PSO
extern shared_ptr<PipelineState> appModelPso;
// RootSignature
extern shared_ptr<RootSignature> appModelRootSignature;
// DescriptorHeap
extern shared_ptr<DescriptorHeap> appModeCbvUavSrvDescHeap;
extern shared_ptr<ModelGpuResource> appQuvierTreeGpuResource;
extern shared_ptr<PipelineState> appTerrianShadowPso;
extern shared_ptr<PipelineState> appQuvierTreeShadowPso;
struct QuvierTreeModelInstanceData
{
    XMFLOAT3 worldPositon{};
    float deltaDistanceWithCamera{};
    // float padding1;
    // float padding2;
};

struct ModelSceneCB
{
    XMFLOAT4X4 world;
    XMFLOAT4X4 viewProj;
    XMFLOAT3 cameraPos;
    float  padding0;

    XMFLOAT3 lightPos;
    float  padding1;

    XMFLOAT3 lightColor;
    float  exposure;
};

struct Light
{
    XMFLOAT3 lightPos;
    XMFLOAT3 lightColor;
    XMFLOAT3 lightDir;
};

extern ModelSceneCB mscb;
// 实例数据是放在顶点缓冲里的
extern shared_ptr<DynamicInstaceBuffer<QuvierTreeModelInstanceData>> appQuvierTreeInstanceData;

extern shared_ptr<Light> light;

extern vector<QuvierTreeModelInstanceData> quvierTreeInstanceDatas;

// Cascade Shadow
struct CascadeShadowConstants
{
    XMFLOAT4X4 gLightViewProj[16];
    XMFLOAT4 gCascadeSplits;
    XMFLOAT4 gLightDirAndCount;
    XMFLOAT4 gShadowInfo;
};

extern CascadeShadowConstants csc;
extern shared_ptr<ConstantBuffer> appCascadeShadowCB;
extern shared_ptr<CascadeShadowMapCalculate> cascadeShadowMapCalculate;
extern shared_ptr<CascadeShadowMap> cascadeShadowMap;
// 只存 cascade shadow depth texture array 的 SRV，后续主渲染采样阴影图用。
extern shared_ptr<DescriptorHeap> appCascadeShadowDepthSrvDescHeap; // 原始 cascade shadow SRV 所在 heap，作为 descriptor 拷贝源。
// 只存 cascade shadow depth texture array 的 DSV，shadow pass 写深度用。
extern shared_ptr<DescriptorHeap> appCascadeShadowDepthDsvDescHeap; // cascade shadow DSV 所在 heap，shadow pass 写深度用。
// 地形主渲染 descriptor heap 中的 cascade shadow SRV 拷贝。
extern shared_ptr<DescriptorHandle> appTerrianCascadeShadowSrvDesc; // 原始 shadow SRV 拷贝到地形主渲染 heap 后的位置。
// 模型主渲染 descriptor heap 中的 cascade shadow SRV 拷贝。
extern shared_ptr<DescriptorHandle> appModelCascadeShadowSrvDesc; // 原始 shadow SRV 拷贝到模型主渲染 heap 后的位置。
extern shared_ptr<Shader> appTerrianCascadeShadowShader;
extern shared_ptr<Shader> appQuvierTreeCascadeShadowShader;
extern shared_ptr<RootSignature> appTerrianCascadeShadowRootSignature;
extern shared_ptr<RootSignature> appQuvierTreeCascadeShadowSignature;
void appModelInit();
void appCascadeShadowInit();
void appInitial(HWND hwnd);

void appCascadeShadowUpdate();
void appModelUpdate();
void appUpdate();

void appTerrianComputePass();
void appTerrianRenderPass();
void appCascadeShadowPass();
void appModelRenderPass();

void appRenderCallBack();

void appInputCallBack();




void appShutDown();

