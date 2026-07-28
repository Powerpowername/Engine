#pragma pack_matrix(row_major)

#define SHADOW_CASCADE_LAYERS 5

cbuffer SceneConstants : register(b0)
{
    row_major float4x4 gViewProj;
    float4 gCameraPos;
    int4 gLevelOffsets[10];
    float4 gInvLevelSizes[10];
    float4 gRenderClipmapParams;
    float4 gTerrainSizeCenter;
    float4 gRenderTerrainSampleParams;
    float4 gRenderFlags;
};

cbuffer CascadeShadowConstants : register(b1)
{
    row_major float4x4 gLightViewProj[16];
    float4 gCascadeSplits;
    float4 gLightDirAndCount;
    float4 gShadowInfo;
};

Texture2DArray gClipmapHeight : register(t0);
SamplerState gWrapSampler : register(s0);

struct VSInput
{
    uint2 grid : GRID;
    int2 instanceOffset : INSTANCE_OFFSET;
    uint instanceLevel : INSTANCE_LEVEL;
    uint instanceId : INSTANCE_ID;
};

struct VSOutput
{
    float4 worldPos : POSITION;
};

struct GSOutput
{
    float4 position : SV_POSITION;
    uint arraySlice : SV_RenderTargetArrayIndex;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    uint levelCount = max((uint)gRenderClipmapParams.z, 1u);
    uint level = min(input.instanceLevel, levelCount - 1u);
    bool hasNextLevel = (level + 1u) < levelCount;

    float clipmapScale = gRenderClipmapParams.w;
    float textureScale = gRenderTerrainSampleParams.z;
    uint levelStep = 1u << level;

    int2 levelOffset = gLevelOffsets[level].xy;
    float scale = clipmapScale * (float)levelStep;
    float2 localOffset = float2(input.grid) * scale;

    float2 meshPos = float2(input.instanceOffset + levelOffset) * clipmapScale;
    float2 worldXZ = meshPos + localOffset;
    int2 gridPos = input.instanceOffset + levelOffset;

    float2 offsetUv = frac((float2(gridPos) / (float)levelStep) * textureScale);
    float2 texcoord = offsetUv + (float2(input.grid) + 0.5f) * textureScale;

    float3 clipHigh = gClipmapHeight.SampleLevel(gWrapSampler, float3(texcoord, (float)level), 0).xyz;
    float3 clipLow = clipHigh;

    if(hasNextLevel)
    {
        uint2 modif = (uint2)(levelOffset - gLevelOffsets[level + 1u].xy + input.instanceOffset);
        uint2 v0 = (modif + ((input.grid + uint2(0u, 0u)) << level)) >> (level + 1u);
        uint2 v1 = (modif + ((input.grid + uint2(0u, 1u)) << level)) >> (level + 1u);
        uint2 v2 = (modif + ((input.grid + uint2(1u, 0u)) << level)) >> (level + 1u);
        uint2 v3 = (modif + ((input.grid + uint2(1u, 1u)) << level)) >> (level + 1u);

        float2 nextOffsetUv =
            (float2(gLevelOffsets[level + 1u].xy) / (float)(1u << (level + 1u))) * textureScale;

        clipLow = 0.0f;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(nextOffsetUv + (float2(v0) + 0.5f) * textureScale, (float)(level + 1u)), 0).xyz;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(nextOffsetUv + (float2(v1) + 0.5f) * textureScale, (float)(level + 1u)), 0).xyz;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(nextOffsetUv + (float2(v2) + 0.5f) * textureScale, (float)(level + 1u)), 0).xyz;
        clipLow += gClipmapHeight.SampleLevel(gWrapSampler, float3(nextOffsetUv + (float2(v3) + 0.5f) * textureScale, (float)(level + 1u)), 0).xyz;
        clipLow *= 0.25f;
    }

    float2 dist = abs(worldXZ - gCameraPos.xz) * gInvLevelSizes[level].x;
    float2 blendAxis = saturate((dist - 0.325f) * 8.0f);
    float lodFactor = hasNextLevel ? max(blendAxis.x, blendAxis.y) : 0.0f;

    float height = lerp(clipHigh.x, clipLow.x, lodFactor);
    output.worldPos = float4(worldXZ.x, height, worldXZ.y, 1.0f);
    return output;
}

[maxvertexcount(3)]
[instance(SHADOW_CASCADE_LAYERS)]
void GSMain(
    triangle VSOutput input[3],
    uint gsInstanceID : SV_GSInstanceID,
    inout TriangleStream<GSOutput> outputStream)
{
    for(int i = 0; i < 3; ++i)
    {
        GSOutput output;
        output.position = mul(input[i].worldPos, gLightViewProj[gsInstanceID]);
        output.arraySlice = gsInstanceID;
        outputStream.Append(output);
    }

    outputStream.RestartStrip();
}

void PSMain(GSOutput input)
{
}
