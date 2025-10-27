// Mirrored struct of Vertex in Vertex.hpp
struct VSInput {
    float3 inPosition;
    float3 inColor;
    float2 inTexCoord;
    float3 inNormal;
};

// Internal struct for passing attributes to fragment shader
struct VSOutput
{
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
    float3 fragNormal;
    float3 worldPos;
};

// 3D Transformation Matrices + Inversions
struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 invView;
    float4x4 proj;
    float4x4 invProj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

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
    output.pos = float4(input.inPosition, 1.0);
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    output.fragNormal = input.inNormal;
    output.worldPos = input.inPosition;
    return output;
}

// // the scene's tlas
// [[vk::binding(1, 0)]] RaytracingAccelerationStructure accelerationStructure;

// the indices of the scene
[[vk::binding(2, 0)]] StructuredBuffer<uint> indexBuffer;

// the vertex colours of the scene
[[vk::binding(3, 0)]] StructuredBuffer<uint> colourBuffer;

// the uvs of each vertex in the scene
[[vk::binding(4, 0)]] StructuredBuffer<float2> uvBuffer;

// // the normals of each vertex in the scene
// [[vk::binding(5, 0)]] StructuredBuffer<float4> nrmBuffer;

// // Each submesh's unique attributes
// struct InstanceLUT {
//     uint materialID; // submesh's material (corresponds to texture array index)
//     uint indexBufferOffset; // lookup indices > indexBufferOffset for other attributes (uvs, normals)
// };
// [[vk::binding(6, 0)]] StructuredBuffer<InstanceLUT> instanceLUTBuffer;

// Sampler of the written-to image
[[vk::binding(7, 0)]] Sampler2D sampler;

// // Write access to StorageImage
// [vk_image_format("rgba32f")]
// [[vk::binding(8, 0)]] RWTexture2D<float4> outImage;

// Sampler for texture array
[[vk::binding(0, 1)]] SamplerState textureSampler;

// the array of the scene's textures (texture atlas)
[[vk::binding(1, 1)]] Texture2D<float4> textures[];

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
    uint width, height;
    sampler.GetDimensions(width, height);
    float2 screenUV = float2(vertIn.pos.xy) / float2(width, height);
    return cascadeN(0, screenUV);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{

}