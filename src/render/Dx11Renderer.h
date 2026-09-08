#pragma once

#include "core/NonCopyable.h"
#include "render/IRenderer.h"

#include <Windows.h>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11BlendState;

namespace engine::render
{
    // The render thread is the sole owner of all D3D11 work after Start().
    class Dx11Renderer final : public IRenderer, private core::NonCopyable
    {
    public:
        Dx11Renderer() = default;
        ~Dx11Renderer() override;

        void Start(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight) override;
        void SetFrameSettings(FrameSettings settings) override;
        void Submit(RenderSnapshot snapshot) override;
        void Resize(std::uint32_t width, std::uint32_t height) override;
        void Stop() override;

    private:
        void RenderLoop(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight);
        void CreateDeviceAndSwapChain(HWND window, std::uint32_t width, std::uint32_t height);
        void CreateRenderTarget();
        void CreateSpritePipeline();
        void ResizeBackBuffer(std::uint32_t width, std::uint32_t height);
        void Render(const RenderSnapshot& snapshot, const FrameSettings& settings);

        std::thread m_thread;
        std::mutex m_mutex;
        std::condition_variable m_workReady;
        std::condition_variable m_started;
        bool m_running{};
        bool m_initialized{};
        std::exception_ptr m_startError;
        // A one-slot mailbox prevents rendering lag from building a frame queue.
        std::optional<RenderSnapshot> m_pendingSnapshot;
        std::optional<SIZE> m_pendingResize;
        FrameSettings m_frameSettings{};

        ID3D11Device* m_device{};
        ID3D11DeviceContext* m_context{};
        IDXGISwapChain* m_swapChain{};
        ID3D11RenderTargetView* m_renderTarget{};
        ID3D11VertexShader* m_vertexShader{};
        ID3D11PixelShader* m_pixelShader{};
        ID3D11InputLayout* m_inputLayout{};
        ID3D11Buffer* m_vertexBuffer{};
        ID3D11Buffer* m_constantBuffer{};
        ID3D11BlendState* m_blendState{};
        std::uint32_t m_width{};
        std::uint32_t m_height{};
    };
}
