#include "game/Application.h"
#include "render/Dx11Renderer.h"

#include <Windows.h>

#include <exception>
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
        // The renderer ships with MeshPass3D + QuadPass2D. Add more stages here
        // before Run(), e.g.:
        //   renderer.AddRenderPass(std::make_unique<engine::render::DebugLinePass>());
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
