#include "render/Dx11Renderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    struct Vertex { float x, y, r, g, b, a; };
    struct Constants { float screenWidth, screenHeight, unused0, unused1; };
    constexpr std::size_t kMaxVertices = 65'536;

    void AddQuad(std::vector<Vertex>& vertices, const engine::render::Quad& quad)
    {
        const Vertex topLeft{ quad.x, quad.y, quad.r, quad.g, quad.b, quad.a };
        const Vertex topRight{ quad.x + quad.width, quad.y, quad.r, quad.g, quad.b, quad.a };
        const Vertex bottomLeft{ quad.x, quad.y + quad.height, quad.r, quad.g, quad.b, quad.a };
        const Vertex bottomRight{ quad.x + quad.width, quad.y + quad.height, quad.r, quad.g, quad.b, quad.a };
        vertices.insert(vertices.end(), { topLeft, topRight, bottomLeft, bottomLeft, topRight, bottomRight });
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
                {
                    std::unique_lock lock(m_mutex);
                    m_workReady.wait(lock, [this] { return !m_running || m_pendingSnapshot || m_pendingResize; });
                    if (!m_running) break;
                    snapshot = std::move(m_pendingSnapshot);
                    m_pendingSnapshot.reset();
                    resize = m_pendingResize;
                    m_pendingResize.reset();
                }
                if (resize) ResizeBackBuffer(static_cast<std::uint32_t>(resize->cx), static_cast<std::uint32_t>(resize->cy));
                if (snapshot)
                {
                    FrameSettings settings;
                    {
                        std::scoped_lock lock(m_mutex);
                        settings = m_frameSettings;
                    }
                    Render(*snapshot);

                    // Present(1) is driven by the display refresh. Software
                    // capping only applies when VSync is off.
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
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ThrowIfFailed(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION, &description, &m_swapChain, &m_device, nullptr, &m_context),
            "D3D11CreateDeviceAndSwapChain failed");
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
    }

    void Dx11Renderer::ResizeBackBuffer(std::uint32_t width, std::uint32_t height)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        Release(m_renderTarget);
        ThrowIfFailed(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers failed");
        m_width = width; m_height = height;
        CreateRenderTarget();
    }

    void Dx11Renderer::Render(const RenderSnapshot& snapshot)
    {
        m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_context->ClearRenderTargetView(m_renderTarget, snapshot.clearColor);
        const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1 };
        m_context->RSSetViewports(1, &viewport);
        std::vector<Vertex> vertices;
        vertices.reserve(6 * (snapshot.uiQuads.size() + 1));
        AddQuad(vertices, { snapshot.playerX, snapshot.playerY, 64, 64, .20f, .75f, 1, 1 });
        for (const Quad& quad : snapshot.uiQuads)
        {
            if (vertices.size() + 6 > kMaxVertices) break;
            AddQuad(vertices, quad);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(m_context->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (vertex) failed");
        std::memcpy(mapped.pData, vertices.data(), sizeof(Vertex) * vertices.size());
        m_context->Unmap(m_vertexBuffer, 0);

        const UINT stride = sizeof(Vertex), offset = 0;
        m_context->IASetInputLayout(m_inputLayout);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
        m_context->VSSetShader(m_vertexShader, nullptr, 0);
        m_context->PSSetShader(m_pixelShader, nullptr, 0);
        const Constants constants{ static_cast<float>(m_width), static_cast<float>(m_height), 0, 0 };
        m_context->UpdateSubresource(m_constantBuffer, 0, nullptr, &constants, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
        m_context->Draw(static_cast<UINT>(vertices.size()), 0);
        FrameSettings settings;
        {
            std::scoped_lock lock(m_mutex);
            settings = m_frameSettings;
        }
        ThrowIfFailed(m_swapChain->Present(settings.verticalSync ? 1 : 0, 0), "Present failed");
    }
}
