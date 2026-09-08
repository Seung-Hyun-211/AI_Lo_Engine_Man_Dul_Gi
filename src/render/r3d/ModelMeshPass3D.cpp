#include "render/r3d/ModelMeshPass3D.h"

#if defined(ENGINE_WITH_3D)

// This pass depends on the import module (one direction, no cycle): it is the
// simplest way to get an FBX on screen for the current milestone. A neutral
// model-upload API on IRenderer replaces this when models become first-class.
#include "import/ModelImporter.h"
#include "import/TgaImage.h"
#include "math/Math3D.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct FrameConstants { float viewProj[16]; float lightDir[4]; };
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

    std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Last path component, extension replaced with ".tga".
    std::string BaseNameAsTga(const std::string& path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        return name + ".tga";
    }

    // Unity-chan demo asset: material name -> the .tga sitting next to the FBX,
    // for the materials whose FBX texture reference is missing or stale.
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
}

namespace engine::render
{
    ModelMeshPass3D::ModelMeshPass3D(std::string modelPath) : m_modelPath(std::move(modelPath)) {}

    ID3D11ShaderResourceView* ModelMeshPass3D::ResolveTexture(ID3D11Device* device,
                                                             const std::string& materialName,
                                                             const std::string& fbxRefPath)
    {
        std::string fileName;
        if (!fbxRefPath.empty()) fileName = BaseNameAsTga(fbxRefPath);
        if (fileName.empty() || fileName == ".tga")
        {
            const char* mapped = MaterialToTga(materialName);
            if (mapped == nullptr) return nullptr;
            fileName = mapped;
        }

        const std::string fullPath = m_resolvedDir + fileName;
        if (const auto it = m_textures.find(fullPath); it != m_textures.end()) return it->second;

        const import::TgaImage image = import::LoadTga(fullPath);
        ID3D11ShaderResourceView* srv = nullptr;
        if (image.ok)
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
        m_textures.emplace(fullPath, srv);   // cache successes and misses (nullptr)
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

        for (const import::ModelMesh& mesh : result.model.meshes)
        {
            if (mesh.vertices.empty() || mesh.indices.empty()) continue;

            SubMesh sub;
            sub.vertexStride = static_cast<std::uint32_t>(sizeof(import::ModelVertex));
            sub.indexCount = static_cast<std::uint32_t>(mesh.indices.size());

            std::string materialName;
            std::string textureRef;
            if (mesh.materialIndex >= 0 && mesh.materialIndex < static_cast<int>(result.model.materials.size()))
            {
                const import::ModelMaterial& mat = result.model.materials[static_cast<std::size_t>(mesh.materialIndex)];
                sub.color = mat.baseColor;
                materialName = mat.name;
                textureRef = mat.diffuseTexture;
            }
            else
            {
                sub.color = { 0.8f, 0.8f, 0.82f, 1.0f };
            }
            sub.texture = ResolveTexture(device, materialName, textureRef);

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

            m_subMeshes.push_back(sub);
        }

        int textured = 0;
        for (const SubMesh& s : m_subMeshes) if (s.texture != nullptr) ++textured;
        OutputDebugStringA(("ModelMeshPass3D: loaded '" + m_modelPath + "' ("
            + std::to_string(m_subMeshes.size()) + " submeshes, " + std::to_string(textured) + " textured)\n").c_str());
    }

