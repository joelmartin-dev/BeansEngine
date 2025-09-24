struct VSInput {
    float2 inPosition;
    float4 inColor;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float pointSize : SV_PointSize;
    float3 fragColor : COLOR0;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 fragColor : COLOR0;
    float2 pointCoord : SV_PointCoord;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pointSize = 20.0;
    output.pos = float4(input.inPosition, 0.0, 1.0);
    output.fragColor = input.inColor.rgb;
    //output.pointCoord = (input.inPosition * float2(1.0, -1.0) + float2(1.0)) / 2.0;
    return output;
}

[shader("fragment")]
float4 fragMain(PSInput input) : SV_TARGET {
    float2 coord = input.pointCoord - float2(0.5);
    return float4(input.fragColor, length(coord) < 0.5 ? 1.0 : 0.0);
}

struct Particle {
	float2 position;
	float2 velocity;
  float4 color;
};

struct UniformBuffer {
    float time;
    float deltaTime;
};
ConstantBuffer<UniformBuffer> ubo;

struct ParticleSSBO {
    Particle particles;
};
StructuredBuffer<ParticleSSBO> particlesIn;
RWStructuredBuffer<ParticleSSBO> particlesOut;


[shader("compute")]
[numthreads(256,1,1)]
void compMain(uint3 threadId : SV_DispatchThreadID)
{

}