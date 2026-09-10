#include "render/r3d/MeshPass3D.h"

#if defined(ENGINE_WITH_3D)

#include "anim/AnimationSampler.h"
#include "import/ImageData.h"
#include "import/ImageFile.h"
#include "import/ModelImporter.h"
#include "math/Math3D.h"
#include "render/r3d/FrameConstants.h"
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine::render
{
    // Position + normal + uv. Matches the input layouts and the HLSL VSIn
    // below (stride 32). The pos-only shadow layouts read POSITION at offset 0
    // and ignore the rest; mesh.hlsl reads POSITION + NORMAL and ignores uv.
    struct MeshVertex { float px, py, pz, nx, ny, nz, u, v; };

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

    // The FBX drawn as the instanced crowd (MeshId::CrowdModel). Set to nullptr
    // to skip the load entirely (e.g. when game/CrowdConfig.h uses CrowdMesh::Cube
    // and you want to avoid the startup cost). Keep the metre height / pivot in
    // the matching game::CrowdConfig preset in sync when swapping this file.
    constexpr const char* kCrowdModelFbx = "assets/models/zombie/Zombie1.FBX";
    // Diffuse for the crowd model (t0 in mesh_instanced.hlsl). Empty / missing
    // => the 1x1 white fallback (flat-lit, tinted by the per-instance colour).
    constexpr const char* kCrowdDiffuseTex = "assets/models/zombie/Zombie.tga";
    // Animation clip baked into the crowd VAT (t2). Empty / missing => static
    // bind pose (per-instance animTime ignored). One clip for now.
    constexpr const char* kCrowdClipFbx = "assets/models/zombie/Zombie@Z_Run.FBX";
    constexpr float kVatFps = 24.0f;   // VAT bake / playback sample rate

    // Weighted sum of up to 4 bone (skin) matrices. Mirrors the helper in
    // ModelMeshPass3D.cpp - used to CPU-skin each VAT frame at load.
    engine::math::Mat4 BlendBoneMatrices(const std::uint32_t (&indices)[engine::import::kMaxBoneInfluences],
                                          const float (&weights)[engine::import::kMaxBoneInfluences],
                                          const std::vector<engine::math::Mat4>& bones)
    {
        engine::math::Mat4 blend{};
        for (float& c : blend.m) c = 0.0f;
        for (int i = 0; i < engine::import::kMaxBoneInfluences; ++i)
        {
            const float w = weights[i];
            if (w <= 0.0f) continue;
            const std::uint32_t b = indices[i];
            if (b >= bones.size()) continue;
            for (int c = 0; c < 16; ++c) blend.m[c] += bones[b].m[c] * w;
        }
        return blend;
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

    // Adds one quad face (two triangles) spanning center +- u/2 +- v/2, all four
    // vertices sharing the face normal. Winding is CCW seen from outside; each
    // face gets a full 0..1 planar uv.
    void AddFace(MeshData& mesh, Vec3 center, Vec3 u, Vec3 v, Vec3 normal)
    {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const Vec3 corners[4]{
            center - u * 0.5f - v * 0.5f,
            center + u * 0.5f - v * 0.5f,
            center + u * 0.5f + v * 0.5f,
            center - u * 0.5f + v * 0.5f,
        };
        const float uvs[4][2]{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int i = 0; i < 4; ++i)
            mesh.vertices.push_back({ corners[i].x, corners[i].y, corners[i].z,
                                     normal.x, normal.y, normal.z, uvs[i][0], uvs[i][1] });
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
        static_assert(sizeof(MeshInstance) == 28,
            "instanced layout assumes {pos@0, yaw@12, scale@16, colorRgba@20, animTime@24}");
        const D3D11_INPUT_ELEMENT_DESC instanced[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA,   0 },   // mesh uv
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // pos
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // yaw
            { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // scale
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // colorRgba
            { "TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,       1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },   // animTime
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

        D3D11_BUFFER_DESC vatInfoDesc{};
        vatInfoDesc.ByteWidth = 16;   // float4
        vatInfoDesc.Usage = D3D11_USAGE_DEFAULT;
        vatInfoDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&vatInfoDesc, nullptr, &m_vatInfo), "CreateBuffer (vat info) failed");

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

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &m_sampler), "CreateSamplerState (mesh) failed");

        const std::uint32_t white = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC whiteDesc{};
        whiteDesc.Width = 1; whiteDesc.Height = 1; whiteDesc.MipLevels = 1; whiteDesc.ArraySize = 1;
        whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        whiteDesc.SampleDesc.Count = 1;
        whiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
        whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA whiteInit{ &white, sizeof(white), 0 };
        ID3D11Texture2D* whiteTex = nullptr;
        ThrowIfFailed(device->CreateTexture2D(&whiteDesc, &whiteInit, &whiteTex), "CreateTexture2D (mesh white) failed");
        ThrowIfFailed(device->CreateShaderResourceView(whiteTex, nullptr, &m_whiteSrv), "CreateSRV (mesh white) failed");
        whiteTex->Release();

        CreateMesh(device, MeshId::Cube, MakeCube());
        CreateMesh(device, MeshId::Plane, MakePlane());
        LoadCrowdMesh(device);
    }

    void MeshPass3D::LoadCrowdMesh(ID3D11Device* device)
    {
        if (kCrowdModelFbx == nullptr || kCrowdModelFbx[0] == '\0') return;   // CrowdMesh::Cube demo

        import::ImportOptions opt;
        opt.skipAnimation = true;   // Zombie1.FBX carries mesh + skeleton only; the clip is a separate file
        opt.scale = 1.0f;           // height is normalised below, so source units do not matter

        std::string resolvedPrefix;
        import::ImportResult res;
        for (const char* prefix : { "", "../../", "../../../" })
        {
            res = import::LoadModelFromFile(std::string(prefix) + kCrowdModelFbx, opt);
            if (res.ok) { resolvedPrefix = prefix; break; }
        }

        MeshData data;
        std::vector<import::ModelVertex> bakeSrc;   // parallel to data.vertices, for the VAT skinning below
        if (res.ok)
        {
            for (const import::ModelMesh& m : res.model.meshes)
            {
                const auto vbase = static_cast<std::uint32_t>(data.vertices.size());
                for (const import::ModelVertex& v : m.vertices)
                {
                    data.vertices.push_back({ v.position.x, v.position.y, v.position.z,
                                              v.normal.x, v.normal.y, v.normal.z,
                                              v.uv.x, v.uv.y });
                    bakeSrc.push_back(v);
                }
                for (const std::uint32_t idx : m.indices)
                    data.indices.push_back(vbase + idx);
            }
        }

        if (data.vertices.empty() || data.indices.empty())
        {
            OutputDebugStringA((std::string("MeshPass3D: crowd mesh '") + kCrowdModelFbx
                + "' failed to load ('" + res.error + "') - falling back to the cube\n").c_str());
            CreateMesh(device, MeshId::CrowdModel, MakeCube());
            return;
        }

        const auto bbox = [&](float& x0, float& y0, float& z0, float& x1, float& y1, float& z1)
        {
            x0 = y0 = z0 = 1e30f;  x1 = y1 = z1 = -1e30f;
            for (const MeshVertex& v : data.vertices)
            {
                x0 = std::min(x0, v.px); x1 = std::max(x1, v.px);
                y0 = std::min(y0, v.py); y1 = std::max(y1, v.py);
                z0 = std::min(z0, v.pz); z1 = std::max(z1, v.pz);
            }
        };

        float minX, minY, minZ, maxX, maxY, maxZ;
        bbox(minX, minY, minZ, maxX, maxY, maxZ);

        // Many 3ds Max exports (this one included) come through Z-up: feet near
        // z = 0, head near z = max, and the Z span is the real height. ufbx's
        // axis target did not reorient it. Rotate Z-up -> Y-up in place:
        // (x, y, z) -> (x, z, -y). A proper rotation (det +1, no mirror, so
        // lighting stays correct); this rig's front is -Y, so this leaves the
        // model facing +Z (engine forward = yaw 0). Flip to (-x, z, y) if it
        // moonwalks. A Y-up model skips this branch untouched.
        const bool zUp = (maxZ - minZ) > (maxY - minY) && minZ > -2.0f;
        if (zUp)
        {
            for (MeshVertex& v : data.vertices)
            {
                const float py = v.py, pz = v.pz, ny = v.ny, nz = v.nz;
                v.py = pz;   v.pz = -py;
                v.ny = nz;   v.nz = -ny;
            }
            bbox(minX, minY, minZ, maxX, maxY, maxZ);
        }

        // Normalise: feet at y = 0, centred on x/z, total height 1. The
        // SnapshotBuilder scales each instance to the metre height it wants.
        // `zUpRotate` + `normalise` together are the full model->render transform;
        // the VAT bake below applies both to raw skinned positions so animated
        // and bind-pose frames share a space. (data.vertices is already Z-up
        // rotated above, so its loop only runs `normalise`.)
        const float s = 1.0f / std::max(maxY - minY, 1e-4f);
        const math::Vec3 offset{ (minX + maxX) * 0.5f, minY, (minZ + maxZ) * 0.5f };
        const auto zUpRotate = [zUp](math::Vec3 p) { return zUp ? math::Vec3{ p.x, p.z, -p.y } : p; };
        const auto normalise = [s, offset](math::Vec3 p)
        {
            return math::Vec3{ (p.x - offset.x) * s, (p.y - offset.y) * s, (p.z - offset.z) * s };
        };
        for (MeshVertex& v : data.vertices)
        {
            const math::Vec3 p = normalise({ v.px, v.py, v.pz });
            v.px = p.x; v.py = p.y; v.pz = p.z;
        }

        CreateMesh(device, MeshId::CrowdModel, data);
        OutputDebugStringA((std::string("MeshPass3D: crowd mesh '") + kCrowdModelFbx + "' loaded ("
            + std::to_string(data.vertices.size()) + " verts, "
            + std::to_string(data.indices.size() / 3) + " tris, zUp=" + (zUp ? "1" : "0") + ")\n").c_str());

        // Crowd diffuse (t0). sRGB SRV so the sample is linearised before
        // lighting (docs/image-assets.md §3; matches ModelMeshPass3D). Missing
        // file -> stay on the white fallback.
        if (kCrowdDiffuseTex != nullptr && kCrowdDiffuseTex[0] != '\0')
        {
            import::ImageData img;
            for (const char* prefix : { "", "../../", "../../../" })
            {
                img = import::LoadImageFromFile(std::string(prefix) + kCrowdDiffuseTex);
                if (img.ok) break;
            }
            if (img.ok && !img.rgba.empty())
            {
                D3D11_TEXTURE2D_DESC td{};
                td.Width = static_cast<UINT>(img.width);
                td.Height = static_cast<UINT>(img.height);
                td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_IMMUTABLE;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{ img.rgba.data(), static_cast<UINT>(img.width) * 4, 0 };
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(device->CreateTexture2D(&td, &init, &tex)))
                {
                    device->CreateShaderResourceView(tex, nullptr, &m_crowdDiffuseSrv);
                    tex->Release();
                    OutputDebugStringA((std::string("MeshPass3D: crowd diffuse '") + kCrowdDiffuseTex
                        + "' " + std::to_string(img.width) + "x" + std::to_string(img.height) + "\n").c_str());
                }
            }
            else
            {
                OutputDebugStringA((std::string("MeshPass3D: crowd diffuse '") + kCrowdDiffuseTex
                    + "' not found - using white\n").c_str());
            }
        }

        // --- Bake one animation clip into a VAT (t2). CPU-skin every vertex at
        // kVatFps frames, apply the same model->render transform, store the
        // final local position per (vertex column, frame row). The VS then just
        // Loads a row instead of skinning. Missing clip / skeleton => static
        // bind pose. docs/instanced-rendering.md §9.6-B, docs/horde-design.md §5.
        if (kCrowdClipFbx != nullptr && kCrowdClipFbx[0] != '\0' && !res.model.skeleton.Empty())
        {
            import::AnimationImportResult clipRes;
            for (const char* p : { resolvedPrefix.c_str(), "", "../../", "../../../" })
            {
                clipRes = import::LoadAnimationClipsFromFile(std::string(p) + kCrowdClipFbx,
                                                            res.model.skeleton, kVatFps, opt.scale);
                if (clipRes.ok) break;
            }

            if (clipRes.ok && !clipRes.clips.empty() && !clipRes.clips.front().tracks.empty())
            {
                const import::AnimationClip& clip = clipRes.clips.front();
                const int frames = std::max(1, static_cast<int>(std::ceil(clip.duration * kVatFps)));
                const int vcount = static_cast<int>(bakeSrc.size());

                anim::AnimationSampler sampler;
                std::vector<math::Mat4> palette;
                std::vector<float> pix(static_cast<std::size_t>(vcount) * frames * 4, 0.0f);

                for (int f = 0; f < frames; ++f)
                {
                    sampler.Evaluate(res.model.skeleton, clip,
                                     static_cast<float>(f) / kVatFps, palette, anim::PlayMode::Loop);
                    float* row = pix.data() + static_cast<std::size_t>(f) * vcount * 4;
                    for (int i = 0; i < vcount; ++i)
                    {
                        const import::ModelVertex& bv = bakeSrc[static_cast<std::size_t>(i)];
                        const math::Mat4 skin = BlendBoneMatrices(bv.boneIndices, bv.boneWeights, palette);
                        const math::Vec3 wp = normalise(zUpRotate(math::TransformPoint(bv.position, skin)));
                        row[i * 4 + 0] = wp.x; row[i * 4 + 1] = wp.y; row[i * 4 + 2] = wp.z;
                    }
                }

                D3D11_TEXTURE2D_DESC vd{};
                vd.Width = static_cast<UINT>(vcount);
                vd.Height = static_cast<UINT>(frames);
                vd.MipLevels = 1; vd.ArraySize = 1;
                vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                vd.SampleDesc.Count = 1;
                vd.Usage = D3D11_USAGE_IMMUTABLE;
                vd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA vinit{ pix.data(), static_cast<UINT>(vcount) * 16, 0 };
                ID3D11Texture2D* vtex = nullptr;
                if (SUCCEEDED(device->CreateTexture2D(&vd, &vinit, &vtex)))
                {
                    device->CreateShaderResourceView(vtex, nullptr, &m_vatSrv);
                    vtex->Release();
                    m_vatSampleRate = kVatFps;
                    m_vatFrameCount = static_cast<float>(frames);
                    OutputDebugStringA((std::string("MeshPass3D: crowd VAT '") + clip.name + "' "
                        + std::to_string(vcount) + " verts x " + std::to_string(frames) + " frames\n").c_str());
                }
            }
            else
            {
                OutputDebugStringA((std::string("MeshPass3D: crowd clip '") + kCrowdClipFbx
                    + "' not loaded ('" + clipRes.error + "') - crowd stays in bind pose\n").c_str());
            }
        }
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
        if (!shadow)
        {
            device->PSSetShader(program->ps, nullptr, 0);
            ID3D11ShaderResourceView* srv = m_crowdDiffuseSrv != nullptr ? m_crowdDiffuseSrv : m_whiteSrv;
            device->PSSetShaderResources(0, 1, &srv);
            device->PSSetSamplers(0, 1, &m_sampler);
            device->VSSetConstantBuffers(2, 1, &m_vatInfo);
            device->VSSetShaderResources(2, 1, &m_vatSrv);   // used only by the CrowdModel batch below
        }

        MeshId vatArmedFor = MeshId::Count;   // avoid redundant cbuffer writes across batches
        for (const InstanceBatch& batch : scene.instanceBatches)
        {
            if (shadow && batch.lod >= 2) continue;                 // far crowd casts no shadow
            if (batch.first >= total) continue;
            const UINT count = std::min(batch.count, total - batch.first);
            if (count == 0) continue;

            const GpuMesh& mesh = m_meshes[static_cast<std::size_t>(batch.mesh)];
            if (mesh.vertexBuffer == nullptr) continue;

            // VAT applies only to the CrowdModel mesh; other meshes (cube) must
            // draw their bind pose (frameCount 0). One cbuffer write per switch.
            if (!shadow && batch.mesh != vatArmedFor)
            {
                const bool useVat = batch.mesh == MeshId::CrowdModel && m_vatSrv != nullptr;
                const float vatParams[4] = { m_vatSampleRate, useVat ? m_vatFrameCount : 0.0f, 0.0f, 0.0f };
                device->UpdateSubresource(m_vatInfo, 0, nullptr, vatParams, 0, 0);
                vatArmedFor = batch.mesh;
            }

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
        SafeRelease(m_vatSrv);
        SafeRelease(m_vatInfo);
        SafeRelease(m_crowdDiffuseSrv);
        SafeRelease(m_whiteSrv);
        SafeRelease(m_sampler);
        SafeRelease(m_rasterizer);
        SafeRelease(m_depthEnabled);
        SafeRelease(m_instanceBuffer);
        SafeRelease(m_objectConstants);
        SafeRelease(m_frameConstants);
    }
}

#endif  // ENGINE_WITH_3D
