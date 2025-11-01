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
    output.cubeID = input.cubeID;
    return output;
}

// Constantly-changing small data
struct PushConstant {
    uint level;
    uint maxLevel;
    uint2 c0ProbeCount;
    float interval;
    float sunIntensity;
    float sunDirection;
};
[push_constant] PushConstant pc;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET
{
    uint width, height;
    renderTextures[0].GetDimensions(width, height);
    
    uint2 globalPixel = uint2(vertIn.fragTexCoord * float2(width, height));
    // return renderTextures[0][globalPixel];
    const uint probeWidth = width / (pc.c0ProbeCount[0] >> pc.level);
    const uint rootProbeCount = width / probeWidth;
    const uint probeHeight = height / rootProbeCount;
    const uint2 localPixel = globalPixel % uint2(probeWidth, probeHeight);

    // Global location of the probe's (0,0) pixel
    const uint2 probeOrigin = uint2(globalPixel - localPixel);

    float3 summedCol = float3(0.0f);
    for (uint i = 0; i < probeWidth; i++) {
        for (uint j = 0; j < probeWidth; j++) {
            summedCol += renderTextures[0].Sample(sampler, (probeOrigin + uint2(i, j)) / float2(width, height)).rgb;
        }
    }
    summedCol /= float(probeWidth * probeWidth);

    return float4(summedCol, 1.0f);
}

