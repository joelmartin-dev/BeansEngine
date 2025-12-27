import common;

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
    float3 lightDir;
    float intensity;
    float time;
    uint frame;
};
[push_constant] PushConstant pc;

// Sample the StorageImage using fullscreen triangle
[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    // return float4(vertIn.fragTexCoord, 0.0, 1.0);
    return renderTextures[0].Sample(textureSampler, vertIn.fragTexCoord);
}

static const float2 BlueNoiseInDisk[64] = {
    float2(0.478712,0.875764),
    float2(-0.337956,-0.793959),
    float2(-0.955259,-0.028164),
    float2(0.864527,0.325689),
    float2(0.209342,-0.395657),
    float2(-0.106779,0.672585),
    float2(0.156213,0.235113),
    float2(-0.413644,-0.082856),
    float2(-0.415667,0.323909),
    float2(0.141896,-0.939980),
    float2(0.954932,-0.182516),
    float2(-0.766184,0.410799),
    float2(-0.434912,-0.458845),
    float2(0.415242,-0.078724),
    float2(0.728335,-0.491777),
    float2(-0.058086,-0.066401),
    float2(0.202990,0.686837),
    float2(-0.808362,-0.556402),
    float2(0.507386,-0.640839),
    float2(-0.723494,-0.229240),
    float2(0.489740,0.317826),
    float2(-0.622663,0.765301),
    float2(-0.010640,0.929347),
    float2(0.663146,0.647618),
    float2(-0.096674,-0.413835),
    float2(0.525945,-0.321063),
    float2(-0.122533,0.366019),
    float2(0.195235,-0.687983),
    float2(-0.563203,0.098748),
    float2(0.418563,0.561335),
    float2(-0.378595,0.800367),
    float2(0.826922,0.001024),
    float2(-0.085372,-0.766651),
    float2(-0.921920,0.183673),
    float2(-0.590008,-0.721799),
    float2(0.167751,-0.164393),
    float2(0.032961,-0.562530),
    float2(0.632900,-0.107059),
    float2(-0.464080,0.569669),
    float2(-0.173676,-0.958758),
    float2(-0.242648,-0.234303),
    float2(-0.275362,0.157163),
    float2(0.382295,-0.795131),
    float2(0.562955,0.115562),
    float2(0.190586,0.470121),
    float2(0.770764,-0.297576),
    float2(0.237281,0.931050),
    float2(-0.666642,-0.455871),
    float2(-0.905649,-0.298379),
    float2(0.339520,0.157829),
    float2(0.701438,-0.704100),
    float2(-0.062758,0.160346),
    float2(-0.220674,0.957141),
    float2(0.642692,0.432706),
    float2(-0.773390,-0.015272),
    float2(-0.671467,0.246880),
    float2(0.158051,0.062859),
    float2(0.806009,0.527232),
    float2(-0.057620,-0.247071),
    float2(0.333436,-0.516710),
    float2(-0.550658,-0.315773),
    float2(-0.652078,0.589846),
    float2(0.008818,0.530556),
    float2(-0.210004,0.519896) 
};

