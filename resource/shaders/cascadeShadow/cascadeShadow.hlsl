#pragma pack_matrix(row_major) //行主序矩阵，乘法要用向量右乘矩阵

Texture2D diffuseTexture : register(t0);
Texture2DArray<float> shadowMap : register(t1);
SamplerState diffuseSampler : register(s0);
SamplerState shadowSampler : register(s1);

cbuffer CameraCB : register(b0)
{
    float4x4 projection;
    float4x4 view;
    float4x4 model;
};

cbuffer LightCB : register(b1)
{
    float3 lightDir;// 指向光源的方向
    float farPlane;// 远平面

    float3 viewPos;// 相机位置
    int cascadeCount;// 级联数量

    float cascadePlaneDistances[16];// 级联距离，实际只有5层级联阴影，也即是5个距离，这里给的冗余
};

cbuffer LightSpaceMatricesCB : register(b2)
{
    float4x4 lightSpaceMatrices[16];
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;//片段法线
    float2 TexCoords : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 FragPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 TexCoords : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.Pos, 1.0f), model);

    output.FragPos = worldPos.xyz;
    output.Normal = normalize(mul(float4(input.Normal, 0.0f), model).xyz);
    output.TexCoords = input.TexCoords;

    output.Position = mul(worldPos, view);
    output.Position = mul(output.Position, projection);
    return output;
}

float ShadowCalculation(float3 fragPosWorldSpace, float3 normal)
{
    float4 fragPosViewSpace = mul(float4(fragPosWorldSpace, 1.0f), view);
    float depthValue = abs(fragPosViewSpace.z);
    int layer = -1;
    [loop]
    for(int i = 0; i < cascadeCount; ++i)
    {
        if(depthValue < cascadePlaneDistances[i])
        {
            layer = i;
            break;
        }
    }

    if(layer == -1)
    {
        layer = cascadeCount;
    }

    float4 fragPosLightSpace = mul(float4(fragPosWorldSpace, 1.0f), lightSpaceMatrices[layer]);
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;// 透视除法 
    float2 shadowUV;
    // 转换到[0,1]范围
    shadowUV.x = projCoords.x * 0.5f + 0.5f;
    shadowUV.y = -projCoords.y * 0.5f + 0.5f;
    float currentDepth = projCoords.z;
    if (currentDepth > 1.0f || currentDepth < 0.0f)
    {
        return 0.0f;
    }

    float bias = max(0.05f * (1.0f - dot(normal, lightDir)), 0.005f);
    const float biasModifier = 0.5f;
    if (layer == cascadeCount)
    {
        bias *= 1.0f / (farPlane * biasModifier);
    }
    else
    {
        bias *= 1.0f / (cascadePlaneDistances[layer] * biasModifier);
    }

    uint width;
    uint height;
    uint elements;//输出 array slice 数量，比如 cascade shadow map 的层数
    uint levels;//输出 mipmap 层数
    shadowMap.GetDimensions(0, width, height, elements, levels);

    float2 texelSize = 1.0f / float2(width,height);
    float shadow = 0.0f;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offsetUV = shadowUV + float2(x, y) * texelSize;
            float pcfDepth = shadowMap.SampleLevel(shadowSampler, float3(offsetUV, (float)layer), 0).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0f : 0.0f;
        }
    }
    return shadow / 9.0f;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 color = diffuseTexture.Sample(diffuseSampler, input.TexCoords).rgb;
    float3 normal = normalize(input.Normal);
    float3 lightColor = float3(0.3f, 0.3f, 0.3f);

    float3 ambient = 0.3f * color;

    float diff = max(dot(lightDir, normal), 0.0f);
    float3 diffuse = diff * lightColor;

    float3 viewDir = normalize(viewPos - input.FragPos);
    float3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0f), 64.0f);
    float3 specular = spec * lightColor;

    float shadow = ShadowCalculation(input.FragPos, normal);
    float3 lighting = (ambient + (1.0f - shadow) * (diffuse + specular)) * color;

    return float4(lighting, 1.0f);
}
