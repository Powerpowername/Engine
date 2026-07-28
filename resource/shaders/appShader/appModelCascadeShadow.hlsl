#pragma pack_matrix(row_major)
#define SHADOW_CASCADE_LAYERS 5
cbuffer ModelSceneCB : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProj;
    float3 cameraPos;
    float padding0;
    float3 lightPos;
    float padding1;
    float3 lightColor;
    float exposure;
};

cbuffer CascadeShadowConstants : register(b1)
{
    row_major float4x4 gLightViewProj[16];
    float4 gCascadeSplits;
    float4 gLightDirAndCount;
    float4 gShadowInfo;
};

struct VSInput
{
    float3 position : POSITION;
    float3 instancePosition : INSTANCE_POSITION;
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

    float4 worldPosition = mul(float4(input.position, 1.0f), world);
    worldPosition.xyz += input.instancePosition;

    output.worldPos = worldPosition;
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
