#pragma pack_matrix(row_major)

#include "pbrConfig.hlsl"

Texture2D baseColorTexture         : register(t0);
Texture2D normalTexture            : register(t1);
Texture2D metallicRoughnessTexture : register(t2);
SamplerState linearWrapSampler     : register(s0);

cbuffer SceneCB : register(b0, space0)
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

struct VSInput
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
    float2 uv0       : TEXCOORD0;
    // 这里写成这种风格的原因是为了把实例矩阵传进来，input layout没有类型可以直接传矩阵
    // 实际上就是InstanceData
    float4 instanceWorld0 : INSTANCEWORLD0;
    float4 instanceWorld1 : INSTANCEWORLD1;
    float4 instanceWorld2 : INSTANCEWORLD2;
    float4 instanceWorld3 : INSTANCEWORLD3;
    float4 instanceInfo   : INSTANCEINFO0;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float3 tangent   : TEXCOORD2;
    float3 bitangent : TEXCOORD3;
    float2 uv0       : TEXCOORD4;
    float4 instanceInfo : TEXCOORD5;
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

PSInput VSMain(VSInput input)
{
    PSInput output;

    float4x4 instanceWorld = float4x4(
        input.instanceWorld0,
        input.instanceWorld1,
        input.instanceWorld2,
        input.instanceWorld3);

    float4 localPosition = mul(float4(input.position, 1.0f), instanceWorld);
    float4 worldPosition = mul(localPosition, world);
    output.position = mul(worldPosition, viewProj);
    output.worldPos = worldPosition.xyz;

    float3x3 instanceNormalWorld = (float3x3)instanceWorld;
    output.normal = normalize(mul(mul(input.normal, instanceNormalWorld), (float3x3)world));
    output.tangent = normalize(mul(mul(input.tangent, instanceNormalWorld), (float3x3)world));
    output.bitangent = normalize(mul(mul(input.bitangent, instanceNormalWorld), (float3x3)world));
    output.uv0 = input.uv0;
    output.instanceInfo = input.instanceInfo;

    return output;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    if (input.instanceInfo.x > 0.5f)
    {
        float3 N = normalize(input.normal);
        if (!isFrontFace)
            N = -N;

        float3 L = normalize(lightPos - input.worldPos);
        float3 V = normalize(cameraPos - input.worldPos);
        float3 H = normalize(V + L);
        float distanceSquared = max(dot(lightPos - input.worldPos, lightPos - input.worldPos), 0.001f);
        float3 radiance = lightColor / distanceSquared;

        float3 albedo = float3(0.20f, 0.35f, 0.17f);
        float roughness = 0.85f;
        float metallic = 0.0f;
        float3 F0 = float3(0.04f, 0.04f, 0.04f);

        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);
        float3 kD = (1.0f - F) * (1.0f - metallic);
        float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f);
        float3 specular = (NDF * G * F) / max(denominator, 0.001f);
        float3 color = (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0f);
        color += albedo * 0.08f;
        color = float3(1.0f, 1.0f, 1.0f) - exp(-color * exposure);
        color = pow(color, 1.0f / 2.2f);
        return float4(color, 1.0f);
    }

    float4 baseColorSample = baseColorTexture.Sample(linearWrapSampler, input.uv0);

    float3 albedo = baseColorSample.rgb;
    float alpha = baseColorSample.a;

    float3 arm = metallicRoughnessTexture.Sample(linearWrapSampler, input.uv0).rgb;

    float ao = arm.r;
    float roughness = saturate(arm.g);
    float metallic = saturate(arm.b);

    roughness = max(roughness, 0.04f);

    float3 N = GetWorldNormal(input, isFrontFace);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = normalize(lightPos - input.worldPos);
    float3 H = normalize(V + L);

    float distanceSquared = max(dot(lightPos - input.worldPos, lightPos - input.worldPos), 0.001f);
    float3 radiance = lightColor / distanceSquared;

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f);
    float3 specular = (NDF * G * F) / max(denominator, 0.001f);

    float NdotL = max(dot(N, L), 0.0f);
    float3 color = (kD * albedo / PI + specular) * radiance * NdotL;

    color *= ao;

    color = float3(1.0f, 1.0f, 1.0f) - exp(-color * exposure);
    color = pow(color, 1.0f / 2.2f);

    return float4(color, alpha);
}
