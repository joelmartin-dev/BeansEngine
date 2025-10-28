import common;

// Apply Transformations
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
    uint frame;
    float time;
    float intensity;
    float3 lightDir;
};
[push_constant] PushConstant pc;

// Sample the StorageImage using fullscreen triangle
[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    return sampler.Sample(vertIn.fragTexCoord);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{
    const uint2 pixelCoords = threadIdx.xy;
    
    // Get StorageImage's resolution
    uint2 imgSize;
    outImage.GetDimensions(imgSize.x, imgSize.y);
    
    // if this pixel is not in the image, don't do the work
    if (pixelCoords.x >= imgSize.x || pixelCoords.y >= imgSize.y) return;
    
    // A different initial seed each frame for each pixel
    uint seed = (uint) pc.time * (imgSize.x * pixelCoords.y + pixelCoords.x);
    
    // Randomise the subpixel location for initial rayOrigin so consecutive frames have different outputs (enables
    // temporal accumulation)
    const float2 subpixelJitter = float2(rand(seed), rand(seed));
    const float2 pixelCenter = (float2)pixelCoords + subpixelJitter;          // Jittered pixel position
    const float2 clipCoords = pixelCenter / (float2)imgSize.xy * 2.0 - 1.0;   // Convert to NDC [-1,1]
    const float4 viewCoords = mul(camera.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space
    
    // More samples == faster convergence, slower execution (but minus new frame overhead)
    const uint MAX_SAMPLES = 256;
    const uint MAX_DEPTH = 4;
    const float INITIAL_MISS_DISTANCE = 1000.0f;

    float3 summedPixelCol = float3(0.0);

    for (uint sampleID = 0; sampleID < MAX_SAMPLES; sampleID++) {
        float3 rayOrigin = mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz; // Camera position in world space

        float3 rayDirection = mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz; // Ray direction in world space

        float3 accumCol = float3(1.0);

        float missDistance = INITIAL_MISS_DISTANCE;

        for (uint depth = 0; depth < MAX_DEPTH; depth++) {
            RayDesc rayDesc;
            rayDesc.Origin = rayOrigin;
            rayDesc.Direction = rayDirection;
            rayDesc.TMin = 0.0;
            rayDesc.TMax = missDistance;

            RayQuery<RAY_FLAG_NONE> rq;
            let rayFlags = RAY_FLAG_NONE;

            rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);

            while (rq.Proceed()) {
                uint instanceID = rq.CandidateInstanceIndex();
                uint primIndex = rq.CandidatePrimitiveIndex();

                float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
                float2 uv = primAttribs[1].xy;

                uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

                uint width, height, levelCount;
                textures[NonUniformResourceIndex(materialID)].GetDimensions(0, width, height, levelCount);
                float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float) levelCount);

                if (intersectionColor.a >= 0.5) rq.CommitNonOpaqueTriangleHit();
            }

            if (rq.CommittedStatus() == COMMITTED_NOTHING) {
                accumCol *= skyColour(rayDirection, pc.lightDir, pc.intensity);
                summedPixelCol += accumCol;
                
                break;
            }
            
            // Get next direction
            const float3 worldHitPos = rayOrigin + rayDirection * rq.CommittedRayT();
            
            // Go to the world position of hit
            rayOrigin += rayDirection * rq.CommittedRayT();

            const float3 bounceRandom = randomDirection(seed);
            
            uint instanceID = rq.CommittedInstanceIndex();
            uint primIndex = rq.CommittedPrimitiveIndex();

            const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CommittedTriangleBarycentrics());
            const float3 col = primAttribs[0].xyz;
            const float2 uv = primAttribs[1].xy;
            const float3 norm = primAttribs[2].xyz;
            const float4x3 worldToObject = rq.CommittedWorldToObject4x3();
            
            rayDirection = normalize(mul(worldToObject, norm).xyz + bounceRandom);
            
            // Avoid self-intersections by starting slightly above the surface
            rayOrigin += rayDirection * 0.001;
            
            uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;
            uint _width, _height, levelCount;
            textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
            float4 intersectionColor = 
            textures[NonUniformResourceIndex(materialID)].SampleLevel(
                textureSampler, uv, rq.CommittedRayT() / (float) levelCount);
                
            accumCol *= intersectionColor.rgb * col;

            // Check the direct illumination at point (still random, biased towards light)
            if (depth + 1 >= MAX_DEPTH) {
                const float3 bounceRandom = randomDirection(seed);
                rayDirection = normalize(pc.lightDir + bounceRandom);
        
                rayDesc.Direction = rayDirection;
                rayDesc.Origin = rayOrigin;
                rayDesc.TMin = 0.0;
                rayDesc.TMax = INITIAL_MISS_DISTANCE;
        
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
                let rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
        
                sq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);
                while (sq.Proceed()) sq.CommitNonOpaqueTriangleHit();
        
                if (sq.CommittedStatus() == COMMITTED_NOTHING) {
                    accumCol *= skyColour(rayDirection, pc.lightDir, pc.intensity);
                    summedPixelCol += accumCol;
                }
            }
            
            float lum = dot(accumCol, float3(0.212671f, 0.715160f, 0.072169f));
            const float fireflyClampThreshold = 10.0f;
            if (lum > fireflyClampThreshold) accumCol *= fireflyClampThreshold / lum;
            accumCol = 1.0 - exp(-1.0 * accumCol);
        }
    }

    summedPixelCol /= (float)MAX_SAMPLES;

    if (pc.frame == 0) {
        outImage[pixelCoords] = float4(abs(summedPixelCol), 1.0);
    }
    else {
        float a = max(1.0 / ((float) (pc.frame + 1) / 2.0f), 0.000);
        float3 old_colour = outImage[pixelCoords].rgb;
        outImage[pixelCoords] = float4(lerp(old_colour, abs(summedPixelCol), a), 1.0);
    }
}