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
    return outColour / float4(pc.maxLevel);
}

[shader("compute")]
[numthreads(4, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{
    uint width, height;
    outImage[pc.level].GetDimensions(width, height);
    const uint2 globalPixel = threadIdx.xy;
    if (globalPixel.x >= width || globalPixel.y >= height) return;
    
    // const uint rootProbeCount = pc.c0ProbeCount[0] >> pc.level;
    const uint probeWidth = width;
    const uint probeHeight = height;

    // Transform the image pixel coordinates to the local pixel coordinates of the probe's kernel
    const uint2 localPixel = globalPixel % uint2(probeWidth, probeHeight);
    
    // Global location of the probe's (0,0) pixel, in the [0, 1] range
    // const float2 probeOrigin = float2(globalPixel - localPixel) / float2(width, height);
    // const float2 probeCenter = probeOrigin + float2(0.5f / (float)rootProbeCount, 0.5f / float(height / probeWidth));

    // const uint localIndex = localPixel.x + localPixel.y * probeWidth;
    // const uint maxLocalIndex = probeWidth * probeHeight;
    
    // const uint2 globalProbeCoords = uint2(probeOrigin * rootProbeCount);
    // const uint2 localProbeCoords = globalProbeCoords % uint2(probeWidth, probeHeight);
    // const uint localProbeIndex = localProbeCoords.x + localProbeCoords.y * probeWidth;
    // const uint2 probeCoordsOrigin = globalProbeCoords - localProbeCoords;
    // const uint2 mappedProbeCoordsOrigin = uint2(probeCoordsOrigin.x * (probeWidth >> 1), probeCoordsOrigin.y * probeHeight);
    // const uint2 fluence_neg_coords = mappedProbeCoordsOrigin + uint2(0, localProbeIndex);
    // const uint2 fluence_pos_coords = fluence_neg_coords + uint2(1, 0);

    const uint rayCount = pc.baseRayCount << pc.level;
    const float2 probeCenter = float2(globalPixel + 0.5) / float2(width, height);
    const float2 clipCoords = probeCenter * 2.0 - 1.0;   // Convert to NDC [-1,1]
    const float4 viewCoords = mul(camera.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space

    // // Calculate the angle between each ray direction in the probe
    // const float deltaTheta = 1.0f / (pc.baseRayCount << pc.level);

    const float GOLDEN_RATIO = (1.0 + pow(5.0, 0.5)) / 2.0;
    const float deltaTheta = 2.0 * float.getPi() / GOLDEN_RATIO;
    const float deltaPhi = 2.0 / float(rayCount);
    
    const float intervalSize = 1.0f / pc.interval;
    const float intervalStart = intervalSize * pc.level * pc.level;
    const float intervalEnd = intervalSize * (pc.level + 1) * (pc.level + 1);

    float3 probeWorldPos = float3(probeCenter, 0.0);
    float4 radiance = float4(0.0f);
    {
        RayDesc ray;
        ray.TMin = 0.0;
        ray.TMax = 1000.0;
        ray.Origin = mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz; // Camera position in world space
        ray.Direction = mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz;         // Ray direction in world space

        RayQuery<RAY_FLAG_NONE> rq;
        let rayFlags = RAY_FLAG_NONE;

        rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, ray);

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
            outImage[pc.level][globalPixel] = float4((renderTextures[pc.level + 1].Sample(sampler, probeCenter).rgb
                + skyColour(ray.Direction, pc.sunDirection, pc.sunIntensity))
                 / 2.0, 1.0);
            return;
        }

        // World position slightly back from hit
        probeWorldPos = ray.Origin + (rq.CommittedRayT() - 0.001) * 1.0 / float(pc.level + 1) * ray.Direction;
    }

    for (uint i = 0; i < rayCount; i++) {
        // const float theta = ((float(i) + 0.5) * 2.0) * deltaTheta;
        const float theta = deltaTheta * float(i);
        const float phi = acos(1.0 - deltaPhi * float(i));
        const float3 rayDirection = float3(
            cos(theta) * sin(phi), 
            sin(theta) * sin(phi), 
            cos(phi)
        );
        RayDesc ray;
        ray.TMin = 0.0;
        ray.TMax = intervalEnd - intervalStart;
        ray.Direction = mul(camera.invView, float4(rayDirection, 0.0)).xyz; // Ray direction in world space
        // ray.Direction = rayDirection;
        ray.Origin = probeWorldPos; // Ray origin in world space
        // if (i == 10) {
        //     outImage[pc.level][globalPixel] = float4(ray.Direction, 1.0f);
        //     return;
        // }
        RayQuery<RAY_FLAG_NONE> rq;
        let rayFlags = RAY_FLAG_NONE;

        rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, ray);

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
        float4 radDelta = float4(0.0f);
        if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const uint instanceID = rq.CommittedInstanceIndex();
            const uint primIndex = rq.CommittedPrimitiveIndex();

            const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CommittedTriangleBarycentrics());
            const float3 col = primAttribs[0];
            const float2 uv = primAttribs[1].xy;

            const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

            uint _width, _height, levelCount;
            textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
            float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CommittedRayT() / (float)levelCount);

            radDelta += float4(intersectionColor.rgb * col, intersectionColor.a);
        }
        if (rq.CommittedStatus() == COMMITTED_NOTHING && (pc.level + 1) < pc.maxLevel) {
            radDelta += float4(renderTextures[pc.level + 1].Sample(sampler, probeCenter)) / 2.0;
        }
        radiance += float4(radDelta.rgb, 1.0);
    }
    // {
    //     RayDesc ray;
    //     ray.TMin = 0.0;
    //     ray.TMax = 1000.0;
    //     ray.Origin = probeWorldPos;                // Camera position in world space
    //     ray.Direction = pc.sunDirection; // Ray direction in world space

    //     RayQuery<RAY_FLAG_NONE> rq;
    //     let rayFlags = RAY_FLAG_NONE;

    //     rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, ray);

    //     while (rq.Proceed()) {
    //         const uint instanceID = rq.CandidateInstanceIndex();
    //         const uint primIndex = rq.CandidatePrimitiveIndex();

    //         const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
    //         const float2 uv = primAttribs[1].xy;

    //         const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

    //         uint _width, _height, levelCount;
    //         textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
    //         const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);

    //         if (intersectionColor.a >= 0.5)
    //             rq.CommitNonOpaqueTriangleHit();
    //     }
    //     if (rq.CommittedStatus() == COMMITTED_NOTHING) {
    //         radiance += float4(skyColour(ray.Direction, pc.sunDirection, pc.sunIntensity), 1.0);
    //     }
    // }
    radiance /= float(rayCount + 1);

    outImage[pc.level][globalPixel] = radiance;
    

    // // Shoot ray from probeOrigin in rayDirection
    // RayDesc rayDesc;
    // rayDesc.Origin =
    //     // Screenspace center
    //     float3(probeCenter, 0.0f)
    //     // Move in 5 units along the camera's forward vector
    //     //+ mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz
    //     // Start from the end interval of the previous cascade
    //     + float3(rayDirection, 0.0f) * intervalStart;
    //     // Add world space camera transform
    //     //+ mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    // // float4 rightVector = camera.invView[0];
    // // float4 upVector = camera.invView[1];

    // rayDesc.Direction = float3(rayDirection, 0.0f);
    // // rayDesc.Direction = mul(camera.invView, float4(rayDirection, 0.0)).xyz;
    // // rayDesc.Direction = normalize((rayDirection.x * rightVector + rayDirection.y * upVector).xyz);
    // rayDesc.TMin = 0.0;
    // rayDesc.TMax = intervalEnd - intervalStart;

    // RayQuery<RAY_FLAG_NONE> rq;
    // let rayFlags = RAY_FLAG_NONE;

    // rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);

    // while (rq.Proceed()) {
    //     // const uint instanceID = rq.CandidateInstanceIndex();
    //     // const uint primIndex = rq.CandidatePrimitiveIndex();

    //     // const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
    //     // const float2 uv = primAttribs[1].xy;

    //     // const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

    //     // uint _width, _height, levelCount;
    //     // textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
    //     // const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float)levelCount);

    //     // if (intersectionColor.a >= 0.5)
    //         rq.CommitNonOpaqueTriangleHit();
    // }
    // float4 radDelta = float4(0.0f);
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
    //     radDelta = float4(col, 1.0f);
    // }
    // if (radDelta.a == 0.0f && pc.level + 1 < pc.maxLevel) {
    //     float4 fluence_neg = outImage[pc.level + 1][fluence_neg_coords];
    //     float4 fluence_pos = outImage[pc.level + 1][fluence_pos_coords];
    //     radDelta = (fluence_neg + fluence_pos) / 2.0f;
    // }

    // outImage[pc.level][globalPixel] = radDelta;
}