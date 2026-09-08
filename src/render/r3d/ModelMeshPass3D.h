#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"
#include "render/r3d/Scene3D.h"

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace engine::render
{
    // Draws one FBX model, loaded once at startup, as static lit geometry (bind
    // pose - no skinning yet). It reuses the MeshPass3D lighting model. The model
    // path is given to the constructor; if the file cannot be loaded the pass
    // simply draws nothing.
    //
    // This is the "see the model" milestone. Skinned animation replaces the
    // static draw with SkinnedMeshPass3D - see docs/model-animation-research.md.
    class ModelMeshPass3D final : public IRenderPass, private core::NonCopyable
    {
    public:
        explicit ModelMeshPass3D(std::string modelPath);

        [[nodiscard]] const char* Name() const override { return "ModelMeshPass3D"; }
        void Initialize(ID3D11Device* device) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        struct SubMesh
        {
            ID3D11Buffer* vertexBuffer{};
            ID3D11Buffer* indexBuffer{};
            std::uint32_t indexCount{};
            std::uint32_t vertexStride{};
            math::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        void LoadModel(ID3D11Device* device);

        std::string m_modelPath;
        std::vector<SubMesh> m_subMeshes;
        ID3D11VertexShader* m_vertexShader{};
        ID3D11PixelShader* m_pixelShader{};
        ID3D11InputLayout* m_inputLayout{};
        ID3D11Buffer* m_frameConstants{};
        ID3D11Buffer* m_objectConstants{};
        ID3D11DepthStencilState* m_depthEnabled{};
        ID3D11RasterizerState* m_rasterizer{};
    };
}
