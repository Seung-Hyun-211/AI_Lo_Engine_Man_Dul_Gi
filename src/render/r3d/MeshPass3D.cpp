#include "render/r3d/MeshPass3D.h"

#if defined(ENGINE_WITH_3D)

#include "math/Math3D.h"
#include "render/r3d/FrameConstants.h"
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace engine::render
{
    // Position + normal. Matches the input layout and the HLSL VSIn below.
    struct MeshVertex { float px, py, pz, nx, ny, nz; };

    struct MeshData
    {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
}

namespace
{
    using engine::math::Vec3;
    using engine::render::MeshData;
    using engine::render::MeshVertex;

    struct ObjectConstants { float world[16]; float color[4]; };

    void ThrowIfFailed(HRESULT result, const char* message)
    {
        if (FAILED(result)) throw std::runtime_error(message);
    }

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr) { object->Release(); object = nullptr; }
    }

    // Adds one quad face (two triangles) spanning center +- u/2 +- v/2, all four
    // vertices sharing the face normal. Winding is CCW seen from outside.
    void AddFace(MeshData& mesh, Vec3 center, Vec3 u, Vec3 v, Vec3 normal)
    {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const Vec3 corners[4]{
            center - u * 0.5f - v * 0.5f,
            center + u * 0.5f - v * 0.5f,
            center + u * 0.5f + v * 0.5f,
            center - u * 0.5f + v * 0.5f,
        };
        for (const Vec3& c : corners)
            mesh.vertices.push_back({ c.x, c.y, c.z, normal.x, normal.y, normal.z });
        for (std::uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u })
            mesh.indices.push_back(base + index);
    }

    MeshData MakeCube()
    {
        MeshData mesh;
        AddFace(mesh, { 0.5f, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 });
        AddFace(mesh, { -0.5f, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 });
        AddFace(mesh, { 0, 0.5f, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 });
        AddFace(mesh, { 0, -0.5f, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 });
        AddFace(mesh, { 0, 0, 0.5f }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
        AddFace(mesh, { 0, 0, -0.5f }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 });
        return mesh;
    }

    MeshData MakePlane()
    {
        MeshData mesh;
        AddFace(mesh, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 });
        return mesh;
    }
}

namespace engine::render
{
    void MeshPass3D::CreateMesh(ID3D11Device* device, MeshId id, const MeshData& data)
    {
        GpuMesh& mesh = m_meshes[static_cast<std::size_t>(id)];

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(sizeof(MeshVertex) * data.vertices.size());
        vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexInit{ data.vertices.data(), 0, 0 };
        ThrowIfFailed(device->CreateBuffer(&vertexDesc, &vertexInit, &mesh.vertexBuffer), "CreateBuffer (mesh vertex) failed");

        D3D11_BUFFER_DESC indexDesc{};
        indexDesc.ByteWidth = static_cast<UINT>(sizeof(std::uint32_t) * data.indices.size());
        indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indexInit{ data.indices.data(), 0, 0 };
        ThrowIfFailed(device->CreateBuffer(&indexDesc, &indexInit, &mesh.indexBuffer), "CreateBuffer (mesh index) failed");

        mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
    }

    void MeshPass3D::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_shader = shaders.Get(device, "mesh", layout, ARRAYSIZE(layout));

        D3D11_BUFFER_DESC frameDesc{};
        frameDesc.ByteWidth = sizeof(FrameConstantsGpu);
        frameDesc.Usage = D3D11_USAGE_DEFAULT;
        frameDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&frameDesc, nullptr, &m_frameConstants), "CreateBuffer (mesh frame) failed");

        D3D11_BUFFER_DESC objectDesc{};
        objectDesc.ByteWidth = sizeof(ObjectConstants);
        objectDesc.Usage = D3D11_USAGE_DEFAULT;
        objectDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&objectDesc, nullptr, &m_objectConstants), "CreateBuffer (mesh object) failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        depthDesc.StencilEnable = FALSE;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthEnabled), "CreateDepthStencilState (mesh) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        // Culling disabled so the skeleton does not depend on winding matching
        // the left-handed projection. Switch to D3D11_CULL_BACK once verified.
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.FrontCounterClockwise = FALSE;
        rasterDesc.DepthClipEnable = TRUE;
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState failed");

        CreateMesh(device, MeshId::Cube, MakeCube());
        CreateMesh(device, MeshId::Plane, MakePlane());
    }

    void MeshPass3D::Execute(const PassContext& context)
    {
        const Scene3D& scene = context.snapshot->scene3d;
        if (scene.meshDraws.empty() || m_shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;

        FrameConstantsGpu frame{};
        FillFrameConstants(scene.camera, scene.lighting, frame);
        device->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->OMSetDepthStencilState(m_depthEnabled, 0);
        device->RSSetState(m_rasterizer);
        device->IASetInputLayout(m_shader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(m_shader->vs, nullptr, 0);
        device->PSSetShader(m_shader->ps, nullptr, 0);
        device->VSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetConstantBuffers(0, 1, &m_frameConstants);

        for (const MeshDraw& draw : scene.meshDraws)
        {
            const GpuMesh& mesh = m_meshes[static_cast<std::size_t>(draw.mesh)];
            if (mesh.vertexBuffer == nullptr) continue;

            ObjectConstants object{};
            std::memcpy(object.world, draw.world.m, sizeof(object.world));
            object.color[0] = draw.color.r;
            object.color[1] = draw.color.g;
            object.color[2] = draw.color.b;
            object.color[3] = draw.color.a;
            device->UpdateSubresource(m_objectConstants, 0, nullptr, &object, 0, 0);
            device->VSSetConstantBuffers(1, 1, &m_objectConstants);
            device->PSSetConstantBuffers(1, 1, &m_objectConstants);

            const UINT stride = sizeof(MeshVertex), offset = 0;
            device->IASetVertexBuffers(0, 1, &mesh.vertexBuffer, &stride, &offset);
            device->IASetIndexBuffer(mesh.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            device->DrawIndexed(mesh.indexCount, 0, 0);
        }
    }

    void MeshPass3D::Release()
    {
        for (GpuMesh& mesh : m_meshes)
        {
            SafeRelease(mesh.vertexBuffer);
            SafeRelease(mesh.indexBuffer);
            mesh.indexCount = 0;
        }
        m_shader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_rasterizer);
        SafeRelease(m_depthEnabled);
        SafeRelease(m_objectConstants);
        SafeRelease(m_frameConstants);
    }
}

#endif  // ENGINE_WITH_3D
