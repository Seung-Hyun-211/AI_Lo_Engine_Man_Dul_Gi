#include "render/r3d/DebugDrawPass.h"

#if defined(ENGINE_WITH_3D)

#include "render/r3d/FrameConstants.h"
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    struct Vertex { float x, y, z, r, g, b, a; };   // stride 28
    constexpr std::size_t kMaxVertices = 131'072;   // 65k line segments

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
    void DebugDrawPass::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_shader = shaders.Get(device, "debugline", layout, ARRAYSIZE(layout));

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * kMaxVertices);
        vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(device->CreateBuffer(&vertexDesc, nullptr, &m_vertexBuffer), "CreateBuffer (debug vertex) failed");

        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(FrameConstantsGpu);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&constantDesc, nullptr, &m_frameConstants), "CreateBuffer (debug frame) failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;   // test, don't write
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthReadNoWrite), "CreateDepthStencilState (debug) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = TRUE;
        rasterDesc.AntialiasedLineEnable = TRUE;
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_raster), "CreateRasterizerState (debug) failed");
    }

    void DebugDrawPass::Execute(const PassContext& context)
    {
        const Scene3D& scene = context.snapshot->scene3d;
        if (scene.debugLines.empty() || m_shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;

        std::vector<Vertex> vertices;
        vertices.reserve(2 * scene.debugLines.size());
        for (const DebugLine& line : scene.debugLines)
        {
            if (vertices.size() + 2 > kMaxVertices) break;
            vertices.push_back({ line.a.x, line.a.y, line.a.z, line.color.r, line.color.g, line.color.b, line.color.a });
            vertices.push_back({ line.b.x, line.b.y, line.b.z, line.color.r, line.color.g, line.color.b, line.color.a });
        }
        if (vertices.empty()) return;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(device->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (debug vertex) failed");
        std::memcpy(mapped.pData, vertices.data(), sizeof(Vertex) * vertices.size());
        device->Unmap(m_vertexBuffer, 0);

        FrameConstantsGpu frame{};
        FillFrameConstants(scene.camera, scene.lighting, frame);
        device->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->OMSetDepthStencilState(m_depthReadNoWrite, 0);
        device->RSSetState(m_raster);
        device->IASetInputLayout(m_shader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        const UINT stride = sizeof(Vertex), offset = 0;
        device->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
        device->VSSetShader(m_shader->vs, nullptr, 0);
        device->VSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetShader(m_shader->ps, nullptr, 0);
        device->Draw(static_cast<UINT>(vertices.size()), 0);
    }

    void DebugDrawPass::Release()
    {
        m_shader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_raster);
        SafeRelease(m_depthReadNoWrite);
        SafeRelease(m_frameConstants);
        SafeRelease(m_vertexBuffer);
    }
}

#endif  // ENGINE_WITH_3D
