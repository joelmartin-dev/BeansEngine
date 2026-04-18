import common;

// // Apply Transformations
// [shader("vertex")]
// VSOutput vertMain(VSInput input) {
//     VSOutput output;
//     output.pos = mul(camera.proj, mul(camera.view, mul(camera.model, float4(input.inPosition, 1.0))));
//     output.fragColor = input.inColor;
//     output.fragTexCoord = input.inTexCoord;
//     output.fragNormal = input.inNormal;
//     output.worldPos = mul(camera.model, float4(input.inPosition, 1.0)).xyz;
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

// Constantly-changing small data
struct PushConstant {
    uint level;
    uint maxLevel;
    uint baseRayCount;
    // uint2 c0ProbeCount;
    float interval;
    float sunIntensity;
    float sunDirection;
};
[push_constant] PushConstant pc;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET
{
    float4 outColour = float4(0.0);
    for (uint level = 0; level < pc.maxLevel; level++) {
        outColour += float4(renderTextures[level].Sample(sampler, vertIn.fragTexCoord).rgb, 1.0);
    }
    return outColour;
    // return float4(renderTextures[2].Sample(sampler, vertIn.fragTexCoord).rgb, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{
    const uint2 pixel_coords = threadIdx.xy;

    // Get StorageImage's resolution
    uint2 img_size;
    outImage[pc.level].GetDimensions(img_size.x, img_size.y);

    // if this pixel is not in the image, don't do the work
    if (pixel_coords.x >= img_size.x || pixel_coords.y >= img_size.y) return;

    const float2 pixel_center = (float2)pixel_coords + float2(0.5);
    const float2 clip_coords = pixel_center / (float2)img_size.xy * 2.0 - 1.0;     // Convert to NDC [-1,1]
    const float4 view_coords = mul(camera.invProj, float4(clip_coords, 1.0, 1.0));  // Transform to view space

    const float3 project_ray_along_forward = normalize(mul(camera.invView, float4(normalize(view_coords.xyz), 0.0)).xyz); // Ray direction in world space
    const float3 initial_ray_origin = mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz + project_ray_along_forward * 4.0;

    const float GOLDEN_RATIO = (1.0 + pow(5.0, 0.5)) / 2.0;
    const float delta_theta = 2.0 * float.getPi() / GOLDEN_RATIO;
    const float delta_phi = 2.0 / float((pc.level + 1) * 8);
    // const float delta_theta = float.getPi() / (float(pc.level + 1) * 4.0);
    float3 radiance = float3(0.0);
    for (uint ray_idx = 0; ray_idx < (pc.level + 1) * 8; ray_idx++) {
        const float theta = delta_theta * (float(ray_idx) + 0.5) ;
        const float phi = acos(1.0 - delta_phi * float(ray_idx));
        const float3 ray_direction = normalize(mul(camera.invView, normalize(float4(
            cos(theta) * sin(phi),
            sin(theta) * sin(phi),
            cos(phi), 0.0
        ))).xyz);
        // const float3 ray_direction = normalize(mul(camera.invView, normalize(float4(
        //     cos(theta), sin(theta), 0.0, 0.0
        // ))).xyz);

        RayDesc ray_desc = {
            initial_ray_origin + ray_direction * pc.level * (pc.interval / 1000.0),
            0.0,
            ray_direction,
            // 1000.0
            pc.interval / 1000.0
        };

        const uint ray_flags = RAY_FLAG_NONE;
        RayQuery<ray_flags> ray_query;

        ray_query.TraceRayInline(accelerationStructure, ray_flags, 0xFF, ray_desc);
        while (ray_query.Proceed())
        {
            ray_query.CommitNonOpaqueTriangleHit();
        }

        if (ray_query.CommittedStatus() == COMMITTED_NOTHING)
        {
            continue;
        }

        uint instance_idx = ray_query.CommittedInstanceIndex();
        uint prim_idx = ray_query.CommittedPrimitiveIndex();

        const float3x3 prim_attribs = getPrimAttribs(instance_idx, prim_idx, ray_query.CommittedTriangleBarycentrics());
        const float2 uv = prim_attribs[1].xy;

        uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instance_idx)].materialID;
        uint _width, _height, level_count;
        textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, level_count);
        float3 texture_sample = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, ray_query.CommittedRayT() / (float)level_count).xyz;

        radiance += float3(texture_sample / ((float)(ray_idx + 1) * 8.0));
    }

    outImage[pc.level][pixel_coords] = float4(radiance, 1.0);
    //float4((float)pixel_coords.x / (float)img_size.x, (float)pixel_coords.y / (float)img_size.y, 0.0, 1.0);
}