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

// the scene's tlas
[[vk::binding(1, 0)]] RaytracingAccelerationStructure accelerationStructure;

// the indices of the scene
[[vk::binding(2, 0)]] StructuredBuffer<uint> indexBuffer;

// the uvs of each vertex in the scene
[[vk::binding(3, 0)]] StructuredBuffer<float2> uvBuffer;

// the normals of each vertex in the scene
[[vk::binding(4, 0)]] StructuredBuffer<float4> nrmBuffer;

// Each submesh's unique attributes
struct InstanceLUT {
    uint materialID; // submesh's material (corresponds to texture array index)
    uint indexBufferOffset; // lookup indices > indexBufferOffset for other attributes (uvs, normals)
};
[[vk::binding(5, 0)]] StructuredBuffer<InstanceLUT> instanceLUTBuffer;

// Sampler of the written-to image
[vk::binding(6, 0)] Sampler2D sampler;

// Write access to StorageImage
[vk_image_format("rgba32f")]
[[vk::binding(7, 0)]] RWTexture2D<float4> outImage;

// Sampler for texture array
[[vk::binding(0,1)]] SamplerState textureSampler;

// the array of the scene's textures (texture atlas)
[[vk::binding(1,1)]] Texture2D<float4> textures[];

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

// Get the attributes at a point on a triangle primitive's surface, interpolating its vertices' attributes
float2x3 getPrimAttribs(uint instanceID, uint primIndex, float2 barycentrics) {
    // The starting index of this primitive within an instance
    uint indexOffset = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].indexBufferOffset;

    // primIndex * 3 because triangle list with 3 vertices each
    uint i0 = indexBuffer[indexOffset + (primIndex * 3 + 0)];
    uint i1 = indexBuffer[indexOffset + (primIndex * 3 + 1)];
    uint i2 = indexBuffer[indexOffset + (primIndex * 3 + 2)];

    // Pad the UVs with second y to make correct row length for float2x3 
    float3 uv0 = uvBuffer[i0].xyy;
    float3 uv1 = uvBuffer[i1].xyy;
    float3 uv2 = uvBuffer[i2].xyy;

    // Grab corresponding normals
    float3 nrm0 = nrmBuffer[i0].xyz;
    float3 nrm1 = nrmBuffer[i1].xyz;
    float3 nrm2 = nrmBuffer[i2].xyz;

    // interpolation weights
    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    float w1 = barycentrics.x;
    float w2 = barycentrics.y;

    // the uvs and normals at hit location on triangle
    return float2x3(w0 * uv0 + w1 * uv1 + w2 * uv2, w0 * nrm0 + w1 * nrm1 + w2 * nrm2);
}

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
// Each use of the function will produce different result through stepping the seed each call
float rand(inout uint seed) {
    // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
    // Step seed
    seed = seed * 747796405 + 1;
    // Generate pseudorandom uint from seed
    uint word = ((seed >> ((seed >> 28) + 4)) ^ seed) * 277803737;
    word = (word >> 22) ^ word;
    return float(word) / 4294967295.0f;
}

// Approximate scattering by randomly choosing a direction to bias a bounce towards (vector in sphere)
float3 randomDirection(uint rngState) {
    float theta = rand(rngState);           // Random in [0, 2pi] (random direction on XY plane)
    float u = 2.0 * rand(rngState) - 1.0;   // Random in [-1, 1] (random direction -Z, Z)
    float r = sqrt(1.0 - u * u);            // Get the radius of the sphere
    return float3(r * cospi(theta), r * sinpi(theta), u); // Vector from origin to surface of sphere
}

// Mimic horizon by interpolating a sky colour if the ray is pointing up, ground if down
float3 skyColour(float3 bounceDir, float3 sunDir)
{
    const float3 topColour = float3(0.25f, 0.5f, 1.0f);
    const float3 halfColour = float3(0.4f, 0.5, 0.6f);
    const float3 bottomColour = float3(0.03f);
    // if pointing up
    if (bounceDir.y > 0.0f) {
        const float3 skyColour = lerp(halfColour, topColour, bounceDir.y);

        const float t = smoothstep(0.99f, 0.996f, abs(dot(bounceDir, sunDir)));
        const float3 sunBias = lerp(skyColour, float3(1.0f, 0.9f, 0.9f) * pc.intensity, t);
        return sunBias;
    }
    // pointing down
    else {
        return lerp(halfColour, bottomColour, abs(bounceDir.y));
    }
}

