#pragma once

#include "render/RenderSnapshot.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;

namespace engine::render
{
    class ShaderLibrary;

    // What a pass is handed each frame. The render target and depth buffer are
    // already created, cleared, and bound by Dx11Renderer; a pass only issues
    // draw calls and sets the pipeline state it needs. It must not Present,
    // resize, or release these objects.
    struct PassContext
    {
        ID3D11Device* device{};
        ID3D11DeviceContext* context{};
        // The scene colour target. During the geometry stage this is slot 0 of
        // two bound render targets - slot 1 is the view-space normal G-buffer,
        // which a pass writes by declaring SV_TARGET1 (or not, see
        // GeometryPSOut in common3d.hlsli) rather than by reading a field here.
        ID3D11RenderTargetView* renderTarget{};
        ID3D11DepthStencilView* depthStencil{};
        std::uint32_t viewportWidth{};
        std::uint32_t viewportHeight{};
        const RenderSnapshot* snapshot{};

        // Populated for the post-process stage only (PostProcessPass) - see
        // docs/post-process-gbuffer-research.md §12.1/§12.8. `backBufferRenderTarget`
        // is where that pass composites to and, from then on, what every pass
        // after it (the 2D overlay) actually draws into via the D3D11 pipeline
        // state PostProcessPass leaves bound - `renderTarget` above stays the
        // scene colour target throughout, unchanged, since it is set once for
        // the whole frame. Every pass other than PostProcessPass ignores these.
        ID3D11RenderTargetView* backBufferRenderTarget{};
        ID3D11ShaderResourceView* sceneColorSrv{};
        // View-space normal G-buffer (docs/post-process-gbuffer-research.md
        // §12.5) - the geometry stage's PS writes it as a second render target
        // alongside colour; nothing reads this SRV yet (SSAO lands here later).
        ID3D11ShaderResourceView* sceneNormalSrv{};
        std::uint32_t sceneSampleCount{ 1 };
    };

    // Handed to RenderShadow(). The renderer has already bound the shadow depth
    // target, the viewport, and the light view-projection at b0. A casting pass
    // only sets its per-object world at b1 and issues depth-only draws.
    struct ShadowContext
    {
        ID3D11DeviceContext* context{};
        const RenderSnapshot* snapshot{};
    };

    // One stage of the render pipeline. Passes run on the render thread in the
    // order they were added to the renderer, between the frame clear and Present.
    //
    // This is the extension point for the pipeline: add a bloom pass, a shadow
    // pass, a debug-line pass, a post-process pass, etc. by implementing this
    // interface and calling Dx11Renderer::AddRenderPass before Start(). Existing
    // passes and the renderer core are not touched (OCP).
    //
    // The interface is D3D11-specific on purpose - a pass issues backend draw
    // calls. A future DX12 renderer would define its own pass contract; the
    // backend-neutral seam is render::IRenderer, not this.
    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;

        // For logs and profiler labels. Return a string literal.
        [[nodiscard]] virtual const char* Name() const = 0;

        // Called once on the render thread after the device exists, before the
        // first Execute. Get shader programs from `shaders` (by .hlsl name) and
        // create buffers / pipeline states here.
        virtual void Initialize(ID3D11Device* device, ShaderLibrary& shaders) = 0;

        // Called every frame. Read ctx.snapshot for what to draw.
        virtual void Execute(const PassContext& context) = 0;

        // Optional: draw this pass's geometry depth-only into the shadow map,
        // from the light's point of view. Default casts nothing.
        virtual void RenderShadow(const ShadowContext& context) { (void)context; }

        // Called on renderer shutdown. Release every device object created in
        // Initialize. Safe to call when Initialize never ran.
        virtual void Release() = 0;
    };
}
