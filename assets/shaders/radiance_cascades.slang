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
    output.pos = (input.cubeID == 1) ? mul(camera.proj, mul(camera.view, mul(camera.model, float4(input.inPosition, 1.0)))) : float4(input.inPosition, 1.0);
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
    float interval;
};
[push_constant] PushConstant pc;

float3 GatherFluence(uint2 globalPixel, const uint maxProbeWidth)
{
    const uint minProbeWidth = (maxProbeWidth / (1 << (pc.maxLevel - 1)));
    uint currentProbeWidth = maxProbeWidth;
    
    const uint maxLevelFluenceCount = 1 << (pc.maxLevel - 1);
    uint currentLevelFluenceCount = maxLevelFluenceCount;
    
    float3 totalSummedCol = float3(0.0f);
    
    for (uint level = pc.maxLevel - 1; level >= 0; level--) {
        float3 levelSummedCol = float3(0.0f);
        uint2 firstLocalPixel = globalPixel % currentProbeWidth;
        for (uint i = 0; i < currentLevelFluenceCount; i++) {
            uint2 fluence = firstLocalPixel + uint2(i, 0);
            while (fluence.x >= currentProbeWidth) {
                fluence = uint2(
                    fluence.x % currentProbeWidth,
                    fluence.y + 1
                );
                if (fluence.y >= currentProbeWidth) fluence = uint2(fluence.x, 0);
            }
            if (fluence.y >= currentProbeWidth) fluence = uint2(fluence.x, 0);
            
            // Convert fluence coords to global coords
            fluence = globalPixel - firstLocalPixel + fluence;
            
            levelSummedCol += renderTextures[level].Sample(sampler, fluence / float(maxProbeWidth)).rgb;
        }
        totalSummedCol += levelSummedCol / float(currentLevelFluenceCount * 2.0f);
        currentProbeWidth /= 2;
        currentLevelFluenceCount /= 2;
    }
    return totalSummedCol;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET
{
    uint width, height;
    renderTextures[0].GetDimensions(width, height);
    uint2 pixel = uint2(round(vertIn.fragTexCoord * float2(width, height)));
    float3 radiance = GatherFluence(pixel, width);
    return float4(radiance, 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{
    uint width, height;
    outImage[pc.level].GetDimensions(width, height);
    const uint2 globalPixel = threadIdx.xy;
    if (globalPixel.x >= width || globalPixel.y >= height) return;

    // Get side length of probe's kernel: width / 2^n
    const uint probeWidth = (width / (1 << (pc.maxLevel - (pc.level + 1))));
    // Transform the image pixel coordinates to the local pixel coordinates of the probe's kernel
    const uint2 localPixel = globalPixel % probeWidth;

    // const float2 screenUV = (float2)localPixel / (float)probeWidth;
    // outImage[pc.level][globalPixel] = float4(screenUV, 0.0f, 1.0f);
    // return;

    // Global location of the probe's (0,0) pixel, in the [0, 1] range
    const float2 probeOrigin = float2(globalPixel - localPixel) / (float)width;

    // Screen position of the probe
    const float2 probeCenter = (float2)probeOrigin + ((float)probeWidth / (float) width) / 2.0f;
    // Calculate the angle between each ray direction in the probe
    const float deltaTheta = -2.0f / float(probeWidth * probeWidth);
    // Determine the direction of this thread
    const float theta = -1 + deltaTheta / 2.0f + (localPixel.x + localPixel.y * (float)probeWidth) * deltaTheta;
    const float2 rayDirection = float2(cospi(theta), -sinpi(theta));

    // outImage[pc.level][globalPixel] = float4(rayDirection, 0.0f, 1.0f);
    // return;

    const float2 pixelCenter = (float2)globalPixel;// + subpixelJitter;             // Jittered pixel position
    const float2 clipCoords = pixelCenter / float2(width, height) * 2.0 - 1.0;      // Convert to NDC [-1,1]
    const float4 viewCoords = mul(camera.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space



    // Shoot ray from probeOrigin in rayDirection
    RayDesc rayDesc;
    rayDesc.Origin =
        // Screenspace center
        float3(probeCenter, 0.0f)
        // Move in 5 units along the camera's forward vector
        + mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz * 5.0f
        // Start from the end interval of the previous cascade
        + float3(rayDirection, 0.0f) * pc.interval * float(pc.level - 1) * float(pc.level) / 2.0f
        // Add world space camera transform
        + mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    float4 rightVector = camera.invView[0];
    float4 upVector = camera.invView[1];

    rayDesc.Direction = normalize((rayDirection.x * rightVector + rayDirection.y * upVector).xyz);
    rayDesc.TMin = 0.0;
    rayDesc.TMax = pc.interval * (pc.level + 1);

    RayQuery<RAY_FLAG_NONE> rq;
    let rayFlags = RAY_FLAG_NONE;

    rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);

    while (rq.Proceed()) {
        const uint instanceID = rq.CandidateInstanceIndex();
        const uint primIndex = rq.CandidatePrimitiveIndex();

        const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
        const float2 uv = primAttribs[1].xy;

        const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

        uint _width, _height, levelCount;
        textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
        const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);

        if (intersectionColor.a >= 0.5)
            rq.CommitNonOpaqueTriangleHit();
    }

    if (rq.CommittedStatus() == COMMITTED_NOTHING) {
        outImage[pc.level][globalPixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const uint instanceID = rq.CommittedInstanceIndex();
    const uint primIndex = rq.CommittedPrimitiveIndex();

    const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CommittedTriangleBarycentrics());
    const float3 col = primAttribs[0];
    const float2 uv = primAttribs[1].xy;

    outImage[pc.level][globalPixel] = float4(col, 1.0f);
    // const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

    // uint _width, _height, levelCount;
    // textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
    // const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);
    // outImage[pc.level][globalPixel] = float4(intersectionColor.rgb * col, intersectionColor.a);
}