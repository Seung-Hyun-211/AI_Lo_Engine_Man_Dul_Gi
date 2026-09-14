#include "render/r3d/ParticlePass3D.h"

#if defined(ENGINE_WITH_3D)

#include "render/r3d/FrameConstants.h"
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
    void ThrowIfFailed(HRESULT result, const char* message)
    {
        if (FAILED(result)) throw std::runtime_error(message);
    }

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr)
        {
            object->Release();
            object = nullptr;
        }
    }
}

namespace engine::render
{
    // Per-instance stream only - no per-vertex buffer, the VS builds the quad
    // from SV_VertexID (particle.hlsl). Offsets match ParticleInstance.
    void ParticlePass3D::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        static_assert(sizeof(ParticleInstance) == 28,
            "particle layout assumes {pos@0, size@12, rotation@16, colorRgba@20, stretch@24}");
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // pos
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // size
            { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // rotation
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // colorRgba
            { "TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,       0, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // stretch
        };
        m_shader = shaders.Get(device, "particle", layout, ARRAYSIZE(layout));

        D3D11_BUFFER_DESC frameDesc{};
        frameDesc.ByteWidth = sizeof(FrameConstantsGpu);
        frameDesc.Usage = D3D11_USAGE_DEFAULT;
        frameDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&frameDesc, nullptr, &m_frameConstants), "CreateBuffer (particle frame) failed");

        D3D11_BUFFER_DESC instDesc{};
        instDesc.ByteWidth = sizeof(ParticleInstance) * kMaxInstances;
        instDesc.Usage = D3D11_USAGE_DYNAMIC;
        instDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        instDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(device->CreateBuffer(&instDesc, nullptr, &m_instanceBuffer), "CreateBuffer (particle instance) failed");

        D3D11_BLEND_DESC additiveDesc{};
        additiveDesc.RenderTarget[0].BlendEnable = TRUE;
        additiveDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        additiveDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        additiveDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        additiveDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        additiveDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        additiveDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        additiveDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(device->CreateBlendState(&additiveDesc, &m_additiveBlend), "CreateBlendState (particle additive) failed");

        D3D11_BLEND_DESC alphaDesc{};
        alphaDesc.RenderTarget[0].BlendEnable = TRUE;
        alphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        alphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        alphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        alphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        alphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        alphaDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        alphaDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(device->CreateBlendState(&alphaDesc, &m_alphaBlend), "CreateBlendState (particle alpha) failed");

        // Test against the opaque scene, never write depth (particles do not
        // occlude each other or themselves - docs/particle-system-research.md §5.1).
        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        depthDesc.StencilEnable = FALSE;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthState), "CreateDepthStencilState (particle) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;   // billboards always face the camera
        rasterDesc.FrontCounterClockwise = FALSE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = TRUE;
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState (particle) failed");
    }

    void ParticlePass3D::Execute(const PassContext& context)
    {
        const RenderSnapshot& snapshot = *context.snapshot;
        const Scene3D& scene = snapshot.scene3d;
        if (m_shader == nullptr || m_instanceBuffer == nullptr) return;

        const UINT total = static_cast<UINT>(
            std::min<std::size_t>(scene.particleInstances.size(), kMaxInstances));
        if (total == 0) return;

        ID3D11DeviceContext* device = context.context;

        FrameConstantsGpu frame{};
        FillFrameConstants(scene.camera, scene.lighting, frame);
        device->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(device->Map(m_instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, scene.particleInstances.data(), total * sizeof(ParticleInstance));
        device->Unmap(m_instanceBuffer, 0);

        device->VSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetConstantBuffers(0, 1, &m_frameConstants);
        device->OMSetDepthStencilState(m_depthState, 0);
        device->RSSetState(m_rasterizer);
        device->IASetInputLayout(m_shader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(m_shader->vs, nullptr, 0);
        device->PSSetShader(m_shader->ps, nullptr, 0);

        const UINT stride = sizeof(ParticleInstance);
        const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };

        for (const ParticleBatch& batch : scene.particleBatches)
        {
            if (batch.first >= total) continue;
            const UINT count = std::min(batch.count, total - batch.first);
            if (count == 0) continue;

            device->OMSetBlendState(batch.blend == ParticleBlend::Additive ? m_additiveBlend : m_alphaBlend,
                                     blendFactor, 0xffffffff);
            const UINT offset = batch.first * stride;
            device->IASetVertexBuffers(0, 1, &m_instanceBuffer, &stride, &offset);
            device->DrawInstanced(6, count, 0, 0);
        }
    }

    void ParticlePass3D::Release()
    {
        m_shader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_frameConstants);
        SafeRelease(m_instanceBuffer);
        SafeRelease(m_additiveBlend);
        SafeRelease(m_alphaBlend);
        SafeRelease(m_depthState);
        SafeRelease(m_rasterizer);
    }
}

#endif
