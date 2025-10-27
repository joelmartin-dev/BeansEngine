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

// Apply Transformations
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    output.fragNormal = input.inNormal;
    output.worldPos = mul(ubo.model, float4(input.inPosition, 1.0)).xyz;
    return output;
}

// // Maintain screenspace positions
// [shader("vertex")]
// VSOutput vertMain(VSInput input) {
//     VSOutput output;
//     output.pos = float4(input.inPosition, 1.0);
//     output.fragColor = input.inColor;
//     output.fragTexCoord = input.inTexCoord;
//     output.fragNormal = input.inNormal;
//     output.worldPos = input.inPosition;
//     return output;
// }

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

// // Sampler of the written-to image
// [[vk::binding(7, 0)]] Sampler2D sampler;

// // Write access to StorageImage
// [vk_image_format("rgba32f")]
// [[vk::binding(8, 0)]] RWTexture2D<float4> outImage;

// Sampler for texture array
[[vk::binding(0,1)]] SamplerState textureSampler;

// the array of the scene's textures (texture atlas)
[[vk::binding(1,1)]] Texture2D<float4> textures[];

// Constantly-changing small data
struct PushConstant {
    uint materialID;
};
[push_constant] PushConstant pc;

// Sample the StorageImage using fullscreen triangle
[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 textureColour = textures[pc.materialID].Sample(textureSampler, vertIn.fragTexCoord);
    return float4(textureColour.rgb * vertIn.fragColor, textureColour.a);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{

}