#pragma once

#include "anim/AnimationSampler.h"
#include "core/NonCopyable.h"
#include "import/Model.h"
#include "render/RenderPass.h"
#include "render/r3d/Scene3D.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11DeviceContext;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

namespace engine::import { struct TgaImage; }

namespace engine::render
{
    struct ShaderProgram;

    // Draws one FBX model, loaded once at startup:
    //   1. inverted-hull silhouette outline (assets/shaders/outline.hlsl)
    //   2. cel-shaded surface, textured        (assets/shaders/cel.hlsl)
    //   3. interior crease lines, AO-tinted    (assets/shaders/crease.hlsl,
    //      geometry from import/CreaseLines) - stays bind-pose even while
    //      skinned (baked once at load; see docs/model-animation-research.md §5)
    // If the model has a skeleton, also loads every clip named in
    // CharacterAnimationClips.h and, when a ModelDraw asks for one
    // (animClipIndex >= 0), CPU-skins submeshes 1 and 2 each frame from the
    // sampled bone palette before drawing (dynamic vertex buffers; §5.2/5.3).
    // If the file cannot be loaded the pass draws nothing.
    class ModelMeshPass3D final : public IRenderPass, private core::NonCopyable
    {
    public:
        explicit ModelMeshPass3D(std::string modelPath);

        [[nodiscard]] const char* Name() const override { return "ModelMeshPass3D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void RenderShadow(const ShadowContext& context) override;
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

            // Skinning (only set up when the mesh came in skinned and clips
            // loaded - see LoadModel). When true, vertexBuffer/hullVertexBuffer
            // are DYNAMIC and re-filled by SkinAndUpload() every frame from
            // these bind-pose sources; when false they are IMMUTABLE and never
            // touched again after LoadModel, exactly as before skinning existed.
            bool skinned{ false };
            std::vector<import::ModelVertex> bindVertices;     // bind pose; source for the cel pass
            std::vector<math::Vec3> bindSmoothNormals;         // parallel to bindVertices; source for the hull pass
        };

        void LoadModel(ID3D11Device* device);
        ID3D11ShaderResourceView* CreateTextureSrv(ID3D11Device* device, const std::string& fileName,
                                                   const engine::import::TgaImage& image);
        void UpdateSkinningForFrame(ID3D11DeviceContext* context, const Scene3D& scene);
        void SkinAndUpload(ID3D11DeviceContext* context, SubMesh& sub) const;

        std::string m_modelPath;
        std::string m_resolvedDir;   // directory the FBX actually loaded from
        std::vector<SubMesh> m_subMeshes;
        std::unordered_map<std::string, ID3D11ShaderResourceView*> m_textures;   // filename -> SRV (owned)

        import::Skeleton m_skeleton;                  // empty if the model had none
        std::vector<import::AnimationClip> m_clips;   // parallel to CharacterAnimationClips.h; may contain empty clips for files that failed to load
        anim::AnimationSampler m_animSampler;
        std::vector<math::Mat4> m_boneScratch;        // this frame's palette; reused across submeshes
        std::uint64_t m_lastSkinnedFrame{ ~0ull };    // frameNumber already skinned (shadow pass usually runs first)

        const ShaderProgram* m_shader{};          // "cel"
        const ShaderProgram* m_outlineShader{};   // "outline"
        const ShaderProgram* m_creaseShader{};    // "crease"
        const ShaderProgram* m_shadowShader{};    // "shadow" (depth-only)

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
