#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"
#include "render/r3d/Scene3D.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace engine::render
{
    // 3D pass: draws snapshot.meshDraws with a perspective camera, a depth
    // buffer, back-face culling, and one directional light (Lambert). Built-in
    // meshes (cube, plane) are created here at startup; MeshDraw::mesh indexes
    // into them.
    class MeshPass3D final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "MeshPass3D"; }
        void Initialize(ID3D11Device* device) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        struct GpuMesh
        {
            ID3D11Buffer* vertexBuffer{};
            ID3D11Buffer* indexBuffer{};
            std::uint32_t indexCount{};
        };

        void CreateMesh(ID3D11Device* device, MeshId id, const struct MeshData& data);

        std::array<GpuMesh, static_cast<std::size_t>(MeshId::Count)> m_meshes{};
        ID3D11VertexShader* m_vertexShader{};
        ID3D11PixelShader* m_pixelShader{};
        ID3D11InputLayout* m_inputLayout{};
        ID3D11Buffer* m_frameConstants{};
        ID3D11Buffer* m_objectConstants{};
        ID3D11DepthStencilState* m_depthEnabled{};
        ID3D11RasterizerState* m_rasterizer{};
    };
}
