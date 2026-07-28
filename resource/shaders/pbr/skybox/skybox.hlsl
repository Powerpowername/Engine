#pragma pack_matrix(row_major)

TextureCube skyBoxTexture : register(t0);
SamplerState skyBoxSampler : register(s0);

cbuffer SkyBoxCB : register(b0)
{
    float4x4 viewProjection;
};

struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.direction = input.position;

    float4 clipPosition = mul(float4(input.position, 1.0f), viewProjection);
    output.position = clipPosition.xyww;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 color = skyBoxTexture.Sample(skyBoxSampler, normalize(input.direction)).rgb;
    return float4(color, 1.0f);
}
