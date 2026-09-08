#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"
#include "render/r3d/Scene3D.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

namespace engine::import { struct TgaImage; }

namespace engine::render
{
    struct ShaderProgram;

    // Draws one FBX model, loaded once at startup, as static geometry (bind pose
    // - no skinning yet):
    //   1. inverted-hull silhouette outline (assets/shaders/outline.hlsl)
    //   2. cel-shaded surface, textured        (assets/shaders/cel.hlsl)
    //   3. interior crease lines, AO-tinted    (assets/shaders/crease.hlsl,
    //      geometry from import/CreaseLines)
    // If the file cannot be loaded the pass draws nothing.
    class ModelMeshPass3D final : public IRenderPass, private core::NonCopyable
    {
    public:
        explicit ModelMeshPass3D(std::string modelPath);

        [[nodiscard]] const char* Name() const override { return "ModelMeshPass3D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void Release() override;

    private:
        struct SubMesh
        {
            ID3D11Buffer* vertexBuffer{};       // ModelVertex, stride 64 (cel pass)
            ID3D11Buffer* hullVertexBuffer{};   // pos + smoothed normal, stride 24 (outline pass)
            ID3D11Buffer* indexBuffer{};
            std::uint32_t indexCount{};
            std::uint32_t vertexStride{};
            math::Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
            float shadowBias{};                    // degrees; larger on face/skin materials
            ID3D11ShaderResourceView* texture{};   // non-owning; owned by m_textures
        };

        void LoadModel(ID3D11Device* device);
        ID3D11ShaderResourceView* CreateTextureSrv(ID3D11Device* device, const std::string& fileName,
                                                   const engine::import::TgaImage& image);

        std::string m_modelPath;
        std::string m_resolvedDir;   // directory the FBX actually loaded from
        std::vector<SubMesh> m_subMeshes;
        std::unordered_map<std::string, ID3D11ShaderResourceView*> m_textures;   // filename -> SRV (owned)

        const ShaderProgram* m_shader{};          // "cel"
        const ShaderProgram* m_outlineShader{};   // "outline"
        const ShaderProgram* m_creaseShader{};    // "crease"

        ID3D11Buffer* m_frameConstants{};
        ID3D11Buffer* m_objectConstants{};
        ID3D11Buffer* m_outlineConstants{};       // b2: outline width
        ID3D11Buffer* m_celConstants{};           // b3: per-material shadow bias
        ID3D11Buffer* m_creaseVertexBuffer{};
        std::uint32_t m_creaseVertexCount{};

        ID3D11DepthStencilState* m_depthEnabled{};        // test LESS + write
        ID3D11DepthStencilState* m_depthReadLessEqual{};  // test LESS_EQUAL, no write (crease)
        ID3D11RasterizerState* m_rasterizer{};            // solid, cull none
        ID3D11RasterizerState* m_outlineRasterizer{};     // solid, cull front (hull)
        ID3D11SamplerState* m_sampler{};
        ID3D11ShaderResourceView* m_whiteTexture{};       // 1x1 fallback
    };
}
