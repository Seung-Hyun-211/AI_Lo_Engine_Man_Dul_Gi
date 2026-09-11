#include "render/r2d/PostProcessPass.h"

#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>

#if defined(ENGINE_WITH_3D)
#include "math/Math3D.h"
#include <random>
#endif

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

#if defined(ENGINE_WITH_3D)
    // Layout must match SsaoParams in ssao.hlsl / ssao_ms.hlsl exactly.
    struct SsaoParamsGpu
    {
        float kernel[16][4];
        float projParams[4];   // x=xScale, y=yScale, z=A, w=B (proj.m[0,5,10,14])
        float params[4];       // x=radius, y=power, z=bias, w=kernel count
        float screenSize[4];   // x=width, y=height
    };

    // Not exposed via Scene3D::postProcess (§12.9) - unlike radius/power,
    // there's little reason a caller would want to retune the bias.
    constexpr float kAoBias = 0.025f;
#endif

    // Layout must match CompositeParams in composite.hlsl / composite_ms.hlsl.
    struct CompositeParamsGpu
    {
        float aoParams[4];    // x = aoStrength
        float fogParams[4];   // x = fogNear, y = fogFar, z = enabled (0/1)
        float fogColor[4];    // rgb
        float depthProj[4];   // z = A, w = B (proj.m[10,14]) - for ViewZFromDepth, fog only
    };
}

namespace engine::render
{
    void PostProcessPass::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        // No vertex buffer: every shader here builds its triangle from
        // SV_VertexID alone (fullscreen.hlsli), so every input layout is empty.
        m_compositeShader = shaders.Get(device, "composite", nullptr, 0);
        m_compositeMsShader = shaders.Get(device, "composite_ms", nullptr, 0);

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;   // full-screen utility triangle - winding is irrelevant
        rasterDesc.DepthClipEnable = TRUE;
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_rasterizer), "CreateRasterizerState (post-process) failed");

        D3D11_SAMPLER_DESC aoSamplerDesc{};
        aoSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        aoSamplerDesc.AddressU = aoSamplerDesc.AddressV = aoSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ThrowIfFailed(device->CreateSamplerState(&aoSamplerDesc, &m_aoSampler), "CreateSamplerState (ao) failed");

        D3D11_BUFFER_DESC compositeParamsDesc{};
        compositeParamsDesc.ByteWidth = sizeof(CompositeParamsGpu);
        compositeParamsDesc.Usage = D3D11_USAGE_DEFAULT;
        compositeParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&compositeParamsDesc, nullptr, &m_compositeParams), "CreateBuffer (composite params) failed");

        // 1x1 white = "no occlusion" - the permanent fallback (see class comment).
        const std::uint8_t white = 255;
        D3D11_TEXTURE2D_DESC whiteDesc{};
        whiteDesc.Width = 1; whiteDesc.Height = 1; whiteDesc.MipLevels = 1; whiteDesc.ArraySize = 1;
        whiteDesc.Format = DXGI_FORMAT_R8_UNORM;
        whiteDesc.SampleDesc.Count = 1;
        whiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
        whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA whiteInit{ &white, sizeof(white), 0 };
        ThrowIfFailed(device->CreateTexture2D(&whiteDesc, &whiteInit, &m_whiteAoTexture), "CreateTexture2D (white ao) failed");
        ThrowIfFailed(device->CreateShaderResourceView(m_whiteAoTexture, nullptr, &m_whiteAoSrv), "CreateShaderResourceView (white ao) failed");

