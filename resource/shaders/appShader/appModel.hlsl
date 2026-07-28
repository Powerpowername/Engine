#pragma pack_matrix(row_major)

#include "pbrConfig.hlsl"

Texture2D baseColorTexture         : register(t0);
Texture2D normalTexture            : register(t1);
Texture2D metallicRoughnessTexture : register(t2);
Texture2DArray gCascadeShadowMap   : register(t3);
SamplerState linearWrapSampler     : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

cbuffer ModelSceneCB : register(b0, space0)
{
    float4x4 world;
    float4x4 viewProj;
    float3 cameraPos;
    float  padding0;

    float3 lightPos;
    float  padding1;

    float3 lightColor;
    float  exposure;
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
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
    float2 uv0       : TEXCOORD0;
    // 这里写成这种风格的原因是为了把实例矩阵传进来，input layout没有类型可以直接传矩阵
    // 实际上就是InstanceData
    float3 instancePosition : INSTANCE_POSITION;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 worldPos  : WORLD_POSITION;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
    float2 uv0       : TEXCOORD0;
};
// 当中normal texture 里颜色范围是 [0, 1]，要转换成-1，1
float3 DecodeNormal(float3 encodedNormal)
{
    return encodedNormal * 2.0f - 1.0f;
}
// 将切线空间里的法线转换成世界空间内的法线，这样计算才能正确
float3 GetWorldNormal(PSInput input, bool isFrontFace)
{
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);

    if (!isFrontFace)
    {
        N = -N;
        T = -T;
        B = -B;
    }

    float3 normalTS = DecodeNormal(normalTexture.Sample(linearWrapSampler, input.uv0).xyz);

    // glTF normal map is OpenGL style. If lighting looks inverted, flip Y here.
    normalTS.y = -normalTS.y;

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(normalTS, TBN));
}
// float4x4 用来将树放大十倍也就是10m
float4x4 instanceLocalScale = float4x4(
        10.0f, 0.0f,  0.0f,  0.0f,
        0.0f,  10.0f, 0.0f,  0.0f,
        0.0f,  0.0f,  10.0f, 0.0f,
        0.0f,  0.0f,  0.0f,  1.0f
    );

uint GetCascadeIndex(float3 worldPos, float3 cameraWorldPos)
{
    float cameraDistance = length(worldPos - cameraWorldPos);
    uint cascadeIndex = 0u;
    if(cameraDistance > gCascadeSplits.x) cascadeIndex = 1u;
    if(cameraDistance > gCascadeSplits.y) cascadeIndex = 2u;
    if(cameraDistance > gCascadeSplits.z) cascadeIndex = 3u;
    if(cameraDistance > gCascadeSplits.w) cascadeIndex = 4u;
    uint cascadeCount = max((uint)gLightDirAndCount.w, 1u);
    return min(cascadeIndex, cascadeCount - 1u);
}

float SampleCascadeShadow(float3 worldPos, float3 cameraWorldPos)
{
    uint cascadeIndex = GetCascadeIndex(worldPos, cameraWorldPos);
    float4 lightClip = mul(float4(worldPos, 1.0f), gLightViewProj[cascadeIndex]);
    float invW = rcp(max(lightClip.w, 0.0001f));
    float3 lightNdc = lightClip.xyz * invW;

    float2 shadowUv = float2(
        lightNdc.x * 0.5f + 0.5f,
        -lightNdc.y * 0.5f + 0.5f);

    if(shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
        shadowUv.y < 0.0f || shadowUv.y > 1.0f ||
        lightNdc.z < 0.0f || lightNdc.z > 1.0f)
    {
        return 1.0f;
    }

    float shadowDepth = lightNdc.z - gShadowInfo.y;
    return gCascadeShadowMap.SampleCmpLevelZero(
        gShadowSampler,
        float3(shadowUv, (float)cascadeIndex),
        shadowDepth);
}

PSInput VSMain(VSInput input)
{
    PSInput output;

    float4 worldPosition = mul(float4(input.position, 1.0f), world);
    worldPosition.xyz += input.instancePosition;

    output.position = mul(worldPosition, viewProj);
    output.worldPos = worldPosition.xyz;

    float3x3 normalWorld = (float3x3)world;
    output.normal = normalize(mul(input.normal, normalWorld));
    output.tangent = normalize(mul(input.tangent, normalWorld));
    output.bitangent = normalize(mul(input.bitangent, normalWorld));
    output.uv0 = input.uv0;

    return output;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float4 baseColorSample = baseColorTexture.Sample(linearWrapSampler, input.uv0);

    float3 albedo = baseColorSample.rgb;
    float alpha = baseColorSample.a;

    float3 arm = metallicRoughnessTexture.Sample(linearWrapSampler, input.uv0).rgb;

    float ao = max(arm.r, 0.2f);
    float roughness = saturate(arm.g);
    float metallic = saturate(arm.b);

    roughness = max(roughness, 0.04f);

    float3 N = GetWorldNormal(input, isFrontFace);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = normalize(-gLightDirAndCount.xyz);
    float3 H = normalize(V + L);

    // Test: keep distant light direction but disable distance attenuation.
    // float distanceSquared = max(dot(lightPos - input.worldPos, lightPos - input.worldPos), 0.001f);
    // float3 radiance = lightColor / distanceSquared;
    float3 radiance = lightColor;

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f);
    float3 specular = (NDF * G * F) / max(denominator, 0.001f);

    float NdotL = max(dot(N, L), 0.0f);
    float shadow = SampleCascadeShadow(input.worldPos, cameraPos);
    float3 directLight = (kD * albedo / PI + specular) * radiance * NdotL * shadow;
    float3 ambientLight = albedo * 0.08f;
    float3 color = (directLight + ambientLight) * ao;

    color = float3(1.0f, 1.0f, 1.0f) - exp(-color * exposure);
    color = pow(color, 1.0f / 2.2f);

    return float4(color, alpha);
}
