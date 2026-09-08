#pragma once

#include "core/NonCopyable.h"
#include "render/IRenderer.h"
#include "render/RenderPass.h"
#include "render/shader/ShaderLibrary.h"

#include <Windows.h>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct ID3D11DepthStencilView;

namespace engine::render
{
    // The render thread is the sole owner of all D3D11 work after Start().
    //
    // The renderer core only owns the device, swap chain, render target, and
    // depth buffer. What actually gets drawn each frame is a list of IRenderPass
    // objects run in order between the frame clear and Present. The default list
    // is MeshPass3D (3D, depth-tested) then QuadPass2D (2D overlay); call
    // AddRenderPass before Start() to append more.
    class Dx11Renderer final : public IRenderer, private core::NonCopyable
    {
    public:
        Dx11Renderer();
        ~Dx11Renderer() override;

        // Append a pass to the pipeline. Must be called before Start(); the pass
        // is Initialize()d on the render thread during Start().
        void AddRenderPass(std::unique_ptr<IRenderPass> pass);

        void Start(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight) override;
        void SetFrameSettings(FrameSettings settings) override;
        void Submit(RenderSnapshot snapshot) override;
        void Resize(std::uint32_t width, std::uint32_t height) override;
        void Stop() override;

    private:
        void RenderLoop(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight);
        void CreateDeviceAndSwapChain(HWND window, std::uint32_t width, std::uint32_t height);
        void CreateRenderTarget();
        void CreateDepthBuffer(std::uint32_t width, std::uint32_t height);
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

        std::vector<std::unique_ptr<IRenderPass>> m_passes;
        ShaderLibrary m_shaders;

        ID3D11Device* m_device{};
        ID3D11DeviceContext* m_context{};
        IDXGISwapChain* m_swapChain{};
        ID3D11RenderTargetView* m_renderTarget{};
        ID3D11Texture2D* m_depthTexture{};
        ID3D11DepthStencilView* m_depthStencil{};
        std::uint32_t m_width{};
        std::uint32_t m_height{};
    };
}
