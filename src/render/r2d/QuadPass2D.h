#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;

namespace engine::render
{
    struct ShaderProgram;

    // 2D pass: draws snapshot.worldQuads then snapshot.uiQuads as screen-space
    // triangles. Depth test off (painter's order), straight-alpha blend on so
    // translucent UI colors composite. Shader: assets/shaders/quad2d.hlsl.
    class QuadPass2D final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "QuadPass2D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        const ShaderProgram* m_shader{};   // owned by ShaderLibrary
        ID3D11Buffer* m_vertexBuffer{};
        ID3D11Buffer* m_constantBuffer{};
        ID3D11BlendState* m_blendState{};
        ID3D11DepthStencilState* m_depthDisabled{};
    };
}
