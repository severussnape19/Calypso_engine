struct VSInput {
    [[vk::location(0)]] float2 inPosition;
    [[vk::location(1)]] float3 inColor;
};

struct VSOutput {
    float4 pos: SV_Position;
    float3 color;
};

// This function is invoked for every vertex
// SV_VertexID -> parameter that contains the index of the current vertex.
// This usually an index into the vertex buffer
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = float4(input.inPosition, 0.0, 1.0);
    output.color = input.inColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_Target {
    float3 color = inVert.color;
    return float4(color, 1.0); // red triangle
}

/*
 * inPosition and inColor are vertex attributes. These are properties that are specified per vertex in the vertex buffer
*/
