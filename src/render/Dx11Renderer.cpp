#include "render/Dx11Renderer.h"

#include "render/r2d/QuadPass2D.h"
#if defined(ENGINE_WITH_3D)
#include "render/r3d/MeshPass3D.h"
#endif

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <chrono>
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
        // Default pipeline: 3D first (writes depth), then the 2D overlay on top.
        // The 3D pass is only registered when the 3D module is built in;
        // AddRenderPass appends further stages (see main.cpp).
#if defined(ENGINE_WITH_3D)
        m_passes.push_back(std::make_unique<MeshPass3D>());
#endif
        m_passes.push_back(std::make_unique<QuadPass2D>());
    }

    Dx11Renderer::~Dx11Renderer() { Stop(); }

    void Dx11Renderer::AddRenderPass(std::unique_ptr<IRenderPass> pass)
    {
        std::scoped_lock lock(m_mutex);
        if (m_running) throw std::logic_error("AddRenderPass must be called before Start()");
        if (!pass) return;
        // Keep the trailing 2D overlay pass last so the UI stays on top; new
        // passes slot in just before it.
        if (m_passes.empty()) m_passes.push_back(std::move(pass));
        else m_passes.insert(m_passes.end() - 1, std::move(pass));
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
        Release(m_depthStencil);
        Release(m_depthTexture);
        Release(m_renderTarget);
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

        m_width = width; m_height = height;
        CreateRenderTarget();
        CreateDepthBuffer(width, height);

        for (std::unique_ptr<IRenderPass>& pass : m_passes)
            pass->Initialize(m_device, m_shaders);
    }

    void Dx11Renderer::CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer{};
        ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer failed");
        const HRESULT result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
        Release(backBuffer);
        ThrowIfFailed(result, "CreateRenderTargetView failed");
    }

    void Dx11Renderer::CreateDepthBuffer(std::uint32_t width, std::uint32_t height)
    {
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ThrowIfFailed(m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthTexture), "CreateTexture2D (depth) failed");
        ThrowIfFailed(m_device->CreateDepthStencilView(m_depthTexture, nullptr, &m_depthStencil), "CreateDepthStencilView failed");
    }

    void Dx11Renderer::ResizeBackBuffer(std::uint32_t width, std::uint32_t height)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        Release(m_depthStencil);
        Release(m_depthTexture);
        Release(m_renderTarget);
        ThrowIfFailed(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers failed");
        m_width = width; m_height = height;
        CreateRenderTarget();
        CreateDepthBuffer(width, height);
    }

    void Dx11Renderer::Render(const RenderSnapshot& snapshot, const FrameSettings& settings)
    {
        m_context->OMSetRenderTargets(1, &m_renderTarget, m_depthStencil);
        m_context->ClearRenderTargetView(m_renderTarget, snapshot.clearColor);
        m_context->ClearDepthStencilView(m_depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1 };
        m_context->RSSetViewports(1, &viewport);

        // Pick up any .hlsl edited on disk since the last frame.
        m_shaders.PollHotReload(m_device);

        PassContext context{};
        context.device = m_device;
        context.context = m_context;
        context.renderTarget = m_renderTarget;
        context.depthStencil = m_depthStencil;
        context.viewportWidth = m_width;
        context.viewportHeight = m_height;
        context.snapshot = &snapshot;
        for (std::unique_ptr<IRenderPass>& pass : m_passes)
            pass->Execute(context);

        ThrowIfFailed(m_swapChain->Present(settings.verticalSync ? 1 : 0, 0), "Present failed");
    }
}