    void ModelMeshPass3D::Initialize(ID3D11Device* device)
    {
        constexpr char shader[] = R"(
cbuffer Frame  : register(b0) { row_major float4x4 viewProj; float4 lightDir; };
cbuffer Object : register(b1) { row_major float4x4 world;    float4 objColor; };
Texture2D    albedo : register(t0);
SamplerState samp   : register(s0);
struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD; };
VSOut VSMain(VSIn input) {
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    output.uv = float2(input.uv.x, 1.0f - input.uv.y);   // FBX bottom-left -> D3D top-left
    return output;
}
float4 PSMain(VSOut input) : SV_TARGET {
    float4 tex = albedo.Sample(samp, input.uv);
    clip(tex.a - 0.35f);                                  // cutout for hair / eyelashes
    float3 n = normalize(input.nrm);
    float ndotl = saturate(dot(n, -normalize(lightDir.xyz)));
    float3 base = tex.rgb * objColor.rgb;
    return float4(base * (0.3f + 0.7f * ndotl), 1.0f);
}
)";
        ID3DBlob* vs{}; ID3DBlob* ps{}; ID3DBlob* errors{};
        ThrowIfFailed(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &errors), "ModelMeshPass3D vertex shader compile failed");
        SafeRelease(errors);
        ThrowIfFailed(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &errors), "ModelMeshPass3D pixel shader compile failed");
        SafeRelease(errors);
        ThrowIfFailed(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vertexShader), "CreateVertexShader failed");
        ThrowIfFailed(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_pixelShader), "CreatePixelShader failed");

        // ModelVertex: position(0) + normal(12) + uv(24) + bone data; stride 64.
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        const HRESULT layoutResult = device->CreateInputLayout(layout, ARRAYSIZE(layout), vs->GetBufferPointer(), vs->GetBufferSize(), &m_inputLayout);
        SafeRelease(vs); SafeRelease(ps);
        ThrowIfFailed(layoutResult, "CreateInputLayout failed");

        D3D11_BUFFER_DESC frameDesc{};
        frameDesc.ByteWidth = sizeof(FrameConstants);
        frameDesc.Usage = D3D11_USAGE_DEFAULT;
        frameDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&frameDesc, nullptr, &m_frameConstants), "CreateBuffer (model frame) failed");

        D3D11_BUFFER_DESC objectDesc{};
        objectDesc.ByteWidth = sizeof(ObjectConstants);
        objectDesc.Usage = D3D11_USAGE_DEFAULT;
        objectDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&objectDesc, nullptr, &m_objectConstants), "CreateBuffer (model object) failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthEnabled), "CreateDepthStencilState (model) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;   // winding unverified; see model-animation-research.md
        rasterDesc.DepthClipEnable = TRUE;
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState failed");

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &m_sampler), "CreateSamplerState failed");

        // 1x1 white fallback so the shader can always sample.
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
        if (m_subMeshes.empty()) return;
        const Scene3D& scene = context.snapshot->scene3d;
        if (scene.modelDraws.empty()) return;

        ID3D11DeviceContext* device = context.context;

        const math::Mat4 viewProj = scene.camera.view * scene.camera.projection;
        FrameConstants frame{};
        std::memcpy(frame.viewProj, viewProj.m, sizeof(frame.viewProj));
        frame.lightDir[0] = scene.camera.lightDirection.x;
        frame.lightDir[1] = scene.camera.lightDirection.y;
        frame.lightDir[2] = scene.camera.lightDirection.z;
        frame.lightDir[3] = 0.0f;
        device->UpdateSubresource(m_frameConstants, 0, nullptr, &frame, 0, 0);

        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->OMSetDepthStencilState(m_depthEnabled, 0);
        device->RSSetState(m_rasterizer);
        device->IASetInputLayout(m_inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(m_vertexShader, nullptr, 0);
        device->PSSetShader(m_pixelShader, nullptr, 0);
        device->VSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetConstantBuffers(0, 1, &m_frameConstants);
        device->PSSetSamplers(0, 1, &m_sampler);

        for (const ModelDraw& draw : scene.modelDraws)
        {
            for (const SubMesh& sub : m_subMeshes)
            {
                ObjectConstants object{};
                std::memcpy(object.world, draw.world.m, sizeof(object.world));
                object.color[0] = sub.color.r * draw.tint.r;
                object.color[1] = sub.color.g * draw.tint.g;
                object.color[2] = sub.color.b * draw.tint.b;
                object.color[3] = sub.color.a * draw.tint.a;
                device->UpdateSubresource(m_objectConstants, 0, nullptr, &object, 0, 0);
                device->VSSetConstantBuffers(1, 1, &m_objectConstants);
                device->PSSetConstantBuffers(1, 1, &m_objectConstants);

                ID3D11ShaderResourceView* srv = sub.texture != nullptr ? sub.texture : m_whiteTexture;
                device->PSSetShaderResources(0, 1, &srv);

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
            SafeRelease(sub.indexBuffer);
            sub.texture = nullptr;
        }
        m_subMeshes.clear();
        for (auto& entry : m_textures) SafeRelease(entry.second);
        m_textures.clear();
        SafeRelease(m_whiteTexture);
        SafeRelease(m_sampler);
        SafeRelease(m_rasterizer);
        SafeRelease(m_depthEnabled);
        SafeRelease(m_objectConstants);
        SafeRelease(m_frameConstants);
        SafeRelease(m_inputLayout);
        SafeRelease(m_pixelShader);
        SafeRelease(m_vertexShader);
    }
}

#endif  // ENGINE_WITH_3D
