#pragma pack_matrix(row_major)

#define SHADOW_CASCADE_LAYERS 5

cbuffer ModelCB : register(b0)
{
    float4x4 model;
};

cbuffer LightSpaceMatricesCB : register(b1)
{
    float4x4 lightSpaceMatrices[16];
};

struct DepthVSInput
{
    float3 Pos : POSITION;
};

struct DepthVSOutput
{
    float4 WorldPos : POSITION;
};

struct DepthGSOutput
{
    float4 Position : SV_POSITION;
    uint ArraySlice : SV_RenderTargetArrayIndex;
};

DepthVSOutput VSMain(DepthVSInput input)
{
    DepthVSOutput output;
    output.WorldPos = mul(float4(input.Pos, 1.0f), model);
    return output;
}

[maxvertexcount(3)]
[instance(SHADOW_CASCADE_LAYERS)]
void GSMain(
    triangle DepthVSOutput input[3],
    uint gsInstanceID  : SV_GSInstanceID,
    inout TriangleStream<DepthGSOutput> outputStream)
{
    for(int i = 0; i < 3; ++i)
    {
        DepthGSOutput output;
        output.Position = mul(input[i].WorldPos, lightSpaceMatrices[gsInstanceID ]);
        output.ArraySlice = gsInstanceID ;
        outputStream.Append(output);
    }

    outputStream.RestartStrip();
}

void PSMain(DepthGSOutput input)
{
    
}

