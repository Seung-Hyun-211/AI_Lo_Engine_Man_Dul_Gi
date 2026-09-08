#include "render/r2d/QuadPass2D.h"

#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    struct Vertex { float x, y, r, g, b, a; };
    struct Constants { float screenWidth, screenHeight, unused0, unused1; };

    // ~43k quads/frame. World sample + UI overlay stay well under this.
    constexpr std::size_t kMaxVertices = 262'144;

    void AddQuad(std::vector<Vertex>& vertices, const engine::render::Quad& quad)
    {
        const Vertex tl{ quad.x, quad.y, quad.r, quad.g, quad.b, quad.a };
        const Vertex tr{ quad.x + quad.width, quad.y, quad.r, quad.g, quad.b, quad.a };
        const Vertex bl{ quad.x, quad.y + quad.height, quad.r, quad.g, quad.b, quad.a };
        const Vertex br{ quad.x + quad.width, quad.y + quad.height, quad.r, quad.g, quad.b, quad.a };
        vertices.insert(vertices.end(), { tl, tr, bl, bl, tr, br });
    }

    void AppendQuads(std::vector<Vertex>& vertices, const std::vector<engine::render::Quad>& quads)
    {
        for (const engine::render::Quad& quad : quads)
        {
            if (vertices.size() + 6 > kMaxVertices) break;
            AddQuad(vertices, quad);
        }
    }

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
    void QuadPass2D::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_shader = shaders.Get(device, "quad2d", layout, ARRAYSIZE(layout));

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * kMaxVertices);
        vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(device->CreateBuffer(&vertexDesc, nullptr, &m_vertexBuffer), "CreateBuffer (quad vertex) failed");

        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(Constants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&constantDesc, nullptr, &m_constantBuffer), "CreateBuffer (quad constant) failed");

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(device->CreateBlendState(&blendDesc, &m_blendState), "CreateBlendState failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = FALSE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.StencilEnable = FALSE;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthDisabled), "CreateDepthStencilState (quad) failed");
    }

    void QuadPass2D::Execute(const PassContext& context)
    {
        const RenderSnapshot& snapshot = *context.snapshot;
        std::vector<Vertex> vertices;
        vertices.reserve(6 * (snapshot.worldQuads.size() + snapshot.uiQuads.size()));
        AppendQuads(vertices, snapshot.worldQuads);
        AppendQuads(vertices, snapshot.uiQuads);
        if (vertices.empty() || m_shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(device->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (quad vertex) failed");
        std::memcpy(mapped.pData, vertices.data(), sizeof(Vertex) * vertices.size());
        device->Unmap(m_vertexBuffer, 0);

        const Constants constants{ static_cast<float>(context.viewportWidth), static_cast<float>(context.viewportHeight), 0, 0 };
        device->UpdateSubresource(m_constantBuffer, 0, nullptr, &constants, 0, 0);

        const UINT stride = sizeof(Vertex), offset = 0;
        const float blendFactor[4]{ 0, 0, 0, 0 };
        device->OMSetBlendState(m_blendState, blendFactor, 0xffffffff);
        device->OMSetDepthStencilState(m_depthDisabled, 0);
        device->IASetInputLayout(m_shader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
        device->VSSetShader(m_shader->vs, nullptr, 0);
        device->VSSetConstantBuffers(0, 1, &m_constantBuffer);
        device->PSSetShader(m_shader->ps, nullptr, 0);
        device->Draw(static_cast<UINT>(vertices.size()), 0);
    }

    void QuadPass2D::Release()
    {
        m_shader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_depthDisabled);
        SafeRelease(m_blendState);
        SafeRelease(m_constantBuffer);
        SafeRelease(m_vertexBuffer);
    }
}
