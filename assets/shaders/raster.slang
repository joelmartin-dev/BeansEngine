import common;

// Apply Transformations
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(camera.proj, mul(camera.view, mul(camera.model, float4(input.inPosition, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    output.fragNormal = input.inNormal;
    output.worldPos = mul(camera.model, float4(input.inPosition, 1.0)).xyz;
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

// Constantly-changing small data
struct PushConstant {
    uint materialID;
};
[push_constant] PushConstant pc;

// Sample the StorageImage using fullscreen triangle
[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 textureColour = textures[pc.materialID].Sample(textureSampler, vertIn.fragTexCoord);
    return textureColour * float4(vertIn.fragColor, 1.0);
    // return float4(vertIn.fragTexCoord, 0.0f, 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{

}