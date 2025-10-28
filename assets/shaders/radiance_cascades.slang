import "common";

// // Apply Transformations
// [shader("vertex")]
// VSOutput vertMain(VSInput input) {
//     VSOutput output;
//     output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
//     output.fragColor = input.inColor;
//     output.fragTexCoord = input.inTexCoord;
//     output.fragNormal = input.inNormal;
//     output.worldPos = mul(ubo.model, float4(input.inPosition, 1.0)).xyz;
//     return output;
// }

// Maintain screenspace positions
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    output.fragNormal = input.inNormal;
    output.worldPos = input.inPosition;
    return output;
}

// Constantly-changing small data
struct PushConstant {
    uint level;
};
[push_constant] PushConstant pc;

struct Ray {
    float2 direction;
    float magnitude;
};

struct Probe {
    float2 pos;
    int rayCount;
};

float sdSegment(float2 p, float2 a, float2 b, float r) {
    float h = min(0.2f, max(0.0f, dot(p - a, b - a) / dot(b - a, b - a)));
    return length(p - a - (b - a) * h) - r;
}

float4 cascadeN(int cascade, float2 texCoord) {
    Probe probes[4];
    probes[0] = Probe(float2(0.25f, 0.25f), 4); // Top Left
    probes[1] = Probe(float2(0.75f, 0.25f), 4); // Top Right
    probes[2] = Probe(float2(0.25f, 0.75f), 4); // Bottom Left
    probes[3] = Probe(float2(0.75f, 0.75f), 4); // Bottom Right

    for (int probeIdx = 0; probeIdx < 4; probeIdx++) {
        for (int rayIdx = 0; rayIdx < 4; rayIdx++) {
            const Ray ray = Ray(normalize(float2(cospi(rayIdx / 2.0f), sinpi(rayIdx / 2.0f))), 0.001f);
            const float3 cyan = float3(0.0f, 1.0f, 1.0f);
            if (sdSegment(texCoord, probes[probeIdx].pos, probes[probeIdx].pos + ray.direction, ray.magnitude) < 0.0001f) {
                return float4(cyan, 1.0f);
            }
        }
    }
    return float4(float3(0.0), 1.0);
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET
{
    return float4(vertIn.fragTexCoord, 0.0f, 1.0f);
    // uint width, height;
    // sampler.GetDimensions(width, height);
    // float2 screenUV = float2(vertIn.pos.xy) / float2(width, height);
    // return cascadeN(0, screenUV);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{

}