[shader("compute")]
[numthreads(4, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{
    uint width, height;
    outImage[pc.level].GetDimensions(width, height);
    const uint2 globalPixel = threadIdx.xy;
    if (globalPixel.x >= width || globalPixel.y >= height) return;

    // Get side length of probe's kernel: width / 2^(M - (L + 1))
    const uint probeWidth = width / (pc.c0ProbeCount[0] >> pc.level);
    const uint rootProbeCount = width / probeWidth;
    const uint probeHeight = height / rootProbeCount;
    // Transform the image pixel coordinates to the local pixel coordinates of the probe's kernel
    const uint2 localPixel = globalPixel % uint2(probeWidth, probeHeight);
    
    // Global location of the probe's (0,0) pixel, in the [0, 1] range
    const float2 probeOrigin = float2(globalPixel - localPixel) / float2(width, height);
    const float2 probeCenter = probeOrigin + float2(0.5f / (float)rootProbeCount, 0.5f / float(height / probeWidth));
    const uint2 globalProbeCoords = uint2(probeOrigin * rootProbeCount);
    const uint2 localProbeCoords = globalProbeCoords % uint2(probeWidth, probeHeight);
    const uint localProbeIndex = localProbeCoords.x + localProbeCoords.y * probeWidth;
    const uint2 probeCoordsOrigin = globalProbeCoords - localProbeCoords;
    const uint2 mappedProbeCoordsOrigin = uint2(probeCoordsOrigin.x, probeCoordsOrigin.y * (probeHeight / probeWidth) * 2);
    const uint2 fluence_neg_coords = mappedProbeCoordsOrigin + uint2(0, localProbeIndex);
    const uint2 fluence_pos_coords = fluence_neg_coords + uint2(1, 0);

    // outImage[pc.level][globalPixel] = float4(fluence_neg_coords, 0.0f, 1.0f);
    // return;

    // const float2 screenUV = (float2)localPixel / float2(uint2(width, height) / rootProbeCount);
    // outImage[pc.level][globalPixel] = float4(probeCenter, 0.0f, 1.0f);
    // return;

    // Calculate the angle between each ray direction in the probe
    const float deltaTheta = 2.0f / float(probeWidth * probeHeight);
    // Determine the direction of this ray
    const float theta = -1 + deltaTheta / 2.0f + (localPixel.x + localPixel.y * (float)probeWidth) * deltaTheta;
    const float2 rayDirection = float2(sinpi(theta), cospi(theta));

    float4 outColour = float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float interval = 1.0f / (pc.interval);

    const float2 pixelCenter = (float2)globalPixel + 0.5f;// + subpixelJitter;             // Jittered pixel position
    const float2 clipCoords = pixelCenter / float2(width, height) * 2.0 - 1.0;      // Convert to NDC [-1,1]
    const float4 viewCoords = mul(camera.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space

    // Shoot ray from probeOrigin in rayDirection
    RayDesc rayDesc;
    rayDesc.Origin =
        // Screenspace center
        float3(probeCenter, 0.0f)
        // Move in 5 units along the camera's forward vector
        //+ mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz
        // Start from the end interval of the previous cascade
        + float3(rayDirection, 0.0f) * interval * ((1 << pc.level) - 1)
        // Add world space camera transform
        + mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    // outImage[pc.level][globalPixel] = float4(rayDirection, 0.0f, 1.0f);
    // return;

    float4 rightVector = camera.invView[0];
    float4 upVector = camera.invView[1];

    // rayDesc.Direction = float3(rayDirection, 0.0f);
    // rayDesc.Direction = mul(camera.invView, float4(rayDirection, 0.0)).xyz;
    rayDesc.Direction = normalize((rayDirection.x * rightVector + rayDirection.y * upVector).xyz);
    rayDesc.TMin = 0.0;
    rayDesc.TMax = interval * (1 << pc.level);

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

    if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        const uint instanceID = rq.CommittedInstanceIndex();
        const uint primIndex = rq.CommittedPrimitiveIndex();

        const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CommittedTriangleBarycentrics());
        const float3 col = primAttribs[0];
        const float2 uv = primAttribs[1].xy;

        // const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

        // uint _width, _height, levelCount;
        // textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
        // const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);
        // outColour = float4(intersectionColor.rgb * col, intersectionColor.a);
        outColour = float4(col, 1.0f);
    }
    else {
        //outColour = float4(skyColour(rayDesc.Direction, pc.sunDirection, pc.sunIntensity), 1.0f);
    }
    // If highest cascade, just store value
    if (pc.level + 1 >= 3) {
        outImage[pc.level][globalPixel] = outColour;
    }
    // Merge F- F+
    else {
        float4 fluence_neg = outImage[pc.level + 1][fluence_neg_coords];
        float4 fluence_pos = outImage[pc.level + 1][fluence_pos_coords];
        outImage[pc.level][globalPixel] = ((fluence_neg + fluence_pos) / 2.0f + outColour) / 2.0f;
        // outImage[pc.level][globalPixel] = outColour;
    }

    // // Get side length of probe's kernel: width / 2^(M - (L + 1))
    // const uint probeWidth = (width / (1 << (pc.maxLevel - (pc.level + 1)))) / pc.c0ProbeCount[0];
    // // Transform the image pixel coordinates to the local pixel coordinates of the probe's kernel
    // const uint2 localPixel = globalPixel % probeWidth;
    
    // // const float2 screenUV = (float2)localPixel / (float)probeWidth;
    // // outImage[pc.level][globalPixel] = float4(screenUV, 0.0f, 1.0f);
    // // return;
    
    // // Global location of the probe's (0,0) pixel, in the [0, 1] range
    // const float2 probeOrigin = float2(globalPixel - localPixel) / (float)(width);
    
    // // Screen position of the probe
    // const float2 probeCenter = probeOrigin + (float(probeWidth) / 2.0f) / (float)(width);
    
    // // Calculate the angle between each ray direction in the probe
    // const float deltaTheta = 2.0f / float(probeWidth * probeWidth);
    // // Determine the direction of this ray
    // const float theta = -1 + deltaTheta / 2.0f + (localPixel.x + localPixel.y * (float)probeWidth) * deltaTheta;
    // const float2 rayDirection = float2(sinpi(theta), cospi(theta));
    
    // // outImage[pc.level][globalPixel] = float4(probeOrigin, 0.0f, 1.0f);
    // // return;
    
    // const float2 pixelCenter = (float2)globalPixel + 0.5f;// + subpixelJitter;             // Jittered pixel position
    // const float2 clipCoords = pixelCenter / float2(width, height) * 2.0 - 1.0;      // Convert to NDC [-1,1]
    // const float4 viewCoords = mul(camera.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space
    
    // // outImage[pc.level][globalPixel] = float4(localPixel, 0.0f, localPixel.x + localPixel.y * probeWidth);
    // // return;

    // float4 outColour = float4(0.0f, 0.0f, 0.0f, 1.0f);
    // const float interval = 1.0f / (pc.interval);

    // // Shoot ray from probeOrigin in rayDirection
    // RayDesc rayDesc;
    // rayDesc.Origin =
    //     // Screenspace center
    //     float3(probeCenter, 0.0f)
    //     // Move in 5 units along the camera's forward vector
    //     //+ mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz
    //     // Start from the end interval of the previous cascade
    //     + float3(rayDirection, 0.0f) * interval * ((1 << pc.level) - 1);
    //     // Add world space camera transform
    //     //+ mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    // // outImage[pc.level][globalPixel] = float4(rayDesc.Origin, 1.0f);
    // // return;

    // float4 rightVector = camera.invView[0];
    // float4 upVector = camera.invView[1];

    // rayDesc.Direction = float3(rayDirection, 0.0f);
    // // rayDesc.Direction = mul(camera.invView, float4(rayDirection, 0.0)).xyz;
    // // rayDesc.Direction = normalize((rayDirection.x * rightVector + rayDirection.y * upVector).xyz);
    // rayDesc.TMin = 0.0;
    // rayDesc.TMax = interval * (1 << pc.level);

    // RayQuery<RAY_FLAG_NONE> rq;
    // let rayFlags = RAY_FLAG_NONE;

    // rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);

    // while (rq.Proceed()) {
    //     const uint instanceID = rq.CandidateInstanceIndex();
    //     const uint primIndex = rq.CandidatePrimitiveIndex();

    //     const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
    //     const float2 uv = primAttribs[1].xy;

    //     const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

    //     uint _width, _height, levelCount;
    //     textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
    //     const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);

    //     if (intersectionColor.a >= 0.5)
    //         rq.CommitNonOpaqueTriangleHit();
    // }

    // if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    //     const uint instanceID = rq.CommittedInstanceIndex();
    //     const uint primIndex = rq.CommittedPrimitiveIndex();

    //     const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CommittedTriangleBarycentrics());
    //     const float3 col = primAttribs[0];
    //     const float2 uv = primAttribs[1].xy;

    //     // const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

    //     // uint _width, _height, levelCount;
    //     // textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
    //     // const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);
    //     // outColour = float4(intersectionColor.rgb * col, intersectionColor.a);
    //     outColour = float4(col, 1.0f);
    // }
    // else {
    //     //outColour = float4(skyColour(rayDesc.Direction, pc.sunDirection, pc.sunIntensity), 1.0f);
    // }
    // // If highest cascade, just store value
    // if (pc.level + 1 >= pc.maxLevel) {
    //     outImage[pc.level][globalPixel] = outColour;
    // }
    // // Merge F- F+
    // else {
    //     float4 fluence_neg = outImage[pc.level + 1][globalPixel];
    //     // Get fluence_pos local pixel as 1D array
    //     uint fluence_pos_1D = ((globalPixel.x % probeWidth) + (globalPixel.y % probeWidth) * probeWidth + 1) % (probeWidth * probeWidth);
    //     // Convert fluence coords to global coords
    //     uint2 fluence_pos_pixel = (globalPixel - localPixel) + uint2(fluence_pos_1D % probeWidth, fluence_pos_1D / probeWidth);
    //     float4 fluence_pos = outImage[pc.level + 1][fluence_pos_pixel];
    //     // fluence_neg = outColour;
    //     // fluence_pos = fluence_neg;
    //     outImage[pc.level][globalPixel] = float4(((fluence_neg + fluence_pos) / 2.0f + outColour) / 2.0f);
    // }
}