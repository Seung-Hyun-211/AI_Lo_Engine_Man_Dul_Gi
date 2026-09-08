#include "render/Dx11Renderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct Vertex { float x, y, r, g, b, a; };
    struct Constants { float screenWidth, screenHeight, unused0, unused1; };

    // ~43k quads per frame. The world sample plus the UI overlay stay well under
    // this; anything past it is dropped for the frame rather than reallocating.
    constexpr std::size_t kMaxVertices = 262'144;

    void AddQuad(std::vector<Vertex>& vertices, const engine::render::Quad& quad)
    {
        const Vertex topLeft{ quad.x, quad.y, quad.r, quad.g, quad.b, quad.a };
        const Vertex topRight{ quad.x + quad.width, quad.y, quad.r, quad.g, quad.b, quad.a };
        const Vertex bottomLeft{ quad.x, quad.y + quad.height, quad.r, quad.g, quad.b, quad.a };
        const Vertex bottomRight{ quad.x + quad.width, quad.y + quad.height, quad.r, quad.g, quad.b, quad.a };
        vertices.insert(vertices.end(), { topLeft, topRight, bottomLeft, bottomLeft, topRight, bottomRight });
    }

    void AppendQuads(std::vector<Vertex>& vertices, const std::vector<engine::render::Quad>& quads)
    {
        for (const engine::render::Quad& quad : quads)
        {
            if (vertices.size() + 6 > kMaxVertices) break;
            AddQuad(vertices, quad);
        }
    }

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
    Dx11Renderer::~Dx11Renderer() { Stop(); }

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
        // A 1 FPS cap is valid for diagnostics; zero deliberately means uncapped.
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

                    // Present(1) is display-driven. Software capping only applies
                    // when VSync is off and a target FPS is set.
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

        Release(m_blendState);
        Release(m_constantBuffer); Release(m_vertexBuffer); Release(m_inputLayout);
        Release(m_pixelShader); Release(m_vertexShader); Release(m_renderTarget);
        Release(m_swapChain); Release(m_context); Release(m_device);
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
        // Legacy blt model. FLIP_DISCARD is preferred on Win10+ but needs extra
        // per-frame handling; revisit when the SpriteBatch pipeline lands.
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT deviceFlags = 0;
#if defined(_DEBUG)
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        // Try the real GPU first, then fall back to the WARP software rasterizer
        // (headless VMs, remote sessions, machines with no D3D11 adapter). Each
        // driver type is also retried without the debug layer, which is absent
        // unless the Graphics Tools optional feature is installed.
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

        // Let the runtime, not DXGI, own Alt+Enter so the borderless/fullscreen
        // transition stays under application control.
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
        CreateSpritePipeline();
    }

    void Dx11Renderer::CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer{};
        ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer failed");
        const HRESULT result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
        Release(backBuffer);
        ThrowIfFailed(result, "CreateRenderTargetView failed");
    }

    void Dx11Renderer::CreateSpritePipeline()
    {
        constexpr char shader[] = R"(
cbuffer Constants : register(b0) { float2 screen; float2 unused; };
struct VSIn { float2 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR; };
VSOut VSMain(VSIn input) {
    VSOut output; float2 pixel = input.pos;
    output.pos = float4(pixel.x / screen.x * 2.0f - 1.0f, 1.0f - pixel.y / screen.y * 2.0f, 0, 1);
    output.color = input.color; return output;
}
float4 PSMain(VSOut input) : SV_TARGET { return input.color; }
)";
        ID3DBlob* vs{}; ID3DBlob* ps{}; ID3DBlob* errors{};
        ThrowIfFailed(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &errors), "Vertex shader compilation failed");
        Release(errors);
        ThrowIfFailed(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &errors), "Pixel shader compilation failed");
        Release(errors);
        ThrowIfFailed(m_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vertexShader), "CreateVertexShader failed");
        ThrowIfFailed(m_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_pixelShader), "CreatePixelShader failed");
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        const HRESULT layoutResult = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), vs->GetBufferPointer(), vs->GetBufferSize(), &m_inputLayout);
        Release(vs); Release(ps);
        ThrowIfFailed(layoutResult, "CreateInputLayout failed");

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * kMaxVertices);
        vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(m_device->CreateBuffer(&vertexDesc, nullptr, &m_vertexBuffer), "CreateBuffer (vertex) failed");

        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(Constants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(m_device->CreateBuffer(&constantDesc, nullptr, &m_constantBuffer), "CreateBuffer (constant) failed");

        // Straight (non-premultiplied) alpha over. Without this the UI's
        // translucent panels (a < 1) would render fully opaque.
        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(m_device->CreateBlendState(&blendDesc, &m_blendState), "CreateBlendState failed");
    }

    void Dx11Renderer::ResizeBackBuffer(std::uint32_t width, std::uint32_t height)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        Release(m_renderTarget);
        ThrowIfFailed(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers failed");
        m_width = width; m_height = height;
        CreateRenderTarget();
    }

    void Dx11Renderer::Render(const RenderSnapshot& snapshot, const FrameSettings& settings)
    {
        m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_context->ClearRenderTargetView(m_renderTarget, snapshot.clearColor);
        const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1 };
        m_context->RSSetViewports(1, &viewport);

        // World first, UI on top. The renderer stays agnostic to what a quad
        // represents; the snapshot builder decides the draw order.
        std::vector<Vertex> vertices;
        vertices.reserve(6 * (snapshot.worldQuads.size() + snapshot.uiQuads.size()));
        AppendQuads(vertices, snapshot.worldQuads);
        AppendQuads(vertices, snapshot.uiQuads);
        if (vertices.empty()) { ThrowIfFailed(m_swapChain->Present(settings.verticalSync ? 1 : 0, 0), "Present failed"); return; }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(m_context->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (vertex) failed");
        std::memcpy(mapped.pData, vertices.data(), sizeof(Vertex) * vertices.size());
        m_context->Unmap(m_vertexBuffer, 0);

        const UINT stride = sizeof(Vertex), offset = 0;
        const float blendFactor[4]{ 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_blendState, blendFactor, 0xffffffff);
        m_context->IASetInputLayout(m_inputLayout);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
        m_context->VSSetShader(m_vertexShader, nullptr, 0);
        m_context->PSSetShader(m_pixelShader, nullptr, 0);
        const Constants constants{ static_cast<float>(m_width), static_cast<float>(m_height), 0, 0 };
        m_context->UpdateSubresource(m_constantBuffer, 0, nullptr, &constants, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
        m_context->Draw(static_cast<UINT>(vertices.size()), 0);

        ThrowIfFailed(m_swapChain->Present(settings.verticalSync ? 1 : 0, 0), "Present failed");
    }
}
