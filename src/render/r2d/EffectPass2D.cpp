#include "render/r2d/EffectPass2D.h"

#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
    struct Constants { float screenWidth, screenHeight, unused0, unused1; };

    void ThrowIfFailed(HRESULT result, const char* message)
    {
        if (FAILED(result)) throw std::runtime_error(message);
    }

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr) { object->Release(); object = nullptr; }
    }
}

namespace engine::render
{
    // Per-instance stream only - no per-vertex buffer, the VS builds the quad
    // from SV_VertexID (effect2d.hlsl). Offsets match EffectInstance.
    void EffectPass2D::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        static_assert(sizeof(EffectInstance) == 24,
            "effect layout assumes {pos@0, radius@8, rotation@12, colorRgba@16, seed@20}");
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,   0,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // pos
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,      0,  8, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // radius
            { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,      0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // rotation
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // colorRgba
            { "TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,      0, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // seed
        };
        m_shader = shaders.Get(device, "effect2d", layout, ARRAYSIZE(layout));

        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(Constants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&constantDesc, nullptr, &m_constantBuffer), "CreateBuffer (effect2d constant) failed");

        D3D11_BUFFER_DESC instDesc{};
        instDesc.ByteWidth = sizeof(EffectInstance) * kMaxInstances;
        instDesc.Usage = D3D11_USAGE_DYNAMIC;
        instDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        instDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(device->CreateBuffer(&instDesc, nullptr, &m_instanceBuffer), "CreateBuffer (effect2d instance) failed");

        D3D11_BLEND_DESC additiveDesc{};
        additiveDesc.RenderTarget[0].BlendEnable = TRUE;
        additiveDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        additiveDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        additiveDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        additiveDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        additiveDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        additiveDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        additiveDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(device->CreateBlendState(&additiveDesc, &m_additiveBlend), "CreateBlendState (effect2d) failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = FALSE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.StencilEnable = FALSE;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthDisabled), "CreateDepthStencilState (effect2d) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = TRUE;   // scene target is multisampled
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState (effect2d) failed");
    }

    void EffectPass2D::Execute(const PassContext& context)
    {
        const RenderSnapshot& snapshot = *context.snapshot;
        const UINT total = static_cast<UINT>(
            std::min<std::size_t>(snapshot.worldEffects.size(), kMaxInstances));
        if (total == 0 || m_shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;

        const Constants constants{ static_cast<float>(context.viewportWidth),
                                   static_cast<float>(context.viewportHeight), 0, 0 };
        device->UpdateSubresource(m_constantBuffer, 0, nullptr, &constants, 0, 0);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(device->Map(m_instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, snapshot.worldEffects.data(), total * sizeof(EffectInstance));
        device->Unmap(m_instanceBuffer, 0);

        const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        device->OMSetBlendState(m_additiveBlend, blendFactor, 0xffffffff);
        device->OMSetDepthStencilState(m_depthDisabled, 0);
        device->RSSetState(m_rasterizer);
        device->IASetInputLayout(m_shader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(m_shader->vs, nullptr, 0);
        device->VSSetConstantBuffers(0, 1, &m_constantBuffer);
        device->PSSetShader(m_shader->ps, nullptr, 0);

        const UINT stride = sizeof(EffectInstance), offset = 0;
        device->IASetVertexBuffers(0, 1, &m_instanceBuffer, &stride, &offset);
        device->DrawInstanced(6, total, 0, 0);
    }

    void EffectPass2D::Release()
    {
        m_shader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_constantBuffer);
        SafeRelease(m_instanceBuffer);
        SafeRelease(m_additiveBlend);
        SafeRelease(m_depthDisabled);
        SafeRelease(m_rasterizer);
    }
}
