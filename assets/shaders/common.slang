module common;

// Mirrored struct of Vertex in Vertex.hpp
public struct VSInput {
    public float3 inPosition;
    public float2 inTexCoord;
    public float3 inColor;
    public float3 inNormal;
    public uint cubeID;
};

// Internal struct for passing attributes to fragment shader
public struct VSOutput {
    public float4 pos : SV_Position;
    public float2 fragTexCoord;
    public float3 fragColor;
    public float3 fragNormal;
    public float3 worldPos;
    public uint cubeID;
};

// 3D Transformation Matrices + Inversions
public struct CameraTransforms {
    public float4x4 model;
    public float4x4 view;
    public float4x4 invView;
    public float4x4 proj;
    public float4x4 invProj;
};
[[vk::binding(0, 0)]] public ConstantBuffer<CameraTransforms> camera;

// 3D Transformation Matrices + Inversions
public struct CubeTransforms {
    public float4x4 model;
    public float4x4 view;
    public float4x4 invView;
    public float4x4 proj;
    public float4x4 invProj;
};
[[vk::binding(1, 0)]] public ConstantBuffer<CameraTransforms> cube;

// the scene's tlas
[[vk::binding(2, 0)]] public RaytracingAccelerationStructure accelerationStructure;

// the indices of the scene
[[vk::binding(3, 0)]] public StructuredBuffer<uint> indexBuffer;

// the vertex colours of the scene
[[vk::binding(4, 0)]] public StructuredBuffer<float3> colourBuffer;

// the uvs of each vertex in the scene
[[vk::binding(5, 0)]] public StructuredBuffer<float2> uvBuffer;

// the normals of each vertex in the scene
[[vk::binding(6, 0)]] public StructuredBuffer<float4> nrmBuffer;

// Each submesh's unique attributes
public struct InstanceLUT {
    public uint materialID; // submesh's material (corresponds to texture array index)
    public uint indexBufferOffset; // lookup indices > indexBufferOffset for other attributes (uvs, normals)
};
[[vk::binding(7, 0)]] public StructuredBuffer<InstanceLUT> instanceLUTBuffer;

// Sampler of the written-to image
[[vk::binding(8, 0)]] public SamplerState sampler;

// Write access to StorageImages
[vk_image_format("rgba16f")]
[[vk::binding(9, 0)]] public RWTexture2D<float4> outImage[];

// Sample access to StorageImages
[vk_image_format("rgba16f")]
[[vk::binding(10, 0)]] public Texture2D<float4> renderTextures[];

// Sampler for texture array
[[vk::binding(0, 1)]] public SamplerState textureSampler;

// the array of the scene's textures (texture atlas)
[[vk::binding(1, 1)]] public Texture2D<float4> textures[];

// Get the attributes at a point on a triangle primitive's surface, interpolating its vertices' attributes
public float3x3 getPrimAttribs(uint instanceID, uint primIndex, float2 barycentrics) {
    // The starting index of this primitive within an instance
    uint indexOffset = instanceLUTBuffer[NonUniformResourceIndex(instanceID)].indexBufferOffset;

    // primIndex * 3 because triangle list with 3 vertices each
    uint i0 = indexBuffer[indexOffset + (primIndex * 3 + 0)];
    uint i1 = indexBuffer[indexOffset + (primIndex * 3 + 1)];
    uint i2 = indexBuffer[indexOffset + (primIndex * 3 + 2)];

    float3 col0 = colourBuffer[i0].xyz;
    float3 col1 = colourBuffer[i1].xyz;
    float3 col2 = colourBuffer[i2].xyz;

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
    return float3x3(
        w0 * col0 + w1 * col1 + w2 * col2,
        w0 * uv0 + w1 * uv1 + w2 * uv2, 
        w0 * nrm0 + w1 * nrm1 + w2 * nrm2
    );
}

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
// Each use of the function will produce different result through stepping the seed each call
public float rand(inout uint seed) {
    // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
    // Step seed
    seed = seed * 747796405u + 1;
    // Generate pseudorandom uint from seed
    uint word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    word = (word >> 22) ^ word;
    return float(word) / 4294967295.0f;
}

// Approximate scattering by randomly choosing a direction to bias a bounce towards (vector in sphere)
public float3 randomDirection(uint rngState) {
    float theta = rand(rngState);           // Random in [0, 2pi] (random direction on XY plane)
    float u = 2.0 * rand(rngState) - 1.0;   // Random in [-1, 1] (random direction -Z, Z)
    float r = sqrt(1.0 - u * u);            // Get the radius of the sphere
    return float3(r * cospi(theta), r * sinpi(theta), u); // Vector from origin to surface of sphere
}

// Mimic horizon by interpolating a sky colour if the ray is pointing up, ground if down
public float3 skyColour(float3 bounceDir, float3 sunDir, float sunIntensity)
{
    const float3 topColour = float3(0.25f, 0.5f, 1.0f);
    const float3 halfColour = float3(0.4f, 0.5, 0.6f);
    const float3 bottomColour = float3(0.03f);
    // if pointing up
    if (bounceDir.y > 0.0f) {
        const float3 skyColour = lerp(halfColour, topColour, bounceDir.y);

        const float t = smoothstep(0.99f, 0.996f, abs(dot(bounceDir, sunDir)));
        const float3 sunBias = lerp(skyColour, float3(1.0f, 0.9f, 0.9f) * sunIntensity, t);
        return sunBias;
    }
    // pointing down
    else {
        const float3 groundInfluence = lerp(halfColour, bottomColour, bounceDir.y);
        return groundInfluence;
    }
}

// Get point light illumination at a point
public float3 directIllumination(float3 worldHitPos)
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