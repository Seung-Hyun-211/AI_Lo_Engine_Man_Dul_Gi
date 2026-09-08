#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;

namespace engine::render
{
    // 2D pass: draws snapshot.worldQuads then snapshot.uiQuads as screen-space
    // triangles. Depth test off (painter's order), straight-alpha blend on so
    // translucent UII colors composite. This is the original sprite path, moved
    // out of Dx11Renderer so the renderer core is just "clear, run passes,
    // present".
    class QuadPass2D final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "QuadPass2D"; }
        void Initialize(ID3D11Device* device) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        ID3D11VertexShader* m_vertexShader{};
        ID3D11PixelShader* m_pixelShader{};
        ID3D11InputLayout* m_inputLayout{};
        ID3D11Buffer* m_vertexBuffer{};
        ID3D11Buffer* m_constantBuffer{};
        ID3D11BlendState* m_blendState{};
        ID3D11DepthStencilState* m_depthDisabled{};
    };
}
