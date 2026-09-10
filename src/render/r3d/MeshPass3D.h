#pragma once

#include "core/NonCopyable.h"
#include "render/RenderPass.h"
#include "render/r3d/Scene3D.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct ID3D11Buffer;
struct ID3D11DeviceContext;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

namespace engine::render
{
    struct ShaderProgram;

    // 3D pass: draws snapshot.scene3d.meshDraws with a perspective camera, a
    // depth buffer, and one directional light. Built-in meshes (cube, plane) are
    // created here at startup. Shader: assets/shaders/mesh.hlsl.
    class MeshPass3D final : public IRenderPass, private core::NonCopyable
    {
    public:
        [[nodiscard]] const char* Name() const override { return "MeshPass3D"; }
        void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
        void Execute(const PassContext& context) override;
        void RenderShadow(const ShadowContext& context) override;
        void Release() override;

    private:
        struct GpuMesh
        {
            ID3D11Buffer* vertexBuffer{};
            ID3D11Buffer* indexBuffer{};
            std::uint32_t indexCount{};
        };

        void CreateMesh(ID3D11Device* device, MeshId id, const struct MeshData& data);
        // Loads kCrowdModelFbx (bind pose, position+normal only) into the
        // MeshId::CrowdModel slot, auto-reorienting Z-up assets and normalising
        // to feet-at-origin / unit height. No-op if the path is empty; falls
        // back to the cube if a set path fails to load. The instanced crowd
        // (game/CrowdConfig.h) uses this slot when CrowdMesh::Model is active.
        void LoadCrowdMesh(ID3D11Device* device);
        void DrawInstanced(ID3D11DeviceContext* context, const Scene3D& scene, bool shadow);

        // Upper bound on instances uploaded per frame. Must match the cap the
        // SnapshotBuilder applies. docs/instanced-rendering.md §4.2/§9.3.
        static constexpr std::uint32_t kMaxInstances = 16384;

        std::array<GpuMesh, static_cast<std::size_t>(MeshId::Count)> m_meshes{};
        const ShaderProgram* m_shader{};              // owned by ShaderLibrary
        const ShaderProgram* m_shadowShader{};        // depth-only, owned by ShaderLibrary
        const ShaderProgram* m_instShader{};          // instanced crowd, owned by ShaderLibrary
        const ShaderProgram* m_shadowInstShader{};    // instanced depth-only, owned by ShaderLibrary
        ID3D11Buffer* m_frameConstants{};
        ID3D11Buffer* m_objectConstants{};
        ID3D11Buffer* m_instanceBuffer{};             // DYNAMIC, kMaxInstances * sizeof(MeshInstance)
        ID3D11DepthStencilState* m_depthEnabled{};
        ID3D11RasterizerState* m_rasterizer{};
        ID3D11SamplerState* m_sampler{};              // LINEAR / WRAP, for the crowd diffuse
        ID3D11ShaderResourceView* m_whiteSrv{};       // 1x1 white fallback (no crowd texture)
        ID3D11ShaderResourceView* m_crowdDiffuseSrv{};// kCrowdDiffuseTex, or null -> white
        ID3D11ShaderResourceView* m_vatSrv{};         // crowd VAT (baked clip positions), or null -> static
        ID3D11Buffer* m_vatInfo{};                    // cbuffer b2: (sampleRate, frameCount, 0, 0)
        float m_vatSampleRate{ 0.0f };
        float m_vatFrameCount{ 0.0f };                // 0 => no VAT (shader draws bind pose)
    };
}
