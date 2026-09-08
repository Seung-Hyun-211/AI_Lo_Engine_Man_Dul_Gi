#include "render/r3d/ModelMeshPass3D.h"

#if defined(ENGINE_WITH_3D)

// This pass depends on the import module (one direction, no cycle): it is the
// simplest way to get an FBX on screen for the current milestone. A neutral
// model-upload API on IRenderer replaces this when models become first-class.
#include "import/ModelImporter.h"
#include "math/Math3D.h"

#include <d3d11.h>
#include <d3dcompiler.h>

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
}

namespace engine::render
{
    ModelMeshPass3D::ModelMeshPass3D(std::string modelPath) : m_modelPath(std::move(modelPath)) {}

    void ModelMeshPass3D::LoadModel(ID3D11Device* device)
    {
        import::ImportOptions options;
        options.scale = 0.01f;          // Unity-chan (and many FBX) are in centimetres
        options.skipAnimation = true;   // static draw for now

        import::ImportResult result;
        for (const std::string& prefix : { std::string{}, std::string{ "../../" }, std::string{ "../../../" } })
        {
            result = import::LoadModelFromFile(prefix + m_modelPath, options);
            if (result.ok) break;
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
            sub.color = mesh.materialIndex >= 0 && mesh.materialIndex < static_cast<int>(result.model.materials.size())
                ? result.model.materials[static_cast<std::size_t>(mesh.materialIndex)].baseColor
                : math::Color{ 0.8f, 0.8f, 0.82f, 1.0f };

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

        OutputDebugStringA(("ModelMeshPass3D: loaded '" + m_modelPath + "' ("
            + std::to_string(m_subMeshes.size()) + " submeshes)\n").c_str());
    }

    void ModelMeshPass3D::Initialize(ID3D11Device* device)
    {
        constexpr char shader[] = R"(
cbuffer Frame  : register(b0) { row_major float4x4 viewProj; float4 lightDir; };
cbuffer Object : register(b1) { row_major float4x4 world;    float4 objColor; };
struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : NORMAL; };
VSOut VSMain(VSIn input) {
    VSOut output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(worldPos, viewProj);
    output.nrm = mul(float4(input.nrm, 0.0f), world).xyz;
    return output;
}
float4 PSMain(VSOut input) : SV_TARGET {
    float3 n = normalize(input.nrm);
    float ndotl = saturate(dot(n, -normalize(lightDir.xyz)));
    float3 lit = objColor.rgb * (0.25f + 0.75f * ndotl);
    return float4(lit, objColor.a);
}
)";
        ID3DBlob* vs{}; ID3DBlob* ps{}; ID3DBlob* errors{};
        ThrowIfFailed(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &errors), "ModelMeshPass3D vertex shader compile failed");
        SafeRelease(errors);
        ThrowIfFailed(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &errors), "ModelMeshPass3D pixel shader compile failed");
        SafeRelease(errors);
        ThrowIfFailed(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vertexShader), "CreateVertexShader failed");
        ThrowIfFailed(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_pixelShader), "CreatePixelShader failed");

        // ModelVertex is position(0) + normal(12) + uv + bone data; stride 64.
        // Bind only the two attributes this shader uses.
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
        }
        m_subMeshes.clear();
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
