cbuffer SceneConstants : register(b0)
{
    // CPU 已经准备好的视图投影矩阵，把世界坐标直接变换到裁剪空间。
    row_major float4x4 gViewProj;
    // 相机世界坐标，xz 用来计算 LOD 混合和雾化距离。
    float4 gCameraPos;
    // 每层 clipmap 在全局 level 0 网格坐标中的左上角起点。
    int4 gLevelOffsets[10];
    // 每层整张环形缓存的世界尺寸倒数，用来把离相机距离归一化到 0..1。
    float4 gInvLevelSizes[10];
    // x=size，y=level_size，z=level_count，w=clipmap_scale。
    float4 gClipmapInfo;
    // xy=地形世界尺寸，zw=地形中心偏移。
    float4 gTerrainSizeCenter;
    // x=最小高度，y=最大高度，z=1/level_size，w=源高度图分辨率。
    float4 gTerrainHeightInfo;
    // 预留渲染开关，目前窗口/离线入口都保持默认值。
    float4 gRenderFlags;
};

// 每个 array slice 对应一层 clipmap，高度纹理里存 float4(height, dH/dx, dH/dz, unused)。
Texture2DArray gClipmapHeight : register(t0);
// clipmap 高度缓存是环形纹理，wrap 采样可以自然处理 texcoord 回卷。
SamplerState gWrapSampler : register(s0);

struct VSInput
{
    uint2 grid : GRID;// 在自己组件内的局部顶点坐标
    int2 instanceOffset : INSTANCE_OFFSET;
    uint instanceLevel : INSTANCE_LEVEL;
    uint instanceId : INSTANCE_ID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 lod : TEXCOORD2;
    float2 gridCoord : TEXCOORD3;
    float fog : TEXCOORD4;
    nointerpolation uint level : TEXCOORD5;
    nointerpolation uint blockId : TEXCOORD6;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // instanceLevel 来自 CPU 生成的实例；这里再次 clamp，防止无效实例访问数组越界。
    uint levelCount = max((uint)gClipmapInfo.z, 1u);
    output.level = min(input.instanceLevel, levelCount - 1u);
    output.blockId = input.instanceId;

    bool hasNextLevel = (output.level + 1u) < levelCount;
    float clipmapScale = gClipmapInfo.w;
    float textureScale = gTerrainHeightInfo.z;
    uint levelStep = 1u << output.level;

    // input.grid 是静态模板中的局部顶点坐标；instanceOffset 是该模板在本层中的局部摆放位置；
    // levelOffset 是该层在全局网格中的起点。三者合成后得到真实世界 XZ。
    int2 levelOffset = gLevelOffsets[output.level].xy;
    float scale = clipmapScale * (float)levelStep;
    float2 localOffset = float2(input.grid) * scale;

    float2 meshPos = float2(input.instanceOffset + levelOffset) * clipmapScale;
    float2 worldXZ = meshPos + localOffset;
    int2 gridPos = levelOffset + input.instanceOffset;

    // clipmap 高度图是环形缓存：gridPos / levelStep 转为当前层 texel，再 frac 到 0..1。
    // 加 0.5 是采样 texel 中心，避免落在边界造成双线性混合偏移。
    float2 offsetUv = frac((float2(gridPos) / (float)levelStep) * textureScale);
    float2 texcoord = offsetUv + (float2(input.grid) + 0.5) * textureScale;

    // 高精度层直接给当前顶点高度和梯度。
    float3 clipHigh = gClipmapHeight.SampleLevel(gWrapSampler, float3(texcoord, (float)output.level), 0).xyz;
    float3 clipLow = clipHigh;

