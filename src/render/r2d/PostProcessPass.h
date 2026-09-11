#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

struct ID3D11RasterizerState;

namespace engine::render
{
    struct ShaderProgram;

    // Runs between the geometry stage and the 2D overlay, owning the hand-off
    // from "draw into the (possibly multisampled) scene colour target" to "draw
    // straight into the back buffer" - see docs/post-process-gbuffer-research.md
    // §3/§12.1. Baseline like QuadPass2D: never excluded by ENGINE_WITH_3D. In
    // its current pass-through form it only reads the generic scene colour
    // target, nothing 3D-specific, so it lives here rather than under r3d; a
    // later step that adds a depth/normal G-buffer read may need to revisit
    // that (docs/post-process-gbuffer-research.md §12.7/§12.10).
    //
    // Today this is a pass-through composite: it copies scene colour to the
    // back buffer, replacing the old end-of-frame MSAA ResolveSubresource
    // (§12.3/§12.11 step 3). AO and fog grow this same shader incrementally in
    // later steps - composite.hlsl/composite_ms.hlsl are the seed, not
    // throwaway placeholders.
    class PostProcessPass final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "PostProcessPass"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        const ShaderProgram* m_compositeShader{};     // composite.hlsl    - single-sample scene colour
        const ShaderProgram* m_compositeMsShader{};   // composite_ms.hlsl - multisampled scene colour
        ID3D11RasterizerState* m_rasterizer{};         // cull none - winding of the utility triangle is irrelevant
    };
}
