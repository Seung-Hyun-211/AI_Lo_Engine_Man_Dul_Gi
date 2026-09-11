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
    // is MeshPass3D (3D, depth-tested) then PostProcessPass (hands the frame off
    // from the scene colour target to the back buffer) then QuadPass2D (2D
    // overlay); call AddRenderPass before Start() to insert more geometry
    // stages, or append with atEnd for a pass that must run last.
    class Dx11Renderer final : public IRenderer, private core::NonCopyable
    {
    public:
        Dx11Renderer();
        ~Dx11Renderer() override;

        // Append a pass to the pipeline. Must be called before Start(); the pass
        // is Initialize()d on the render thread during Start(). By default the
        // pass slots in just before the trailing QuadPass2D overlay; pass
        // `atEnd = true` to append after everything (a pass that must run last).
        void AddRenderPass(std::unique_ptr<IRenderPass> pass, bool atEnd = false);

        void Start(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight) override;
        void SetFrameSettings(FrameSettings settings) override;
        void Submit(RenderSnapshot snapshot) override;
        void Resize(std::uint32_t width, std::uint32_t height) override;
        void Stop() override;

    private:
        void RenderLoop(HWND window, std::uint32_t initialWidth, std::uint32_t initialHeight);
        void CreateDeviceAndSwapChain(HWND window, std::uint32_t width, std::uint32_t height);
        void CreateBackBufferView();
        void CreateSceneTargets(std::uint32_t width, std::uint32_t height);
        void ReleaseSceneTargets();
        void CreateShadowResources();
        void ReleaseShadowResources();
        void RenderShadowMap(const RenderSnapshot& snapshot);
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

        // Back buffer: PostProcessPass's composite destination and, from then on
        // for the rest of the frame, what the 2D overlay draws into.
        ID3D11RenderTargetView* m_backBufferRtv{};

        // Scene targets the geometry stage renders into - always dedicated
        // textures (never aliases the back buffer, see CreateSceneTargets),
        // multisampled when m_sampleCount>1.
        ID3D11Texture2D* m_sceneColor{};
        ID3D11RenderTargetView* m_sceneColorRtv{};
        // Readable alongside the RTV (docs/post-process-gbuffer-research.md §12.3) -
        // PostProcessPass reads this to hand the frame off to the back buffer.
        ID3D11ShaderResourceView* m_sceneColorSrv{};
        ID3D11Texture2D* m_sceneDepth{};
        ID3D11DepthStencilView* m_sceneDepthDsv{};
        // Readable alongside the DSV (docs/post-process-gbuffer-research.md §4.1) -
        // nothing binds this yet; it exists so a later post-process pass can.
        ID3D11ShaderResourceView* m_sceneDepthSrv{};
        std::uint32_t m_sampleCount{ 1 };

        // Directional shadow map (single-sample). See docs/shadows.md.
        ID3D11Texture2D* m_shadowDepth{};
        ID3D11DepthStencilView* m_shadowDsv{};
        ID3D11ShaderResourceView* m_shadowSrv{};
        ID3D11Buffer* m_shadowFrameCb{};        // b0 for the depth-only pass: lightViewProj
        ID3D11SamplerState* m_shadowSampler{};
        ID3D11RasterizerState* m_shadowRaster{};
        ID3D11DepthStencilState* m_shadowDepthState{};

        std::uint32_t m_width{};
        std::uint32_t m_height{};
    };
}
