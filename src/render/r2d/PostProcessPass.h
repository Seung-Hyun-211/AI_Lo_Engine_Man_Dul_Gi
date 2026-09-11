#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

#include <array>
#include <cstdint>

struct ID3D11Buffer;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace engine::render
{
    struct ShaderProgram;

    // Runs between the geometry stage and the 2D overlay, owning the hand-off
    // from "draw into the (possibly multisampled) scene colour target" to "draw
    // straight into the back buffer" - see docs/post-process-gbuffer-research.md
    // §2. Baseline like QuadPass2D: never excluded by ENGINE_WITH_3D. Its
    // composite stage (colour -> back buffer) has no 3D-specific concept and
    // runs unconditionally; SSAO is inherently a 3D-only idea (view-space
    // reconstruction from a projection matrix, meaningless for a 2D scene), so
    // that half of this class is wrapped in `#if defined(ENGINE_WITH_3D)` and
    // falls back to a 1x1 white AO texture (= no occlusion) when compiled out
    // or when the depth/normal G-buffer isn't available yet - the composite
    // shader always just multiplies by whatever AO texture it's handed.
    class PostProcessPass final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "PostProcessPass"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        void Composite(const PassContext& context, ID3D11ShaderResourceView* aoSrv);

        const ShaderProgram* m_compositeShader{};     // composite.hlsl    - single-sample scene colour
        const ShaderProgram* m_compositeMsShader{};   // composite_ms.hlsl - multisampled scene colour
        ID3D11RasterizerState* m_rasterizer{};         // cull none - winding of the utility triangle is irrelevant
        ID3D11SamplerState* m_aoSampler{};             // linear/clamp - reads whichever AO texture Composite gets
        // b0 for composite*.hlsl: aoStrength + fog params, filled each frame
        // from Scene3D::postProcess (docs/post-process-gbuffer-research.md
        // §8.1) when ENGINE_WITH_3D, else left at "no effect" defaults.
        ID3D11Buffer* m_compositeParams{};

        // 1x1 white R8_UNORM = "no occlusion". The permanent fallback when SSAO
        // isn't available (2D-only build, or before the G-buffer exists).
        ID3D11Texture2D* m_whiteAoTexture{};
        ID3D11ShaderResourceView* m_whiteAoSrv{};

#if defined(ENGINE_WITH_3D)
        [[nodiscard]] ID3D11ShaderResourceView* ComputeAo(const PassContext& context);
        void EnsureAoTarget(ID3D11Device* device, std::uint32_t width, std::uint32_t height);

        static constexpr std::size_t kKernelSize = 16;

        const ShaderProgram* m_ssaoShader{};       // ssao.hlsl    - single-sample depth/normal
        const ShaderProgram* m_ssaoMsShader{};     // ssao_ms.hlsl - multisampled depth/normal
        const ShaderProgram* m_blurShader{};       // ssao_blur.hlsl - 4x4 box blur, matches the noise tile size
        ID3D11SamplerState* m_wrapSampler{};        // point/wrap - the tiling noise texture

        ID3D11Texture2D* m_noiseTexture{};
        ID3D11ShaderResourceView* m_noiseSrv{};
        std::array<float, kKernelSize * 4> m_kernel{};   // xyz per sample, w unused - filled once at Initialize

        ID3D11Buffer* m_ssaoParams{};   // b0 for ssao*.hlsl: kernel + projection scalars + radius/power/bias

        // AO render targets, full-resolution, lazily (re)created in
        // EnsureAoTarget when the viewport size changes - IRenderPass has no
        // resize hook, so this pass tracks its own last-seen size instead
        // (docs/post-process-gbuffer-research.md §4). m_aoTexture
        // holds the raw (noisy) SSAO result; m_aoBlurTexture the blurred
        // result Composite actually reads - see ssao_blur.hlsl for why both
        // exist.
        ID3D11Texture2D* m_aoTexture{};
        ID3D11RenderTargetView* m_aoRtv{};
        ID3D11ShaderResourceView* m_aoSrv{};
        ID3D11Texture2D* m_aoBlurTexture{};
        ID3D11RenderTargetView* m_aoBlurRtv{};
        ID3D11ShaderResourceView* m_aoBlurSrv{};
        std::uint32_t m_aoWidth{};
        std::uint32_t m_aoHeight{};
#endif
    };
}
