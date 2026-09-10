// SpritePass2D: screen-space textured, tinted quads (UI atlas sprites, glyphs).
// One atlas SRV bound per (atlasId, clip) group; scissor set per group by the
// pass. No lighting. See docs/texture-atlas-and-sprite-pass.md.
cbuffer Screen : register(b0) { float2 screen; float2 unused; };

Texture2D    atlas : register(t0);
SamplerState samp  : register(s0);

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD; float4 tint : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 tint : COLOR; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = float4(input.pos.x / screen.x * 2.0f - 1.0f,
                        1.0f - input.pos.y / screen.y * 2.0f, 0.0f, 1.0f);
    output.uv = input.uv;
    output.tint = input.tint;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // Straight multiply. A coverage/SDF font atlas will want tint.rgb with
    // tint.a * sample.r instead - handled when the font moves to an atlas.
    return atlas.Sample(samp, input.uv) * input.tint;
}