// Get point light illumination at a point
float3 directIllumination(float3 worldHitPos)
{
    float3 lightPositions[] = {
        float3(0.0, 1.7, 0.0)//, float3(1.0, 1.7, 0.0), float3(-1.0, 1.7, 0.0)
    };
    float lightIntensities[] = {5.0};
    float3 lightColours[] = { float3(1.0) };
    float3 output = 0.;
    const uint lightCount = lightPositions.getCount();
    // Point light: Calculate direction from light to surface and apply distance attenuation
    for (uint i = 0; i < lightCount; i++)
    {
        // Direction from surface point to light position
        float3 direction = lightPositions[i] - worldHitPos;
        float d = length(direction);
        // Apply inverse square law: intensity decreases with distance squared
        float intensity = lightIntensities[0] / (d * d);
        // Accumulate lighting intensity and colour at point
        output += lightColours[0] * intensity;
    }

    return output;
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
    const float4 viewCoords = mul(ubo.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space
    
    // More samples == faster convergence, slower execution (but minus new frame overhead)
    const uint MAX_SAMPLES = 2;
    const uint MAX_DEPTH = 6;
    const float INITIAL_MISS_DISTANCE = 1000.0f;

    float3 summedPixelCol = float3(0.0);
    for (uint sampleID = 0; sampleID < MAX_SAMPLES; sampleID++) {
        float3 rayOrigin = mul(ubo.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz; // Camera position in world space

        float3 rayDirection = mul(ubo.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz; // Ray direction in world space

        float3 accumCol = float3(1.0);

        float missDistance = INITIAL_MISS_DISTANCE;

        for (uint depth = 0; depth < MAX_DEPTH; depth++) {
            RayDesc rayDesc;
            rayDesc.Origin = rayOrigin;
            rayDesc.Direction = rayDirection;
            rayDesc.TMin = 0.0;
            rayDesc.TMax = missDistance;
            
            // attenuate missDistance
            //missDistance /= (depth + 1.0);

            RayQuery<RAY_FLAG_NONE> rq;
            let rayFlags = RAY_FLAG_NONE;

            rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);

            while (rq.Proceed()) {
                uint instanceID = rq.CandidateInstanceIndex();
                uint primIndex = rq.CandidatePrimitiveIndex();

                float2x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
                float2 uv = primAttribs[0].xy;
                float3 norm = primAttribs[1].xyz;

                uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

                uint width, height, levelCount;
                textures[NonUniformResourceIndex(materialID)].GetDimensions(1, width, height, levelCount);
                float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float) levelCount);

                if (intersectionColor.a >= 0.5)
                    rq.CommitNonOpaqueTriangleHit();
            }

            if (rq.CommittedStatus() == COMMITTED_NOTHING) {
                accumCol *= skyColour(rayDirection, pc.lightDir);
                summedPixelCol += accumCol;
                
                break;
            }

            uint instanceID = rq.CommittedInstanceIndex();
            uint primIndex = rq.CommittedPrimitiveIndex();

            float2x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CommittedTriangleBarycentrics());
            float2 uv = primAttribs[0].xy;
            float3 norm = primAttribs[1].xyz;

            float4x3 worldToObject = rq.CommittedWorldToObject4x3();

            uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;
            float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CommittedRayT() / 10.0);

            const float3 worldHitPos = rayOrigin + rayDirection * rq.CommittedRayT();
            const float3 L = 1.0;//directIllumination(worldHitPos);

            accumCol *= L * intersectionColor.rgb;

            // Go to the world position of hit
            rayOrigin += rayDirection * rq.CommittedRayT();
            
            const float3 bounceRandom = randomDirection(seed);
            
            rayDirection = normalize(mul(worldToObject, norm).xyz + bounceRandom);
            
            // Avoid self-intersections by starting slightly above the surface
            rayOrigin += rayDirection * 0.001;
            
            // Check direct illumination at point (still random, biased towards light)
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
                    accumCol *= skyColour(rayDirection, pc.lightDir);
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
        outImage[pixelCoords] = float4(summedPixelCol, 1.0);
    }
    else {
        float a = max(1.0 / (float) (pc.frame + 1), 0.001);
        float3 old_colour = outImage[pixelCoords].rgb;
        outImage[pixelCoords] = float4(lerp(old_colour, summedPixelCol, a), 1.0);
    }
}