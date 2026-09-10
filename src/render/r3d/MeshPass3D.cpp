#include "render/r3d/MeshPass3D.h"

#if defined(ENGINE_WITH_3D)

#include "import/ModelImporter.h"
#include "math/Math3D.h"
#include "render/r3d/FrameConstants.h"
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
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

        const D3D11_INPUT_ELEMENT_DESC posOnly[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_shadowShader = shaders.Get(device, "shadow", posOnly, ARRAYSIZE(posOnly));

        // Instanced crowd path: mesh vertex on slot 0, one MeshInstance per
        // instance on slot 1. Byte offsets match render::MeshInstance.
        static_assert(sizeof(MeshInstance) == 24, "instanced input layout offsets assume {pos@0, yaw@12, scale@16, colorRgba@20}");
        const D3D11_INPUT_ELEMENT_DESC instanced[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };
        m_instShader = shaders.Get(device, "mesh_instanced", instanced, ARRAYSIZE(instanced));

        const D3D11_INPUT_ELEMENT_DESC instancedShadow[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };
        m_shadowInstShader = shaders.Get(device, "shadow_instanced", instancedShadow, ARRAYSIZE(instancedShadow));

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

        D3D11_BUFFER_DESC instDesc{};
        instDesc.ByteWidth = sizeof(MeshInstance) * kMaxInstances;
        instDesc.Usage = D3D11_USAGE_DYNAMIC;
        instDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        instDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(device->CreateBuffer(&instDesc, nullptr, &m_instanceBuffer), "CreateBuffer (mesh instance) failed");

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
        rasterDesc.MultisampleEnable = TRUE;   // MSAA coverage (scene target is multisampled)
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState failed");

        CreateMesh(device, MeshId::Cube, MakeCube());
        CreateMesh(device, MeshId::Plane, MakePlane());
        LoadZombieMesh(device);
    }

    void MeshPass3D::LoadZombieMesh(ID3D11Device* device)
    {
        import::ImportOptions opt;
        opt.skipAnimation = true;   // bind pose only - animated crowds need VAT (docs/horde-design.md §5)
        opt.scale = 1.0f;           // height is normalised below, so source units do not matter

        import::ImportResult res;
        for (const char* prefix : { "", "../../", "../../../" })
        {
            res = import::LoadModelFromFile(std::string(prefix) + "assets/models/zombie/Zombie1.FBX", opt);
            if (res.ok) break;
        }

        MeshData data;
        if (res.ok)
        {
            for (const import::ModelMesh& m : res.model.meshes)
            {
                const auto vbase = static_cast<std::uint32_t>(data.vertices.size());
                for (const import::ModelVertex& v : m.vertices)
                    data.vertices.push_back({ v.position.x, v.position.y, v.position.z,
                                              v.normal.x, v.normal.y, v.normal.z });
                for (const std::uint32_t idx : m.indices)
                    data.indices.push_back(vbase + idx);
            }
        }

        if (data.vertices.empty() || data.indices.empty())
        {
            OutputDebugStringA(("MeshPass3D: zombie mesh load failed ('" + res.error
                + "') - crowd falls back to the cube\n").c_str());
            CreateMesh(device, MeshId::Zombie, MakeCube());
            return;
        }

        // Normalise: feet at y = 0, centred on x/z, total height 1. The
        // SnapshotBuilder scales each instance to the metre height it wants.
        float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
        float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
        for (const MeshVertex& v : data.vertices)
        {
            minX = std::min(minX, v.px); maxX = std::max(maxX, v.px);
            minY = std::min(minY, v.py); maxY = std::max(maxY, v.py);
            minZ = std::min(minZ, v.pz); maxZ = std::max(maxZ, v.pz);
        }
        const float s = 1.0f / std::max(maxY - minY, 1e-4f);
        const float cx = (minX + maxX) * 0.5f;
        const float cz = (minZ + maxZ) * 0.5f;
        for (MeshVertex& v : data.vertices)
        {
            v.px = (v.px - cx) * s;
            v.py = (v.py - minY) * s;
            v.pz = (v.pz - cz) * s;
        }

        CreateMesh(device, MeshId::Zombie, data);
        OutputDebugStringA(("MeshPass3D: zombie mesh loaded ("
            + std::to_string(data.vertices.size()) + " verts, "
            + std::to_string(data.indices.size() / 3) + " tris)\n").c_str());
    }

    void MeshPass3D::Execute(const PassContext& context)
    {
        const Scene3D& scene = context.snapshot->scene3d;
        const bool hasUnique = !scene.meshDraws.empty() && m_shader != nullptr;
        const bool hasInstanced = !scene.instanceBatches.empty() && m_instShader != nullptr;
        if (!hasUnique && !hasInstanced) return;

        ID3D11DeviceContext* device = context.context;

        FrameConstantsGpu frame{};
        FillFrameConstants(scene.camera, scene.lighting, frame);
        device->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->OMSetDepthStencilState(m_depthEnabled, 0);
        device->RSSetState(m_rasterizer);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetConstantBuffers(0, 1, &m_frameConstants);

        if (hasUnique)
        {
            device->IASetInputLayout(m_shader->inputLayout);
            device->VSSetShader(m_shader->vs, nullptr, 0);
            device->PSSetShader(m_shader->ps, nullptr, 0);

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

        if (hasInstanced)
            DrawInstanced(device, scene, /*shadow=*/false);
    }

    void MeshPass3D::RenderShadow(const ShadowContext& context)
    {
        const Scene3D& scene = context.snapshot->scene3d;
        const bool hasUnique = !scene.meshDraws.empty() && m_shadowShader != nullptr;
        const bool hasInstanced = !scene.instanceBatches.empty() && m_shadowInstShader != nullptr;
        if (!hasUnique && !hasInstanced) return;

        ID3D11DeviceContext* device = context.context;
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->PSSetShader(nullptr, nullptr, 0);

        if (hasUnique)
        {
            device->IASetInputLayout(m_shadowShader->inputLayout);
            device->VSSetShader(m_shadowShader->vs, nullptr, 0);

            for (const MeshDraw& draw : scene.meshDraws)
            {
                const GpuMesh& mesh = m_meshes[static_cast<std::size_t>(draw.mesh)];
                if (mesh.vertexBuffer == nullptr) continue;

                ObjectConstants object{};
                std::memcpy(object.world, draw.world.m, sizeof(object.world));
                device->UpdateSubresource(m_objectConstants, 0, nullptr, &object, 0, 0);
                device->VSSetConstantBuffers(1, 1, &m_objectConstants);

                const UINT stride = sizeof(MeshVertex), offset = 0;
                device->IASetVertexBuffers(0, 1, &mesh.vertexBuffer, &stride, &offset);
                device->IASetIndexBuffer(mesh.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
                device->DrawIndexed(mesh.indexCount, 0, 0);
            }
        }

        if (hasInstanced)
            DrawInstanced(device, scene, /*shadow=*/true);
    }

    // Uploads scene.meshInstances to the DYNAMIC buffer once, then one
    // DrawIndexedInstanced per batch. `shadow` picks the depth-only shader and
    // skips far-LOD batches (their shadows do not read). docs/instanced-rendering.md §4.
    void MeshPass3D::DrawInstanced(ID3D11DeviceContext* device, const Scene3D& scene, bool shadow)
    {
        const ShaderProgram* program = shadow ? m_shadowInstShader : m_instShader;
        if (program == nullptr || m_instanceBuffer == nullptr) return;

        const UINT total = static_cast<UINT>(
            std::min<std::size_t>(scene.meshInstances.size(), kMaxInstances));
        if (total == 0) return;

        // On the shadow pass, if every batch is far-LOD (lod >= 2) there is
        // nothing to draw - skip the upload entirely.
        if (shadow)
        {
            bool anyCaster = false;
            for (const InstanceBatch& b : scene.instanceBatches)
                if (b.lod < 2 && b.first < total) { anyCaster = true; break; }
            if (!anyCaster) return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(device->Map(m_instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, scene.meshInstances.data(), total * sizeof(MeshInstance));
        device->Unmap(m_instanceBuffer, 0);

        device->IASetInputLayout(program->inputLayout);
        device->VSSetShader(program->vs, nullptr, 0);
        if (!shadow) device->PSSetShader(program->ps, nullptr, 0);

        for (const InstanceBatch& batch : scene.instanceBatches)
        {
            if (shadow && batch.lod >= 2) continue;                 // far crowd casts no shadow
            if (batch.first >= total) continue;
            const UINT count = std::min(batch.count, total - batch.first);
            if (count == 0) continue;

            const GpuMesh& mesh = m_meshes[static_cast<std::size_t>(batch.mesh)];
            if (mesh.vertexBuffer == nullptr) continue;

            ID3D11Buffer* buffers[2] = { mesh.vertexBuffer, m_instanceBuffer };
            const UINT strides[2] = { sizeof(MeshVertex), sizeof(MeshInstance) };
            const UINT offsets[2] = { 0, batch.first * static_cast<UINT>(sizeof(MeshInstance)) };
            device->IASetVertexBuffers(0, 2, buffers, strides, offsets);
            device->IASetIndexBuffer(mesh.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            device->DrawIndexedInstanced(mesh.indexCount, count, 0, 0, 0);
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
        m_shader = nullptr;             // owned by ShaderLibrary
        m_shadowShader = nullptr;       // owned by ShaderLibrary
        m_instShader = nullptr;         // owned by ShaderLibrary
        m_shadowInstShader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_rasterizer);
        SafeRelease(m_depthEnabled);
        SafeRelease(m_instanceBuffer);
        SafeRelease(m_objectConstants);
        SafeRelease(m_frameConstants);
    }
}

#endif  // ENGINE_WITH_3D