#if defined(ENGINE_WITH_3D)
        m_ssaoShader = shaders.Get(device, "ssao", nullptr, 0);
        m_ssaoMsShader = shaders.Get(device, "ssao_ms", nullptr, 0);
        m_blurShader = shaders.Get(device, "ssao_blur", nullptr, 0);

        D3D11_SAMPLER_DESC wrapDesc{};
        wrapDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        wrapDesc.AddressU = wrapDesc.AddressV = wrapDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        ThrowIfFailed(device->CreateSamplerState(&wrapDesc, &m_wrapSampler), "CreateSamplerState (ssao noise) failed");

        // Deterministic (fixed seed) - no reason for the kernel/noise to differ
        // frame to frame or run to run.
        std::mt19937 rng(1337);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (std::size_t i = 0; i < kKernelSize; ++i)
        {
            math::Vec3 sample{ dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, dist(rng) };
            sample = math::Normalized(sample) * dist(rng);
            float scale = static_cast<float>(i) / static_cast<float>(kKernelSize);
            scale = 0.1f + 0.9f * scale * scale;   // bias samples toward the origin
            sample = sample * scale;
            m_kernel[i * 4 + 0] = sample.x;
            m_kernel[i * 4 + 1] = sample.y;
            m_kernel[i * 4 + 2] = sample.z;
            m_kernel[i * 4 + 3] = 0.0f;
        }

        float noiseData[4 * 4 * 4];
        for (int i = 0; i < 16; ++i)
        {
            noiseData[i * 4 + 0] = dist(rng) * 2.0f - 1.0f;
            noiseData[i * 4 + 1] = dist(rng) * 2.0f - 1.0f;
            noiseData[i * 4 + 2] = 0.0f;
            noiseData[i * 4 + 3] = 0.0f;
        }
        D3D11_TEXTURE2D_DESC noiseDesc{};
        noiseDesc.Width = 4; noiseDesc.Height = 4; noiseDesc.MipLevels = 1; noiseDesc.ArraySize = 1;
        noiseDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        noiseDesc.SampleDesc.Count = 1;
        noiseDesc.Usage = D3D11_USAGE_IMMUTABLE;
        noiseDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA noiseInit{ noiseData, 4 * sizeof(float) * 4, 0 };
        ThrowIfFailed(device->CreateTexture2D(&noiseDesc, &noiseInit, &m_noiseTexture), "CreateTexture2D (ssao noise) failed");
        ThrowIfFailed(device->CreateShaderResourceView(m_noiseTexture, nullptr, &m_noiseSrv), "CreateShaderResourceView (ssao noise) failed");

        D3D11_BUFFER_DESC paramsDesc{};
        paramsDesc.ByteWidth = sizeof(SsaoParamsGpu);
        paramsDesc.Usage = D3D11_USAGE_DEFAULT;
        paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&paramsDesc, nullptr, &m_ssaoParams), "CreateBuffer (ssao params) failed");
#endif
    }

