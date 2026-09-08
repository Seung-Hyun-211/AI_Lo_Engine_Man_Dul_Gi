#include "render/r3d/ModelMeshPass3D.h"

#if defined(ENGINE_WITH_3D)

// This pass depends on the import module (one direction, no cycle): it is the
// simplest way to get an FBX on screen for the current milestone. A neutral
// model-upload API on IRenderer replaces this when models become first-class.
#include "import/CreaseLines.h"
#include "import/ModelImporter.h"
#include "import/TgaImage.h"
#include "math/Math3D.h"
#include "render/r3d/FrameConstants.h"
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    struct ObjectConstants { float world[16]; float color[4]; };
    struct OutlineConstants { float width; float pad[3]; };
    struct CelParamsGpu { float shadowBias; float pad[3]; };   // b3, per-material
    struct CreaseVertexGpu { float px, py, pz, r, g, b, a; };

    // Silhouette thickness as a fraction of half-screen (see outline.hlsl).
    constexpr float kOutlineWidth = 0.002f;

    void ThrowIfFailed(HRESULT result, const char* message)
    {
        if (FAILED(result)) throw std::runtime_error(message);
    }

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr) { object->Release(); object = nullptr; }
    }

    std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string BaseNameAsTga(const std::string& path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        return name + ".tga";
    }

    // Unity-chan demo asset: material name -> the .tga next to the FBX, for the
    // materials whose FBX texture reference is missing or stale.
    const char* MaterialToTga(const std::string& materialName)
    {
        const std::string n = ToLower(materialName);
        if (n == "body")     return "body_01.tga";
        if (n == "hair")     return "hair_01.tga";
        if (n == "skin1")    return "skin_01.tga";
        if (n == "face")     return "face_00.tga";
        if (n == "eyeline")  return "eyeline_00.tga";
        if (n == "mat_cheek" || n == "cheek") return "cheek_00.tga";
        if (n == "eye_l1")   return "eye_iris_L_00.tga";
        if (n == "eye_r1")   return "eye_iris_R_00.tga";
        return nullptr;
    }

    std::string ResolveTextureFileName(const std::string& materialName, const std::string& fbxRef)
    {
        if (!fbxRef.empty())
        {
            const std::string base = BaseNameAsTga(fbxRef);
            if (base != ".tga") return base;
        }
        const char* mapped = MaterialToTga(materialName);
        return mapped != nullptr ? std::string(mapped) : std::string{};
    }

    // Face / skin materials keep lit through a wider angle so the self-shadow
    // terminator does not carve up the face (docs/toon-rendering.md).
    float MaterialShadowBias(const std::string& materialName)
    {
        const std::string n = ToLower(materialName);
        if (n.rfind("face", 0) == 0 || n == "eyebase" || n == "eyeline"
            || n == "eye_l1" || n == "eye_r1" || n == "mat_cheek" || n == "cheek" || n == "skin1")
            return 14.0f;
        return 0.0f;
    }
}

namespace engine::render
{
    ModelMeshPass3D::ModelMeshPass3D(std::string modelPath) : m_modelPath(std::move(modelPath)) {}

