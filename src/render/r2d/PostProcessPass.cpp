#include "render/r2d/PostProcessPass.h"

#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

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
        if (object != nullptr) { object->Release(); object = nullptr; }
    }
}

namespace engine::render
{
    void PostProcessPass::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        // No vertex buffer: both shaders' VS (fullscreen.hlsli) builds the
        // triangle from SV_VertexID alone, so the input layout is empty.
        m_compositeShader = shaders.Get(device, "composite", nullptr, 0);
        m_compositeMsShader = shaders.Get(device, "composite_ms", nullptr, 0);

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;   // full-screen utility triangle - winding is irrelevant
        rasterDesc.DepthClipEnable = TRUE;
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState (post-process) failed");
    }

    void PostProcessPass::Execute(const PassContext& context)
    {
        if (context.backBufferRenderTarget == nullptr || context.sceneColorSrv == nullptr) return;

        const bool multisampled = context.sceneSampleCount > 1;
        const ShaderProgram* shader = multisampled ? m_compositeMsShader : m_compositeShader;
        if (shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;

        // Hand off from the scene colour target to the back buffer. No depth
        // buffer from here on: the back buffer is always single-sample, so it
        // could never be bound together with a multisampled scene depth, and
        // nothing after this point (AO aside, once it lands) needs depth
        // testing anyway. §3/§12.1.
        ID3D11RenderTargetView* backBuffer = context.backBufferRenderTarget;
        device->OMSetRenderTargets(1, &backBuffer, nullptr);

        ID3D11ShaderResourceView* srv = context.sceneColorSrv;
        device->PSSetShaderResources(0, 1, &srv);
        device->RSSetState(m_rasterizer);
        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->IASetInputLayout(nullptr);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(shader->vs, nullptr, 0);
        device->PSSetShader(shader->ps, nullptr, 0);
        device->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrv = nullptr;
        device->PSSetShaderResources(0, 1, &nullSrv);   // detach - scene colour is a render target again next frame
    }

    void PostProcessPass::Release()
    {
        m_compositeShader = nullptr;     // owned by ShaderLibrary
        m_compositeMsShader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_rasterizer);
    }
}
