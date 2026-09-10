#include "game/Application.h"
#include "render/Dx11Renderer.h"
#include "render/r2d/SpritePass2D.h"
#if defined(ENGINE_WITH_3D)
#include "render/r3d/DebugDrawPass.h"
#include "render/r3d/ModelMeshPass3D.h"
#endif

#include <Windows.h>

#include <exception>
#include <memory>
#include <string>

// Entry point only. It picks the concrete renderer and hands it to Application
// through the render::IRenderer abstraction; everything else lives in the engine
// modules under src/. Swapping Dx11Renderer for a future Dx12Renderer is the
// only change this file would need.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try
    {
        engine::render::Dx11Renderer renderer;
        // The renderer ships with MeshPass3D + QuadPass2D. Extra stages slot in
        // before the 2D overlay.
#if defined(ENGINE_WITH_3D)
        renderer.AddRenderPass(std::make_unique<engine::render::ModelMeshPass3D>(
            "assets/models/unitychan/unitychan.fbx"));
        // Dev line visualisation (colliders, rays, skeletons) - over 3D, under UI.
        renderer.AddRenderPass(std::make_unique<engine::render::DebugDrawPass>());
#endif
        // Textured UI sprites + scissor clipping. Runs last (atEnd) so its
        // scissor rasterizer state does not leak into QuadPass2D.
        renderer.AddRenderPass(std::make_unique<engine::render::SpritePass2D>(
            "assets/atlas/ui.0.dds"), /*atEnd=*/true);
        engine::game::Application application(instance, renderer);
        return application.Run();
    }
    catch (const std::exception& error)
    {
        const std::string what = error.what();
        OutputDebugStringA(("Startup error: " + what + "\n").c_str());
        const std::wstring message =
            L"The engine could not start. Check that your graphics driver supports DirectX 11.\n\n"
            + std::wstring(what.begin(), what.end());
        MessageBoxW(nullptr, message.c_str(), L"Startup error", MB_ICONERROR);
        return 1;
    }
}
