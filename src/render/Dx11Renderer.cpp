#include "render/Dx11Renderer.h"

#include "render/r2d/PostProcessPass.h"
#include "render/r2d/QuadPass2D.h"
#if defined(ENGINE_WITH_3D)
#include "render/r3d/FrameConstants.h"
#include "render/r3d/MeshPass3D.h"
#endif

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace
{
    void ThrowIfFailed(HRESULT result, const char* message)
    {
        if (FAILED(result)) throw std::runtime_error(message);
    }

    template <typename T>
    void Release(T*& object)
    {
        if (object != nullptr) { object->Release(); object = nullptr; }
    }
}

namespace engine::render
{
    Dx11Renderer::Dx11Renderer()
    {
        // Default pipeline: 3D geometry first (writes depth), then
        // PostProcessPass hands the frame off from the scene colour target to
        // the back buffer (docs/post-process-gbuffer-research.md §2),
        // then the 2D overlay on top. The 3D pass is only registered when the
        // 3D module is built in; AddRenderPass inserts further geometry stages
        // before PostProcessPass (see main.cpp).
#if defined(ENGINE_WITH_3D)
        m_passes.push_back(std::make_unique<MeshPass3D>());
#endif
        m_passes.push_back(std::make_unique<PostProcessPass>());
        m_passes.push_back(std::make_unique<QuadPass2D>());
    }

    Dx11Renderer::~Dx11Renderer() { Stop(); }

    void Dx11Renderer::AddRenderPass(std::unique_ptr<IRenderPass> pass, bool atEnd)
    {
        std::scoped_lock lock(m_mutex);
        if (m_running) throw std::logic_error("AddRenderPass must be called before Start()");
        if (!pass) return;
        // Default: slot in before the trailing {PostProcessPass, QuadPass2D}
        // pair so a new geometry pass still runs while the scene colour target
        // is bound, and the quad UI still ends up on top
        // (docs/post-process-gbuffer-research.md §2). `atEnd` appends
        // after everything - for a pass that must run last (e.g. SpritePass2D,
        // whose scissor rasterizer state would otherwise leak into QuadPass2D).
        constexpr std::size_t kTrailingPassCount = 2;   // PostProcessPass, QuadPass2D
        if (atEnd || m_passes.size() < kTrailingPassCount) m_passes.push_back(std::move(pass));
        else m_passes.insert(m_passes.end() - static_cast<std::ptrdiff_t>(kTrailingPassCount), std::move(pass));
    }

    void Dx11Renderer::Start(HWND window, std::uint32_t width, std::uint32_t height)
    {
        std::unique_lock lock(m_mutex);
        if (m_running) return;
        m_running = true;
        m_initialized = false;
        m_startError = nullptr;
        m_thread = std::thread(&Dx11Renderer::RenderLoop, this, window, width, height);
        m_started.wait(lock, [this] { return m_initialized || m_startError != nullptr; });
        if (m_startError != nullptr)
        {
            lock.unlock();
            Stop();
            std::rethrow_exception(m_startError);
        }
    }

    void Dx11Renderer::Submit(RenderSnapshot snapshot)
    {
        {
            std::scoped_lock lock(m_mutex);
            if (!m_running) return;
            m_pendingSnapshot = std::move(snapshot);
        }
        m_workReady.notify_one();
    }

    void Dx11Renderer::SetFrameSettings(FrameSettings settings)
    {
        {
            std::scoped_lock lock(m_mutex);
            m_frameSettings = settings;
        }
        m_workReady.notify_one();
    }

