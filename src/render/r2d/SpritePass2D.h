#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

#include <cstdint>
#include <string>

struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

namespace engine::render
{
    struct ShaderProgram;

    // 2D pass: draws snapshot.uiSprites - textured, tinted, optionally
    // scissor-clipped screen-space quads - after QuadPass2D. Consecutive sprites
    // that share (atlasId, clip) are one draw call; a change of atlas rebinds the
    // SRV, a change of clip calls RSSetScissorRects. `atlasId == 0` uses a
    // built-in 1x1 white texture (solid rect = white sprite + tint).
    //
    // For this first pass a single atlas page (kUiAtlasId) is loaded from a .dds
    // at Initialize(), the same load-once shape as ModelMeshPass3D. A resident
    // atlas registry replaces that when loading-and-streaming lands.
    // Shader: assets/shaders/sprite2d.hlsl. See docs/texture-atlas-and-sprite-pass.md.
    class SpritePass2D final : public IRenderPass, private core::NonCopyable
    {
    public:
        explicit SpritePass2D(std::string atlasDdsPath);

        [[nodiscard]] const char* Name() const override { return "SpritePass2D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        [[nodiscard]] ID3D11ShaderResourceView* SrvFor(std::uint32_t atlasId) const;

        std::string m_atlasDdsPath;

        const ShaderProgram* m_shader{};   // "sprite2d", owned by ShaderLibrary
        ID3D11Buffer* m_vertexBuffer{};
        ID3D11Buffer* m_constantBuffer{};
        ID3D11BlendState* m_blendState{};
        ID3D11DepthStencilState* m_depthDisabled{};
        ID3D11RasterizerState* m_scissorRaster{};   // solid, cull none, ScissorEnable
        ID3D11SamplerState* m_sampler{};

        ID3D11ShaderResourceView* m_whiteSrv{};     // atlasId 0
        ID3D11ShaderResourceView* m_atlasSrv{};     // atlasId kUiAtlasId
    };
}