    if (hasNextLevel)
    {
        // 为了跨 LOD 无裂缝，当前层边缘会逐渐混合到下一层的粗高度。
        // v0..v3 是当前高精度顶点覆盖到下一层粗 texel 的四个邻点，平均后得到低精度高度。
        uint2 modif = (uint2)(levelOffset - gLevelOffsets[output.level + 1u].xy + input.instanceOffset);
        uint2 v0 = (modif + ((input.grid + uint2(0u, 0u)) << output.level)) >> (output.level + 1u);
        uint2 v1 = (modif + ((input.grid + uint2(0u, 1u)) << output.level)) >> (output.level + 1u);
        uint2 v2 = (modif + ((input.grid + uint2(1u, 0u)) << output.level)) >> (output.level + 1u);
        uint2 v3 = (modif + ((input.grid + uint2(1u, 1u)) << output.level)) >> (output.level + 1u);

        float2 nextOffsetUv =
            (float2(gLevelOffsets[output.level + 1u].xy) / (float)(1u << (output.level + 1u))) * textureScale;

        float2 texcoordV0 = nextOffsetUv + (float2(v0) + 0.5) * textureScale;
        float2 texcoordV1 = nextOffsetUv + (float2(v1) + 0.5) * textureScale;
        float2 texcoordV2 = nextOffsetUv + (float2(v2) + 0.5) * textureScale;
        float2 texcoordV3 = nextOffsetUv + (float2(v3) + 0.5) * textureScale;

        clipLow = float3(0.0, 0.0, 0.0);
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(texcoordV0, (float)(output.level + 1u)), 0).xyz;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(texcoordV1, (float)(output.level + 1u)), 0).xyz;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(texcoordV2, (float)(output.level + 1u)), 0).xyz;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(texcoordV3, (float)(output.level + 1u)), 0).xyz;
        clipLow *= 0.25;
    }

    // 离当前层中心越远，越接近外圈边缘；外圈边缘使用下一层高度以隐藏 LOD 接缝。
    float2 dist = abs(worldXZ - gCameraPos.xz) * gInvLevelSizes[output.level].x;
    float2 blendAxis = saturate((dist - 0.325) * 8.0);
    float lodFactor = hasNextLevel ? max(blendAxis.x, blendAxis.y) : 0.0;

    float height = lerp(clipHigh.x, clipLow.x, lodFactor);
    // compute shader 写入的是高度梯度，shader 里还原为近似法线。
    float3 normalHigh = normalize(float3(-clipHigh.y, 1.0, -clipHigh.z));
    float3 normalLow = normalize(float3(-clipLow.y, 1.0, -clipLow.z));

    output.worldPos = float3(worldXZ.x, height, worldXZ.y);
    output.normal = normalize(lerp(normalHigh, normalLow, lodFactor));
    output.lod = float2((float)output.level, lodFactor);
    output.gridCoord = float2(gridPos) + float2(input.grid) * (float)levelStep;
    float3 distCamera = gCameraPos.xyz - output.worldPos;
    output.fog = saturate(dot(distCamera, distCamera) / 25000000.0);
    output.position = mul(float4(output.worldPos, 1.0), gViewProj);
    return output;
}

float4 PSSurface(VSOutput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(0.45, 0.85, 0.25));
    float diffuse = saturate(dot(lightDir, input.normal));
    float3 baseColor = lerp(float3(0.50, 0.50, 0.48), float3(0.72, 0.72, 0.69), saturate(diffuse));
    baseColor = lerp(baseColor, float3(0.62, 0.68, 0.72), input.fog);
    return float4(baseColor, 1.0);
}

float4 PSGrid(VSOutput input) : SV_TARGET
{
    return float4(0.12, 0.95, 0.68, 1.0);
}

cbuffer ComputeConstants : register(b0)
{
    // x=level_size，y=预留，z=源高度图分辨率，w=本帧更新矩形数量。
    uint4 gComputeInfo;
    // xy=地形世界尺寸，zw=地形中心偏移。
    float4 gComputeTerrainInfo;
    // x=clipmap_scale，即 level 0 一个网格单位对应的世界距离。
    float4 gComputeScaleInfo;
    // 每个更新任务：xy=写入 clipmap 纹理的起点，zw=更新区域大小。
    int4 gUpdateTexSize[80];
    // 每个更新任务：xy=源地形逻辑 texel 起点，z=clipmap level。
    int4 gUpdateStartLevel[80];
};

