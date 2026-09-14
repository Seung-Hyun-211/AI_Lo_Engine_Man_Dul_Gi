#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"
#include "render/r3d/Scene3D.h"

struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace engine::render
{
    struct ShaderProgram;

    // Draws snapshot.scene3d.particleInstances - camera-facing billboards, no
    // vertex buffer (SV_VertexID builds the quad), no texture (a procedural
    // soft circle, assets/shaders/particle.hlsl). Separate from MeshPass3D on
    // purpose: different vertex format, no lighting, two blend modes instead
    // of opaque, depth-tested but not depth-written (docs/particle-system-
    // research.md §5).
    class ParticlePass3D final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "ParticlePass3D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;
        // No RenderShadow override: particles neither cast nor receive shadows
        // (default IRenderPass::RenderShadow casts nothing).

    private:
        // Upper bound on instances uploaded per frame. Must stay in sync with
        // vfx::ParticleSystem::kCapacity (SnapshotBuilder caps to whichever is
        // smaller, same convention as MeshPass3D::kMaxInstances).
        static constexpr std::uint32_t kMaxInstances = 4096;

        const ShaderProgram* m_shader{};                 // owned by ShaderLibrary
        ID3D11Buffer* m_frameConstants{};
        ID3D11Buffer* m_instanceBuffer{};                // DYNAMIC, no per-vertex buffer needed
        ID3D11BlendState* m_additiveBlend{};
        ID3D11BlendState* m_alphaBlend{};
        ID3D11DepthStencilState* m_depthState{};          // test on, write off
        ID3D11RasterizerState* m_rasterizer{};            // cull none, MSAA on
    };
}
