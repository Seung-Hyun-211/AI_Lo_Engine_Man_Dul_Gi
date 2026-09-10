#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"

#include <cstdint>

struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace engine::render
{
    struct ShaderProgram;

    // Draws snapshot.scene3d.debugLines as a world-space colored line list,
    // after the shaded geometry. Depth-tested (so lines behind solids are
    // hidden) but no depth write. Dev-only visualisation - collider AABBs,
    // raycasts, skeletons. Shader: assets/shaders/debugline.hlsl.
    class DebugDrawPass final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "DebugDrawPass"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        const ShaderProgram* m_shader{};   // "debugline", owned by ShaderLibrary
        ID3D11Buffer* m_vertexBuffer{};
        ID3D11Buffer* m_frameConstants{};
        ID3D11DepthStencilState* m_depthReadNoWrite{};
        ID3D11RasterizerState* m_raster{};
    };
}