    void Dx11Renderer::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0) return;
        {
            std::scoped_lock lock(m_mutex);
            if (!m_running) return;
            m_pendingResize = SIZE{ static_cast<LONG>(width), static_cast<LONG>(height) };
        }
        m_workReady.notify_one();
    }

    void Dx11Renderer::Stop()
    {
        {
            std::scoped_lock lock(m_mutex);
            if (!m_running && !m_thread.joinable()) return;
            m_running = false;
        }
        m_workReady.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }

    void Dx11Renderer::RenderLoop(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight)
    {
        try
        {
            CreateDeviceAndSwapChain(window, initialWidth, initialHeight);
            { std::scoped_lock lock(m_mutex); m_initialized = true; }
            m_started.notify_one();

            auto nextFrameDeadline = std::chrono::steady_clock::now();
            for (;;)
            {
                std::optional<RenderSnapshot> snapshot;
                std::optional<SIZE> resize;
                FrameSettings settings;
                {
                    std::unique_lock lock(m_mutex);
                    m_workReady.wait(lock, [this] { return !m_running || m_pendingSnapshot || m_pendingResize; });
                    if (!m_running) break;
                    snapshot = std::move(m_pendingSnapshot);
                    m_pendingSnapshot.reset();
                    resize = m_pendingResize;
                    m_pendingResize.reset();
                    settings = m_frameSettings;
                }

                if (resize)
                    ResizeBackBuffer(static_cast<std::uint32_t>(resize->cx), static_cast<std::uint32_t>(resize->cy));

                if (snapshot)
                {
                    Render(*snapshot, settings);

                    if (!settings.verticalSync && settings.targetFramesPerSecond > 0)
                    {
                        const auto frameDuration = std::chrono::duration<double>(
                            1.0 / static_cast<double>(settings.targetFramesPerSecond));
                        nextFrameDeadline = std::max(nextFrameDeadline, std::chrono::steady_clock::now())
                            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration);
                        std::this_thread::sleep_until(nextFrameDeadline);
                    }
                    else
                    {
                        nextFrameDeadline = std::chrono::steady_clock::now();
                    }
                }
            }
        }
        catch (...)
        {
            std::scoped_lock lock(m_mutex);
            if (!m_initialized)
            {
                m_startError = std::current_exception();
                m_started.notify_one();
            }
        }

        for (std::unique_ptr<IRenderPass>& pass : m_passes)
            if (pass) pass->Release();
        m_shaders.ReleaseAll();
        ReleaseShadowResources();
        ReleaseSceneTargets();
        Release(m_backBufferRtv);
        Release(m_swapChain);
        Release(m_context);
        Release(m_device);
    }

    void Dx11Renderer::CreateDeviceAndSwapChain(HWND window, std::uint32_t width, std::uint32_t height)
    {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Width = width;
        description.BufferDesc.Height = height;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT deviceFlags = 0;
#if defined(_DEBUG)
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        // Try the real GPU first, then fall back to the WARP software rasterizer
        // (headless VMs, remote sessions, no D3D11 adapter). Each driver type is
        // also retried without the debug layer, which is absent unless the
        // Graphics Tools optional feature is installed.
        const D3D_DRIVER_TYPE driverTypes[]{ D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
        HRESULT result = E_FAIL;
        for (const D3D_DRIVER_TYPE driverType : driverTypes)
        {
            UINT flags = deviceFlags;
            result = D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr,
                flags, nullptr, 0, D3D11_SDK_VERSION, &description, &m_swapChain, &m_device, nullptr, &m_context);
            if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
            {
                flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
                result = D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr,
                    flags, nullptr, 0, D3D11_SDK_VERSION, &description, &m_swapChain, &m_device, nullptr, &m_context);
            }
            if (SUCCEEDED(result)) break;
        }
        ThrowIfFailed(result, "D3D11CreateDeviceAndSwapChain failed (hardware and WARP)");

        // Let the runtime, not DXGI, own Alt+Enter.
        if (IDXGIDevice* dxgiDevice{}; SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
        {
            if (IDXGIAdapter* adapter{}; SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
            {
                if (IDXGIFactory* factory{}; SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))))
                {
                    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
                    Release(factory);
                }
                Release(adapter);
            }
            Release(dxgiDevice);
        }

        // Pick the highest supported MSAA sample count up to 8x for smooth
        // silhouettes (the toon outline is a geometry edge). Falls back to 1x
        // (no MSAA, no resolve) when nothing is supported.
        m_sampleCount = 1;
        for (const UINT candidate : { 8u, 4u, 2u })
        {
            UINT qualityLevels = 0;
            if (SUCCEEDED(m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, candidate, &qualityLevels))
                && qualityLevels > 0)
            {
                m_sampleCount = candidate;
                break;
            }
        }

        m_width = width; m_height = height;
        CreateBackBufferView();
        CreateSceneTargets(width, height);
        CreateShadowResources();

        for (std::unique_ptr<IRenderPass>& pass : m_passes)
            pass->Initialize(m_device, m_shaders);
    }

    void Dx11Renderer::CreateShadowResources()
    {
#if defined(ENGINE_WITH_3D)
        const UINT size = kShadowMapSize;

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = size;
        depthDesc.Height = size;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ThrowIfFailed(m_device->CreateTexture2D(&depthDesc, nullptr, &m_shadowDepth), "CreateTexture2D (shadow) failed");

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ThrowIfFailed(m_device->CreateDepthStencilView(m_shadowDepth, &dsvDesc, &m_shadowDsv), "CreateDepthStencilView (shadow) failed");

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        ThrowIfFailed(m_device->CreateShaderResourceView(m_shadowDepth, &srvDesc, &m_shadowSrv), "CreateShaderResourceView (shadow) failed");

        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.ByteWidth = 16 * sizeof(float);   // one row-major float4x4
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(m_device->CreateBuffer(&cbDesc, nullptr, &m_shadowFrameCb), "CreateBuffer (shadow frame) failed");

        D3D11_SAMPLER_DESC sampDesc{};
        sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(m_device->CreateSamplerState(&sampDesc, &m_shadowSampler), "CreateSamplerState (shadow) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;   // model winding unverified; lean on bias
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.DepthBias = 1200;
        rasterDesc.SlopeScaledDepthBias = 2.5f;
        ThrowIfFailed(m_device->CreateRasterizerState(&rasterDesc, &m_shadowRaster), "CreateRasterizerState (shadow) failed");

        D3D11_DEPTH_STENCIL_DESC dsDesc{};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
        ThrowIfFailed(m_device->CreateDepthStencilState(&dsDesc, &m_shadowDepthState), "CreateDepthStencilState (shadow) failed");
#endif
    }

    void Dx11Renderer::ReleaseShadowResources()
    {
        Release(m_shadowDepthState);
        Release(m_shadowRaster);
        Release(m_shadowSampler);
        Release(m_shadowFrameCb);
        Release(m_shadowSrv);
        Release(m_shadowDsv);
        Release(m_shadowDepth);
    }

    void Dx11Renderer::RenderShadowMap(const RenderSnapshot& snapshot)
    {
#if defined(ENGINE_WITH_3D)
        ID3D11ShaderResourceView* nullSrv = nullptr;
        m_context->PSSetShaderResources(1, 1, &nullSrv);   // detach before writing it

        m_context->OMSetRenderTargets(0, nullptr, m_shadowDsv);
        m_context->ClearDepthStencilView(m_shadowDsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0, 1 };
        m_context->RSSetViewports(1, &viewport);
        m_context->RSSetState(m_shadowRaster);
        m_context->OMSetDepthStencilState(m_shadowDepthState, 0);
        m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);

        float lightViewProj[16];
        std::memcpy(lightViewProj, snapshot.scene3d.lighting.lightViewProj.m, sizeof(lightViewProj));
        m_context->UpdateSubresource(m_shadowFrameCb, 0, nullptr, lightViewProj, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, &m_shadowFrameCb);

        ShadowContext context{};
        context.context = m_context;
        context.snapshot = &snapshot;
        for (std::unique_ptr<IRenderPass>& pass : m_passes)
            pass->RenderShadow(context);
#else
        (void)snapshot;
#endif
    }

    void Dx11Renderer::CreateBackBufferView()
    {
        ID3D11Texture2D* backBuffer{};
        ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer failed");
        const HRESULT result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_backBufferRtv);
        Release(backBuffer);
        ThrowIfFailed(result, "CreateRenderTargetView (back buffer) failed");
    }

    void Dx11Renderer::CreateSceneTargets(std::uint32_t width, std::uint32_t height)
    {
        const bool multisampled = m_sampleCount > 1;

        // Colour is always a dedicated texture now, multisampled or not - it
        // used to alias the back buffer's RTV directly when 1x, but the back
        // buffer can't be bound as a shader resource (the swap chain only
        // requests DXGI_USAGE_RENDER_TARGET_OUTPUT), and PostProcessPass needs
        // to read scene colour regardless of sample count. See
        // docs/post-process-gbuffer-research.md §3.1.
        D3D11_TEXTURE2D_DESC colorDesc{};
        colorDesc.Width = width;
        colorDesc.Height = height;
        colorDesc.MipLevels = 1;
        colorDesc.ArraySize = 1;
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = m_sampleCount;
        colorDesc.Usage = D3D11_USAGE_DEFAULT;
        colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ThrowIfFailed(m_device->CreateTexture2D(&colorDesc, nullptr, &m_sceneColor), "CreateTexture2D (scene colour) failed");
        ThrowIfFailed(m_device->CreateRenderTargetView(m_sceneColor, nullptr, &m_sceneColorRtv), "CreateRenderTargetView (scene colour) failed");

        D3D11_SHADER_RESOURCE_VIEW_DESC colorSrvDesc{};
        colorSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (multisampled)
        {
            colorSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            colorSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            colorSrvDesc.Texture2D.MipLevels = 1;
        }
        ThrowIfFailed(m_device->CreateShaderResourceView(m_sceneColor, &colorSrvDesc, &m_sceneColorSrv), "CreateShaderResourceView (scene colour) failed");

        // View-space normal G-buffer, same size/sample count as colour - bound
        // as a second render target during the geometry stage. See
        // docs/post-process-gbuffer-research.md §3.1.
        D3D11_TEXTURE2D_DESC normalDesc = colorDesc;
        ThrowIfFailed(m_device->CreateTexture2D(&normalDesc, nullptr, &m_sceneNormal), "CreateTexture2D (scene normal) failed");
        ThrowIfFailed(m_device->CreateRenderTargetView(m_sceneNormal, nullptr, &m_sceneNormalRtv), "CreateRenderTargetView (scene normal) failed");

        D3D11_SHADER_RESOURCE_VIEW_DESC normalSrvDesc = colorSrvDesc;
        ThrowIfFailed(m_device->CreateShaderResourceView(m_sceneNormal, &normalSrvDesc, &m_sceneNormalSrv), "CreateShaderResourceView (scene normal) failed");

        // Typeless + BIND_SHADER_RESOURCE (same combination as m_shadowDepth) so a
        // later pass can read depth as t-something while it's still bound as the
        // DSV elsewhere in the frame - see docs/post-process-gbuffer-research.md
        // §3.1. The DSV keeps depth-stencil semantics unchanged
        // (D24_UNORM_S8_UINT); only the SRV view is new and, until something
        // actually samples it, this is a no-op resource-shape change.
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        depthDesc.SampleDesc.Count = m_sampleCount;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ThrowIfFailed(m_device->CreateTexture2D(&depthDesc, nullptr, &m_sceneDepth), "CreateTexture2D (depth) failed");

        D3D11_DEPTH_STENCIL_VIEW_DESC depthDsvDesc{};
        depthDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDsvDesc.ViewDimension = multisampled ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        ThrowIfFailed(m_device->CreateDepthStencilView(m_sceneDepth, &depthDsvDesc, &m_sceneDepthDsv), "CreateDepthStencilView failed");

        D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
        depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        if (multisampled)
        {
            depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depthSrvDesc.Texture2D.MipLevels = 1;
        }
        ThrowIfFailed(m_device->CreateShaderResourceView(m_sceneDepth, &depthSrvDesc, &m_sceneDepthSrv), "CreateShaderResourceView (scene depth) failed");
    }

    void Dx11Renderer::ReleaseSceneTargets()
    {
        Release(m_sceneDepthSrv);
        Release(m_sceneDepthDsv);
        Release(m_sceneDepth);
        Release(m_sceneNormalSrv);
        Release(m_sceneNormalRtv);
        Release(m_sceneNormal);
        Release(m_sceneColorSrv);
        Release(m_sceneColorRtv);
        Release(m_sceneColor);
    }

    void Dx11Renderer::ResizeBackBuffer(std::uint32_t width, std::uint32_t height)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        ReleaseSceneTargets();
        Release(m_backBufferRtv);
        ThrowIfFailed(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers failed");
        m_width = width; m_height = height;
        CreateBackBufferView();
        CreateSceneTargets(width, height);
    }

    void Dx11Renderer::Render(const RenderSnapshot& snapshot, const FrameSettings& settings)
    {
        // Pick up any .hlsl edited on disk since the last frame.
        m_shaders.PollHotReload(m_device);

        // Shadow map first (own target + viewport), then the scene.
#if defined(ENGINE_WITH_3D)
        const bool shadows = snapshot.scene3d.lighting.shadowsEnabled && m_shadowDsv != nullptr;
        if (shadows) RenderShadowMap(snapshot);
#endif

        // Geometry stage: colour + view-space normal MRT (docs/post-process-
        // gbuffer-research.md §3.1/§3.4). A pass whose PS doesn't declare
        // SV_TARGET1 (outline, debug lines, shadow) just leaves the normal
        // target at this clear value for its pixels - see common3d.hlsli's
        // GeometryPSOut comment.
        ID3D11RenderTargetView* geometryTargets[2]{ m_sceneColorRtv, m_sceneNormalRtv };
        m_context->OMSetRenderTargets(2, geometryTargets, m_sceneDepthDsv);
        m_context->ClearRenderTargetView(m_sceneColorRtv, snapshot.clearColor);
        const float neutralNormal[4]{ 0.5f, 0.5f, 1.0f, 1.0f };   // decodes to view-space (0,0,1)
        m_context->ClearRenderTargetView(m_sceneNormalRtv, neutralNormal);
        m_context->ClearDepthStencilView(m_sceneDepthDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1 };
        m_context->RSSetViewports(1, &viewport);

#if defined(ENGINE_WITH_3D)
        if (shadows)
        {
            m_context->PSSetShaderResources(1, 1, &m_shadowSrv);
            m_context->PSSetSamplers(1, 1, &m_shadowSampler);
        }
#endif

        PassContext context{};
        context.device = m_device;
        context.context = m_context;
        context.renderTarget = m_sceneColorRtv;
        context.depthStencil = m_sceneDepthDsv;
        context.viewportWidth = m_width;
        context.viewportHeight = m_height;
        context.snapshot = &snapshot;
        // PostProcessPass (always in m_passes, see the constructor) reads these
        // to hand the frame off from the scene colour target to the back
        // buffer - see docs/post-process-gbuffer-research.md §2/§3.1.
        // It also now does the job the old end-of-frame ResolveSubresource used
        // to when multisampled, so there is no separate resolve call here.
        context.backBufferRenderTarget = m_backBufferRtv;
        context.sceneColorSrv = m_sceneColorSrv;
        context.sceneNormalSrv = m_sceneNormalSrv;
        context.sceneDepthSrv = m_sceneDepthSrv;
        context.sceneSampleCount = m_sampleCount;
        for (std::unique_ptr<IRenderPass>& pass : m_passes)
            pass->Execute(context);

        ThrowIfFailed(m_swapChain->Present(settings.verticalSync ? 1 : 0, 0), "Present failed");
    }
}