#if defined(ENGINE_WITH_3D)
    void PostProcessPass::EnsureAoTarget(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
    {
        if (width == m_aoWidth && height == m_aoHeight && m_aoRtv != nullptr) return;

        SafeRelease(m_aoSrv);
        SafeRelease(m_aoRtv);
        SafeRelease(m_aoTexture);
        SafeRelease(m_aoBlurSrv);
        SafeRelease(m_aoBlurRtv);
        SafeRelease(m_aoBlurTexture);
        m_aoWidth = 0;
        m_aoHeight = 0;
        if (width == 0 || height == 0) return;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;   // AO is computed full-resolution but single-sample regardless of scene MSAA
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, &m_aoTexture), "CreateTexture2D (ao) failed");
        ThrowIfFailed(device->CreateRenderTargetView(m_aoTexture, nullptr, &m_aoRtv), "CreateRenderTargetView (ao) failed");
        ThrowIfFailed(device->CreateShaderResourceView(m_aoTexture, nullptr, &m_aoSrv), "CreateShaderResourceView (ao) failed");

        // Blurred copy Composite actually reads - see ssao_blur.hlsl.
        ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, &m_aoBlurTexture), "CreateTexture2D (ao blur) failed");
        ThrowIfFailed(device->CreateRenderTargetView(m_aoBlurTexture, nullptr, &m_aoBlurRtv), "CreateRenderTargetView (ao blur) failed");
        ThrowIfFailed(device->CreateShaderResourceView(m_aoBlurTexture, nullptr, &m_aoBlurSrv), "CreateShaderResourceView (ao blur) failed");

        m_aoWidth = width;
        m_aoHeight = height;
    }

    ID3D11ShaderResourceView* PostProcessPass::ComputeAo(const PassContext& context)
    {
        if (context.sceneDepthSrv == nullptr || context.sceneNormalSrv == nullptr) return m_whiteAoSrv;

        EnsureAoTarget(context.device, context.viewportWidth, context.viewportHeight);
        if (m_aoRtv == nullptr || m_aoBlurRtv == nullptr) return m_whiteAoSrv;

        const bool multisampled = context.sceneSampleCount > 1;
        const ShaderProgram* shader = multisampled ? m_ssaoMsShader : m_ssaoShader;
        if (shader == nullptr || m_blurShader == nullptr) return m_whiteAoSrv;

        const auto& postProcess = context.snapshot->scene3d.postProcess;

        SsaoParamsGpu params{};
        std::memcpy(params.kernel, m_kernel.data(), sizeof(params.kernel));
        const math::Mat4& proj = context.snapshot->scene3d.camera.projection;
        params.projParams[0] = proj.m[0];    // xScale
        params.projParams[1] = proj.m[5];    // yScale
        params.projParams[2] = proj.m[10];   // A = far/(far-near)
        params.projParams[3] = proj.m[14];   // B = -near*far/(far-near)
        params.params[0] = postProcess.aoRadius;
        params.params[1] = postProcess.aoPower;
        params.params[2] = kAoBias;
        params.params[3] = static_cast<float>(kKernelSize);
        params.screenSize[0] = static_cast<float>(context.viewportWidth);
        params.screenSize[1] = static_cast<float>(context.viewportHeight);

        ID3D11DeviceContext* device = context.context;
        device->UpdateSubresource(m_ssaoParams, 0, nullptr, &params, 0, 0);

        device->OMSetRenderTargets(1, &m_aoRtv, nullptr);
        ID3D11ShaderResourceView* srvs[3]{ context.sceneDepthSrv, context.sceneNormalSrv, m_noiseSrv };
        device->PSSetShaderResources(0, 3, srvs);
        device->PSSetSamplers(0, 1, &m_wrapSampler);
        device->PSSetConstantBuffers(0, 1, &m_ssaoParams);
        device->RSSetState(m_rasterizer);
        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->IASetInputLayout(nullptr);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(shader->vs, nullptr, 0);
        device->PSSetShader(shader->ps, nullptr, 0);
        device->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrvs[3]{ nullptr, nullptr, nullptr };
        device->PSSetShaderResources(0, 3, nullSrvs);   // detach - depth/normal are render targets again next frame

        // Blur pass: 4x4 box blur matching the noise tile, so the per-pixel
        // kernel rotation reads as smooth AO instead of a dithered stipple
        // (ssao_blur.hlsl). Binding m_aoBlurRtv here implicitly detaches
        // m_aoRtv, so m_aoSrv (the same resource) is safe to read right after.
        device->OMSetRenderTargets(1, &m_aoBlurRtv, nullptr);
        ID3D11ShaderResourceView* aoSrv = m_aoSrv;
        device->PSSetShaderResources(0, 1, &aoSrv);
        device->VSSetShader(m_blurShader->vs, nullptr, 0);
        device->PSSetShader(m_blurShader->ps, nullptr, 0);
        device->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrv1{ nullptr };
        device->PSSetShaderResources(0, 1, &nullSrv1);   // detach - ao is a render target again next frame

        return m_aoBlurSrv;
    }
