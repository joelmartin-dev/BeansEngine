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

struct UniformBuffer {
    float time;
    float deltaTime;
};
ConstantBuffer<UniformBuffer> ubo;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_Target
{
    return float4(vertIn.fragTexCoord, 0.0, 0.0);//(sin(ubo.time) + 1.0) / 10.0);
}

/*
[shader("compute")]
[numthreads(256, 1, 1)]
void compMain(uint3 threadId: SV_DispatchThreadID)
{

}
*/