    ID3D11ShaderResourceView* ModelMeshPass3D::CreateTextureSrv(ID3D11Device* device, const std::string& fileName,
                                                               const import::TgaImage& image)
    {
        if (const auto it = m_textures.find(fileName); it != m_textures.end()) return it->second;

        ID3D11ShaderResourceView* srv = nullptr;
        if (image.ok && !image.rgba.empty())
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(image.width);
            desc.Height = static_cast<UINT>(image.height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{};
            init.pSysMem = image.rgba.data();
            init.SysMemPitch = static_cast<UINT>(image.width) * 4;

            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &tex)))
            {
                device->CreateShaderResourceView(tex, nullptr, &srv);
                tex->Release();
            }
        }
        m_textures.emplace(fileName, srv);
        return srv;
    }

    void ModelMeshPass3D::LoadModel(ID3D11Device* device)
    {
        import::ImportOptions options;
        options.scale = 0.01f;          // Unity-chan (and many FBX) are in centimetres
        options.skipAnimation = true;   // static draw for now

        const std::string dir = m_modelPath.substr(0, m_modelPath.find_last_of("/\\") + 1);

        import::ImportResult result;
        for (const std::string& prefix : { std::string{}, std::string{ "../../" }, std::string{ "../../../" } })
        {
            result = import::LoadModelFromFile(prefix + m_modelPath, options);
            if (result.ok) { m_resolvedDir = prefix + dir; break; }
        }
        if (!result.ok)
        {
            OutputDebugStringA(("ModelMeshPass3D: could not load '" + m_modelPath + "': " + result.error + "\n").c_str());
            return;
        }

        // Decoded textures, kept only for the duration of the load (crease line
        // colours sample them at face centroids).
        std::unordered_map<std::string, import::TgaImage> tgaCache;
        auto getTga = [&](const std::string& fileName) -> const import::TgaImage*
        {
            if (fileName.empty()) return nullptr;
            const auto it = tgaCache.find(fileName);
            if (it != tgaCache.end()) return &it->second;
            import::TgaImage image = import::LoadTga(m_resolvedDir + fileName);
            return &tgaCache.emplace(fileName, std::move(image)).first->second;
        };

        std::vector<CreaseVertexGpu> creaseVerts;

        for (const import::ModelMesh& mesh : result.model.meshes)
        {
            if (mesh.vertices.empty() || mesh.indices.empty()) continue;

            SubMesh sub;
            sub.vertexStride = static_cast<std::uint32_t>(sizeof(import::ModelVertex));
            sub.indexCount = static_cast<std::uint32_t>(mesh.indices.size());

            const import::ModelMaterial* material = nullptr;
            std::string fileName;
            if (mesh.materialIndex >= 0 && mesh.materialIndex < static_cast<int>(result.model.materials.size()))
            {
                material = &result.model.materials[static_cast<std::size_t>(mesh.materialIndex)];
                sub.color = material->baseColor;
                sub.shadowBias = MaterialShadowBias(material->name);
                fileName = ResolveTextureFileName(material->name, material->diffuseTexture);
            }
            else
            {
                sub.color = { 0.8f, 0.8f, 0.82f, 1.0f };
            }

            const import::TgaImage* tga = getTga(fileName);
            sub.texture = tga != nullptr ? CreateTextureSrv(device, fileName, *tga) : nullptr;

            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.ByteWidth = static_cast<UINT>(sizeof(import::ModelVertex) * mesh.vertices.size());
            vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vertexInit{ mesh.vertices.data(), 0, 0 };
            ThrowIfFailed(device->CreateBuffer(&vertexDesc, &vertexInit, &sub.vertexBuffer), "CreateBuffer (model vertex) failed");

            D3D11_BUFFER_DESC indexDesc{};
            indexDesc.ByteWidth = static_cast<UINT>(sizeof(std::uint32_t) * mesh.indices.size());
            indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA indexInit{ mesh.indices.data(), 0, 0 };
            ThrowIfFailed(device->CreateBuffer(&indexDesc, &indexInit, &sub.indexBuffer), "CreateBuffer (model index) failed");

            // Hull geometry for the silhouette outline: same order/count as the
            // model vertices (so the index buffer is shared) but position +
            // *smoothed* normal, so the ring does not split at hard-normal seams.
            const std::vector<math::Vec3> smoothNormals = import::BuildSmoothNormals(mesh);
            std::vector<float> hull;
            hull.reserve(mesh.vertices.size() * 6);
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
            {
                const math::Vec3& p = mesh.vertices[i].position;
                const math::Vec3& n = smoothNormals[i];
                hull.insert(hull.end(), { p.x, p.y, p.z, n.x, n.y, n.z });
            }
            D3D11_BUFFER_DESC hullDesc{};
            hullDesc.ByteWidth = static_cast<UINT>(sizeof(float) * hull.size());
            hullDesc.Usage = D3D11_USAGE_IMMUTABLE;
            hullDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA hullInit{ hull.data(), 0, 0 };
            ThrowIfFailed(device->CreateBuffer(&hullDesc, &hullInit, &sub.hullVertexBuffer), "CreateBuffer (hull) failed");

            // Interior crease lines for this submesh.
            const import::TgaImage* creaseTex = tga != nullptr && tga->ok ? tga : nullptr;
            const std::vector<import::CreaseVertex> creases = import::BuildCreaseLines(mesh, creaseTex, material, {});
            for (const import::CreaseVertex& v : creases)
                creaseVerts.push_back({ v.position.x, v.position.y, v.position.z,
                                        v.color.r, v.color.g, v.color.b, v.color.a });

            m_subMeshes.push_back(sub);
        }

        if (!creaseVerts.empty())
        {
            D3D11_BUFFER_DESC creaseDesc{};
            creaseDesc.ByteWidth = static_cast<UINT>(sizeof(CreaseVertexGpu) * creaseVerts.size());
            creaseDesc.Usage = D3D11_USAGE_IMMUTABLE;
            creaseDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA creaseInit{ creaseVerts.data(), 0, 0 };
            ThrowIfFailed(device->CreateBuffer(&creaseDesc, &creaseInit, &m_creaseVertexBuffer), "CreateBuffer (crease) failed");
            m_creaseVertexCount = static_cast<std::uint32_t>(creaseVerts.size());
        }

        int textured = 0;
        for (const SubMesh& s : m_subMeshes) if (s.texture != nullptr) ++textured;
        OutputDebugStringA(("ModelMeshPass3D: loaded '" + m_modelPath + "' ("
            + std::to_string(m_subMeshes.size()) + " submeshes, " + std::to_string(textured) + " textured, "
            + std::to_string(m_creaseVertexCount / 6) + " crease segments)\n").c_str());
    }

    void ModelMeshPass3D::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        // ModelVertex: position(0) + normal(12) + uv(24) + bone data; stride 64.
        const D3D11_INPUT_ELEMENT_DESC modelLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        const D3D11_INPUT_ELEMENT_DESC outlineLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        const D3D11_INPUT_ELEMENT_DESC creaseLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        // "cel" = toon shading; swap for "model" to get plain Lambert.
        m_shader = shaders.Get(device, "cel", modelLayout, ARRAYSIZE(modelLayout));
        m_outlineShader = shaders.Get(device, "outline", outlineLayout, ARRAYSIZE(outlineLayout));
        m_creaseShader = shaders.Get(device, "crease", creaseLayout, ARRAYSIZE(creaseLayout));
        m_shadowShader = shaders.Get(device, "shadow", outlineLayout, 1);   // POSITION only

        auto makeConstantBuffer = [device](UINT bytes, ID3D11Buffer** buffer, const char* what)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = bytes;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            ThrowIfFailed(device->CreateBuffer(&desc, nullptr, buffer), what);
        };
        makeConstantBuffer(sizeof(FrameConstantsGpu), &m_frameConstants, "CreateBuffer (model frame) failed");
        makeConstantBuffer(sizeof(ObjectConstants), &m_objectConstants, "CreateBuffer (model object) failed");
        makeConstantBuffer(sizeof(OutlineConstants), &m_outlineConstants, "CreateBuffer (outline) failed");
        makeConstantBuffer(sizeof(CelParamsGpu), &m_celConstants, "CreateBuffer (cel params) failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthEnabled), "CreateDepthStencilState (model) failed");

        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthReadLessEqual), "CreateDepthStencilState (crease) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;   // winding unverified; see model-animation-research.md
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = TRUE;     // MSAA coverage (scene target is multisampled)
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState failed");

        rasterDesc.CullMode = D3D11_CULL_FRONT;  // inverted hull: show the back of the inflated shell
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_outlineRasterizer), "CreateRasterizerState (outline) failed");

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &m_sampler), "CreateSamplerState failed");

        const std::uint32_t white = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC whiteDesc{};
        whiteDesc.Width = 1; whiteDesc.Height = 1; whiteDesc.MipLevels = 1; whiteDesc.ArraySize = 1;
        whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        whiteDesc.SampleDesc.Count = 1;
        whiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
        whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA whiteInit{ &white, sizeof(white), 0 };
        ID3D11Texture2D* whiteTex = nullptr;
        ThrowIfFailed(device->CreateTexture2D(&whiteDesc, &whiteInit, &whiteTex), "CreateTexture2D (white) failed");
        const HRESULT whiteSrv = device->CreateShaderResourceView(whiteTex, nullptr, &m_whiteTexture);
        whiteTex->Release();
        ThrowIfFailed(whiteSrv, "CreateShaderResourceView (white) failed");

        LoadModel(device);
    }

    void ModelMeshPass3D::Execute(const PassContext& context)
    {
        if (m_subMeshes.empty() || m_shader == nullptr) return;
        const Scene3D& scene = context.snapshot->scene3d;
        if (scene.modelDraws.empty()) return;

        ID3D11DeviceContext* device = context.context;

        FrameConstantsGpu frame{};
        FillFrameConstants(scene.camera, scene.lighting, frame);
        device->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetSamplers(0, 1, &m_sampler);

        const OutlineConstants outline{ kOutlineWidth, { 0.0f, 0.0f, 0.0f } };
        device->UpdateSubresource(m_outlineConstants, 0, nullptr, &outline, 0, 0);

        for (const ModelDraw& draw : scene.modelDraws)
        {
            ObjectConstants object{};
            std::memcpy(object.world, draw.world.m, sizeof(object.world));
            object.color[0] = draw.tint.r; object.color[1] = draw.tint.g;
            object.color[2] = draw.tint.b; object.color[3] = draw.tint.a;
            device->UpdateSubresource(m_objectConstants, 0, nullptr, &object, 0, 0);
            device->VSSetConstantBuffers(1, 1, &m_objectConstants);
            device->PSSetConstantBuffers(1, 1, &m_objectConstants);

            // --- 1) silhouette outline (inverted hull) ---
            device->RSSetState(m_outlineRasterizer);
            device->OMSetDepthStencilState(m_depthEnabled, 0);
            device->IASetInputLayout(m_outlineShader->inputLayout);
            device->VSSetShader(m_outlineShader->vs, nullptr, 0);
            device->PSSetShader(m_outlineShader->ps, nullptr, 0);
            device->VSSetConstantBuffers(2, 1, &m_outlineConstants);
            for (const SubMesh& sub : m_subMeshes)
            {
                if (sub.hullVertexBuffer == nullptr) continue;
                const UINT stride = 6 * sizeof(float), offset = 0;   // pos + smoothed normal
                device->IASetVertexBuffers(0, 1, &sub.hullVertexBuffer, &stride, &offset);
                device->IASetIndexBuffer(sub.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
                device->DrawIndexed(sub.indexCount, 0, 0);
            }

            // --- 2) cel-shaded surface ---
            device->RSSetState(m_rasterizer);
            device->OMSetDepthStencilState(m_depthEnabled, 0);
            device->IASetInputLayout(m_shader->inputLayout);
            device->VSSetShader(m_shader->vs, nullptr, 0);
            device->PSSetShader(m_shader->ps, nullptr, 0);
            for (const SubMesh& sub : m_subMeshes)
            {
                ObjectConstants perSub{};
                std::memcpy(perSub.world, draw.world.m, sizeof(perSub.world));
                perSub.color[0] = sub.color.r * draw.tint.r;
                perSub.color[1] = sub.color.g * draw.tint.g;
                perSub.color[2] = sub.color.b * draw.tint.b;
                perSub.color[3] = sub.color.a * draw.tint.a;
                device->UpdateSubresource(m_objectConstants, 0, nullptr, &perSub, 0, 0);

                const CelParamsGpu cel{ sub.shadowBias, { 0.0f, 0.0f, 0.0f } };
                device->UpdateSubresource(m_celConstants, 0, nullptr, &cel, 0, 0);
                device->PSSetConstantBuffers(3, 1, &m_celConstants);

                ID3D11ShaderResourceView* srv = sub.texture != nullptr ? sub.texture : m_whiteTexture;
                device->PSSetShaderResources(0, 1, &srv);

                const UINT stride = sub.vertexStride, offset = 0;
                device->IASetVertexBuffers(0, 1, &sub.vertexBuffer, &stride, &offset);
                device->IASetIndexBuffer(sub.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
                device->DrawIndexed(sub.indexCount, 0, 0);
            }

            // --- 3) interior crease lines ---
            if (m_creaseVertexCount > 0 && m_creaseShader != nullptr)
            {
                // Restore the object cbuffer to this draw's world (the cel loop
                // above left the last submesh's tint in it; colour is unused by
                // the crease shader, but the world matrix matters).
                device->UpdateSubresource(m_objectConstants, 0, nullptr, &object, 0, 0);

                device->RSSetState(m_rasterizer);
                device->OMSetDepthStencilState(m_depthReadLessEqual, 0);
                device->IASetInputLayout(m_creaseShader->inputLayout);
                device->VSSetShader(m_creaseShader->vs, nullptr, 0);
                device->PSSetShader(m_creaseShader->ps, nullptr, 0);

                const UINT stride = static_cast<UINT>(sizeof(CreaseVertexGpu)), offset = 0;
                device->IASetVertexBuffers(0, 1, &m_creaseVertexBuffer, &stride, &offset);
                device->Draw(m_creaseVertexCount, 0);
            }
        }
    }

    void ModelMeshPass3D::RenderShadow(const ShadowContext& context)
    {
        if (m_subMeshes.empty() || m_shadowShader == nullptr) return;
        const Scene3D& scene = context.snapshot->scene3d;
        if (scene.modelDraws.empty()) return;

        ID3D11DeviceContext* device = context.context;
        device->IASetInputLayout(m_shadowShader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(m_shadowShader->vs, nullptr, 0);
        device->PSSetShader(nullptr, nullptr, 0);

        for (const ModelDraw& draw : scene.modelDraws)
        {
            ObjectConstants object{};
            std::memcpy(object.world, draw.world.m, sizeof(object.world));
            device->UpdateSubresource(m_objectConstants, 0, nullptr, &object, 0, 0);
            device->VSSetConstantBuffers(1, 1, &m_objectConstants);

            for (const SubMesh& sub : m_subMeshes)
            {
                const UINT stride = sub.vertexStride, offset = 0;
                device->IASetVertexBuffers(0, 1, &sub.vertexBuffer, &stride, &offset);
                device->IASetIndexBuffer(sub.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
                device->DrawIndexed(sub.indexCount, 0, 0);
            }
        }
    }

    void ModelMeshPass3D::Release()
    {
        for (SubMesh& sub : m_subMeshes)
        {
            SafeRelease(sub.vertexBuffer);
            SafeRelease(sub.hullVertexBuffer);
            SafeRelease(sub.indexBuffer);
            sub.texture = nullptr;
        }
        m_subMeshes.clear();
        for (auto& entry : m_textures) SafeRelease(entry.second);
        m_textures.clear();

        m_shader = nullptr;
        m_outlineShader = nullptr;
        m_creaseShader = nullptr;
        m_shadowShader = nullptr;
        m_creaseVertexCount = 0;

        SafeRelease(m_creaseVertexBuffer);
        SafeRelease(m_whiteTexture);
        SafeRelease(m_sampler);
        SafeRelease(m_outlineRasterizer);
        SafeRelease(m_rasterizer);
        SafeRelease(m_depthReadLessEqual);
        SafeRelease(m_depthEnabled);
        SafeRelease(m_celConstants);
        SafeRelease(m_outlineConstants);
        SafeRelease(m_objectConstants);
        SafeRelease(m_frameConstants);
    }
}

#endif  // ENGINE_WITH_3D