#endif

    void PostProcessPass::Execute(const PassContext& context)
    {
        ID3D11DeviceContext* device = context.context;

        // Detach the geometry stage's render targets so their SRVs (colour,
        // depth, normal - context.scene*Srv) can be safely bound as shader
        // inputs; all three alias resources that are currently bound as
        // RTV/DSV. See docs/post-process-gbuffer-research.md §12.1/§12.3.
        device->OMSetRenderTargets(0, nullptr, nullptr);

#if defined(ENGINE_WITH_3D)
        ID3D11ShaderResourceView* aoSrv = ComputeAo(context);
#else
        ID3D11ShaderResourceView* aoSrv = m_whiteAoSrv;
#endif

        Composite(context, aoSrv);
    }

    void PostProcessPass::Composite(const PassContext& context, ID3D11ShaderResourceView* aoSrv)
    {
        if (context.backBufferRenderTarget == nullptr || context.sceneColorSrv == nullptr) return;

        const bool multisampled = context.sceneSampleCount > 1;
        const ShaderProgram* shader = multisampled ? m_compositeMsShader : m_compositeShader;
        if (shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;

        // aoStrength defaults to "full effect" and fog to "off" so a 2D-only
        // build (no Scene3D::postProcess to read) still composites correctly -
        // ao is already the white fallback there (Execute), and fogParams[2]
        // staying 0 skips the fog branch in the shader entirely.
        CompositeParamsGpu params{};
        params.aoParams[0] = 1.0f;
#if defined(ENGINE_WITH_3D)
        const auto& postProcess = context.snapshot->scene3d.postProcess;
        params.aoParams[0] = postProcess.aoStrength;
        params.fogParams[0] = postProcess.fogNear;
        params.fogParams[1] = postProcess.fogFar;
        params.fogParams[2] = postProcess.fogEnabled ? 1.0f : 0.0f;
        params.fogColor[0] = postProcess.fogColor.r;
        params.fogColor[1] = postProcess.fogColor.g;
        params.fogColor[2] = postProcess.fogColor.b;
        const math::Mat4& proj = context.snapshot->scene3d.camera.projection;
        params.depthProj[2] = proj.m[10];   // A
        params.depthProj[3] = proj.m[14];   // B
#endif
        device->UpdateSubresource(m_compositeParams, 0, nullptr, &params, 0, 0);

        // No depth buffer *attachment* from here on: the back buffer is always
        // single-sample, so it could never be bound together with a
        // multisampled scene depth, and nothing after this point needs depth
        // testing (fog reads scene depth as a plain SRV, at t2 below, not as
        // the active DSV).
        ID3D11RenderTargetView* backBuffer = context.backBufferRenderTarget;
        device->OMSetRenderTargets(1, &backBuffer, nullptr);

        ID3D11ShaderResourceView* srvs[3]{ context.sceneColorSrv, aoSrv, context.sceneDepthSrv };
        device->PSSetShaderResources(0, 3, srvs);
        device->PSSetSamplers(0, 1, &m_aoSampler);
        device->PSSetConstantBuffers(0, 1, &m_compositeParams);
        device->RSSetState(m_rasterizer);
        device->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        device->IASetInputLayout(nullptr);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(shader->vs, nullptr, 0);
        device->PSSetShader(shader->ps, nullptr, 0);
        device->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrvs[3]{ nullptr, nullptr, nullptr };
        device->PSSetShaderResources(0, 3, nullSrvs);   // detach - scene colour/depth are render targets again next frame
    }

    void PostProcessPass::Release()
    {
        m_compositeShader = nullptr;     // owned by ShaderLibrary
        m_compositeMsShader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_rasterizer);
        SafeRelease(m_aoSampler);
        SafeRelease(m_compositeParams);
        SafeRelease(m_whiteAoSrv);
        SafeRelease(m_whiteAoTexture);

#if defined(ENGINE_WITH_3D)
        m_ssaoShader = nullptr;      // owned by ShaderLibrary
        m_ssaoMsShader = nullptr;    // owned by ShaderLibrary
        m_blurShader = nullptr;      // owned by ShaderLibrary
        SafeRelease(m_wrapSampler);
        SafeRelease(m_noiseSrv);
        SafeRelease(m_noiseTexture);
        SafeRelease(m_ssaoParams);
        SafeRelease(m_aoSrv);
        SafeRelease(m_aoRtv);
        SafeRelease(m_aoTexture);
        SafeRelease(m_aoBlurSrv);
        SafeRelease(m_aoBlurRtv);
        SafeRelease(m_aoBlurTexture);
        m_aoWidth = 0;
        m_aoHeight = 0;
#endif
    }
}
