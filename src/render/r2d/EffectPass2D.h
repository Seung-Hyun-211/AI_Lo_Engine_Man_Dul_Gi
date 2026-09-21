#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

#include <cstdint>

struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace engine::render
{
    struct ShaderProgram;

    // 2D pass: draws snapshot.worldEffects - additive, textureless glow
    // billboards in screen space (card/weapon hit & death VFX for the
    // "Circular" scene, docs/circular-design.md §7 option A). Same "no
    // vertex buffer, SV_VertexID builds the quad" shape as ParticlePass3D,
    // but this module has no camera matrix and no depth buffer to test
    // against, and it is always additive (no alpha-blend batch split - v1
    // has one kind of effect). Runs after QuadPass2D (so a hit flash layers
    // on top of the mob/player quad it hit) and before SpritePass2D (so UI
    // stays on top of gameplay VFX) - see main.cpp's AddRenderPass order.
    // Shader: assets/shaders/effect2d.hlsl.
    class EffectPass2D final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "EffectPass2D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        // Upper bound on instances uploaded per frame - matches
        // ParticlePass3D::kMaxInstances's role (SnapshotBuilder caps to
        // whichever is smaller).
        static constexpr std::uint32_t kMaxInstances = 4096;

        const ShaderProgram* m_shader{};   // "effect2d", owned by ShaderLibrary
        ID3D11Buffer* m_constantBuffer{};  // Screen (width/height) - same cbuffer shape as sprite2d.hlsl
        ID3D11Buffer* m_instanceBuffer{};  // DYNAMIC, no per-vertex buffer needed
        ID3D11BlendState* m_additiveBlend{};
        ID3D11DepthStencilState* m_depthDisabled{};
        ID3D11RasterizerState* m_rasterizer{};
    };
}