// 原始完整地形高度图，由 CPU 从 TerrainData 上传。
Texture2D<float> gSourceHeight : register(t0);
// clipmap 环形高度缓存，array slice 是 level，float4 存高度和梯度。
RWTexture2DArray<float4> gOutputHeight : register(u0);
SamplerState gClampSampler : register(s0);

float2 WorldToSourceUv(float2 worldXZ)
{
    // TerrainData 的世界坐标以 centerOffset 为中心，转换到源高度图 0..1 UV。
    return (worldXZ - gComputeTerrainInfo.zw) / gComputeTerrainInfo.xy + float2(0.5, 0.5);
}

float3 GetTerrain(float2 worldXZ)
{
    // 超出地形边界时 clamp 到边缘，避免采样到无效高度。
    float2 uv = clamp(WorldToSourceUv(worldXZ), float2(0.0, 0.0), float2(1.0, 1.0));
    float height = gSourceHeight.SampleLevel(gClampSampler, uv, 0);

    // 通过左右/上下各采样一次估算高度梯度，渲染阶段用梯度重建法线。
    float safeResolution = (float)max((int)gComputeInfo.z - 1, 1);
    float2 texelWorldSize = gComputeTerrainInfo.xy / float2(safeResolution, safeResolution);
    float2 uvLeft = clamp(WorldToSourceUv(worldXZ - float2(texelWorldSize.x, 0.0)), float2(0.0, 0.0), float2(1.0, 1.0));
    float2 uvRight = clamp(WorldToSourceUv(worldXZ + float2(texelWorldSize.x, 0.0)), float2(0.0, 0.0), float2(1.0, 1.0));
    float2 uvDown = clamp(WorldToSourceUv(worldXZ - float2(0.0, texelWorldSize.y)), float2(0.0, 0.0), float2(1.0, 1.0));
    float2 uvUp = clamp(WorldToSourceUv(worldXZ + float2(0.0, texelWorldSize.y)), float2(0.0, 0.0), float2(1.0, 1.0));

    float heightLeft = gSourceHeight.SampleLevel(gClampSampler, uvLeft, 0);
    float heightRight = gSourceHeight.SampleLevel(gClampSampler, uvRight, 0);
    float heightDown = gSourceHeight.SampleLevel(gClampSampler, uvDown, 0);
    float heightUp = gSourceHeight.SampleLevel(gClampSampler, uvUp, 0);

    float2 gradient = float2(
        (heightRight - heightLeft) / max(2.0 * texelWorldSize.x, 0.0001),
        (heightUp - heightDown) / max(2.0 * texelWorldSize.y, 0.0001));
    return float3(height, gradient);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // z 维对应一个 HeightmapUpdateInfo，xy 维对应该矩形内部的 texel。
    uint updateIndex = dispatchThreadId.z;
    if (updateIndex >= gComputeInfo.w) {
        return;
    }

    int4 texSize = gUpdateTexSize[updateIndex];
    int4 startLevel = gUpdateStartLevel[updateIndex];
    if (dispatchThreadId.x >= (uint)texSize.z || dispatchThreadId.y >= (uint)texSize.w) {
        return;
    }

    uint level = (uint)startLevel.z;
    int levelStep = 1 << level;
    int2 levelTexel = startLevel.xy + int2(dispatchThreadId.xy);
    // startLevel 是该层 texel 坐标，乘 2^level 回到 level 0 网格，再乘 clipmap_scale 得到世界 XZ。
    float2 worldXZ = gComputeScaleInfo.x * float2(levelTexel * levelStep);
    float3 value = GetTerrain(worldXZ);
    // texSize.xy 是环形纹理写入起点，dispatchThreadId.xy 是矩形内偏移。
    gOutputHeight[int3(texSize.xy + int2(dispatchThreadId.xy), (int)level)] = float4(value, 0.0);
}
