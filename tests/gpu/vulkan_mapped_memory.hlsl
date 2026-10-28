struct Vertex {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

Vertex VSMain(float2 position : POSITION, float4 color : COLOR0) {
    Vertex result;
    result.position = float4(position, 0.0f, 1.0f);
    result.color = color;
    return result;
}

float4 PSMain(Vertex input) : SV_Target {
    return input.color;
}