[shader("compute")]
[numthreads(8, 8, 1)]
void compMain(uint3 threadIdx: SV_DispatchThreadID)
{
    const uint2 pixelCoords = threadIdx.xy;
    // if (all(pixelCoords == uint2(0, 0))) printf("%f\n", pc.time);
    
    // Get StorageImage's resolution
    uint2 imgSize;
    outImage[0].GetDimensions(imgSize.x, imgSize.y);
    
    // if this pixel is not in the image, don't do the work
    if (pixelCoords.x >= imgSize.x || pixelCoords.y >= imgSize.y) return;
    
    // A different initial seed each frame for each pixel
    uint seed = (uint) pc.time * (imgSize.x * pixelCoords.y + pixelCoords.x);
    
    // Randomise the subpixel location for initial rayOrigin so consecutive frames have different outputs (enables
    // temporal accumulation)
    // const uint jitterIndex = floor(rand(seed) * 64);
    const float2 subpixelJitter = BlueNoiseInDisk[pc.frame % 64] / 2.0;// + float2(rand(seed), rand(seed)) / 10.0;
    const float2 pixelCenter = (float2)pixelCoords + subpixelJitter;          // Jittered pixel position
    const float2 clipCoords = pixelCenter / (float2)imgSize.xy * 2.0 - 1.0;   // Convert to NDC [-1,1]
    const float4 viewCoords = mul(camera.invProj, float4(clipCoords, 1.0, 1.0)); // Transform to view space
    
    // More samples == faster convergence, slower execution (but minus new frame overhead)
    const uint MAX_SAMPLES = 1;
    const uint MAX_DEPTH = 1;
    const float INITIAL_MISS_DISTANCE = 1000.0f;

    float3 summedPixelCol = float3(0.0);
    for (uint sampleID = 0; sampleID < MAX_SAMPLES; sampleID++) {
        float3 rayOrigin = mul(camera.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz; // Camera position in world space

        float3 rayDirection = mul(camera.invView, float4(normalize(viewCoords.xyz), 0.0)).xyz; // Ray direction in world space

        float3 accumCol = float3(1.0);

        float missDistance = INITIAL_MISS_DISTANCE;
        RayDesc rayDesc;
        rayDesc.TMin = 0.0;
        rayDesc.TMax = missDistance;

        for (uint depth = 0; depth <= MAX_DEPTH; depth++) {
            rayDesc.Origin = rayOrigin;
            rayDesc.Direction = rayDirection;

            RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> rq;
            let rayFlags = RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

            rq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, rayDesc);

            while (rq.Proceed()) {
                const uint instanceID = rq.CandidateInstanceIndex();
                const uint primIndex = rq.CandidatePrimitiveIndex();

                const float3x3 primAttribs = getPrimAttribs(instanceID, primIndex, rq.CandidateTriangleBarycentrics());
                const float2 uv = primAttribs[1].xy;

                const uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;

                uint _width, _height, levelCount;
                textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
                const float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CandidateTriangleRayT() / (float) levelCount);

                if (intersectionColor.a >= 0.5)
                    rq.CommitNonOpaqueTriangleHit();

            }

            if (rq.CommittedStatus() == COMMITTED_NOTHING) {
                // if (dot(rayDirection, pc.lightDir) > 0.9) {
                //     accumCol *= float3(pc.intensity);
                // } else {
                //     accumCol *= float3(0.0);
                // }
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

            float3 reflected = mul(worldToObject, norm + bounceRandom).xyz;
            // if (depth + 1 >= MAX_DEPTH) { reflected = normalize(pc.lightDir + bounceRandom); }


            if (length(reflected) > 1e-1) {
                rayDirection = normalize(reflected);
            } else {
                // Guaranteed non-nan direction
                rayDirection = pc.lightDir;
            }
            
            // Avoid self-intersections by starting slightly above the surface
            rayOrigin += rayDirection * 0.001;

            uint materialID = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].materialID;
            uint _width, _height, levelCount;
            textures[NonUniformResourceIndex(materialID)].GetDimensions(1, _width, _height, levelCount);
            float4 intersectionColor = textures[NonUniformResourceIndex(materialID)].SampleLevel(textureSampler, uv, rq.CommittedRayT() / (float) levelCount);

            accumCol *= intersectionColor.rgb / sqrt(rq.CommittedRayT());

            float lum = dot(accumCol, float3(0.212671f, 0.715160f, 0.072169f));
            const float fireflyClampThreshold = 10.0f;
            if (lum > fireflyClampThreshold) accumCol *= fireflyClampThreshold / lum;
            accumCol = 1.0 - exp(-1.0 * accumCol);
        }
    }

    summedPixelCol /= (float)MAX_SAMPLES;

    if (pc.frame == 0) {
        outImage[0][pixelCoords] = float4(summedPixelCol, 1.0);
    }
    else {
        float a = max(1.0 / (float) (pc.frame + 1), 0.001);
        float3 old_colour = outImage[0][pixelCoords].rgb;
        outImage[0][pixelCoords] = float4(lerp(old_colour, summedPixelCol, a), 1.0);
    }
}