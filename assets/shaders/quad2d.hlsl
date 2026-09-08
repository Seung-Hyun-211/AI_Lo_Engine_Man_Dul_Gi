// QuadPass2D: screen-space colored quads (world overlay + UI). No lighting.
cbuffer Screen : register(b0) { float2 screen; float2 unused; };

struct VSIn  { float2 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = float4(input.pos.x / screen.x * 2.0f - 1.0f,
                        1.0f - input.pos.y / screen.y * 2.0f, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}
