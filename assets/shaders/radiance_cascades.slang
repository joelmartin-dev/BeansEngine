struct VSInput {
    float3 inPosition;
    float3 inColor;
    float2 inTexCoord;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 color;
    float2 fragTexCoord;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.pos = float4(input.inPosition, 1.0);
    output.color = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

struct PInput {
    float2 pos : SV_PointCoord;
    float3 col : COLOR0;
};

[shader("fragment")]
float4 fragMain(PInput pointIn) : SV_TARGET
{
    float2 coord = pointIn.pos - float2(0.5);
    return float4(pointIn.col, 0.5 - length(coord));
}

struct Cascade {
    int2 renderExtent;
    float2 cascadeExtent;
    int cascadeCount;
    int cascadeIndex;
    float cascadeLinear;
    float cascadeInterval;
};
//ConstantBuffer<Cascade> cascade;

[shader("compute")]
[numthreads(1,1,1)]
void compMain(uint3 threadId : SV_DispatchThreadID)
{

}