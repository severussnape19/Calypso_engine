struct VSInput {
    [[vk::location(0)]] float2 inPosition;
    [[vk::location(1)]] float3 inColor;
    [[vk::location(2)]] float2 inUV;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput {
    float4 pos: SV_Position;
    float3 frag_color;
    float2 frag_uv;
};

// This function is invoked for every vertex
// SV_VertexID -> parameter that contains the index of the current vertex.
// This usually an index into the vertex buffer
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 0.0, 1.0))));
    output.frag_color = input.inColor;
    output.frag_uv = input.inUV;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_Target {
    float3 color = inVert.frag_color;
    return float4(inVert.frag_uv, 0.0, 1.0); // red triangle
}

/*
 * inPosition and inColor are vertex attributes. These are properties that are specified per vertex in the vertex buffer
*/
