#pragma once

#include <array>
#include <cstdint>
#include <string>

// User-configurable options: one value struct, loaded once at startup and
// saved back to disk when the settings screen closes. See
// docs/game-settings.md for the catalog (defaults, ranges, what each field
// actually does today vs. is stored for a future subsystem).
namespace engine::core
{
    struct Resolution
    {
        int width;
        int height;
    };

    // Fixed candidate list rather than an arbitrary slider: the window/renderer
    // resize path (Win32Window::RequestResize -> IWindowEventSink::OnResize ->
    // IRenderer::Resize) works for any size, but a game only needs a short,
    // known-good list in the UI. Settings::resolutionIndex indexes this array.
    inline constexpr std::array<Resolution, 3> kResolutionPresets{ {
        { 1280, 720 },
        { 1600, 900 },
        { 1920, 1080 },
    } };

    // Frame rate cap candidates - same fixed-list-not-slider reasoning as
    // kResolutionPresets. The render thread's own pacing (Dx11Renderer) only
    // applies this when !verticalSync (see FrameSettings) - vsync already
    // paces its Present to the display. Application::Run's main-loop pacing
    // applies it unconditionally though (docs/game-settings.md "카운터가 캡을
    // 넘던 문제"), so the counter/input-sim rate stays under this cap either
    // way. Stored/cycled independently of the vsync checkbox so the choice
    // sticks once vsync is toggled off. Settings::frameRateIndex indexes this
    // array.
    inline constexpr std::array<std::uint32_t, 3> kFrameRatePresets{ 60, 120, 144 };

    inline constexpr const char* kSettingsFilePath = "settings.cfg";

    struct Settings
    {
        // 0..1. Stored only - no audio subsystem exists yet to apply these to.
        float masterVolume{ 1.0f };
        float musicVolume{ 1.0f };
        float sfxVolume{ 1.0f };

        // 0.1..3.0 / bool. Stored only - a mouse-driven camera exists now
        // (Simulation::UpdateCameraLook), but it still uses its own hardcoded
        // kMouseSensitivity and never reads these (docs/game-settings.md §1).
        float mouseSensitivity{ 1.0f };
        bool invertMouseY{ false };

        // Applied immediately: Application::ApplyFrameSettings -> IRenderer::SetFrameSettings.
        bool vsync{ true };

        // Applied immediately: Application::ApplyResolution -> Win32Window::RequestResize.
        // Index into kResolutionPresets; out-of-range values from a stale/hand-
        // edited file are clamped back in LoadOrDefault.
        int resolutionIndex{ 1 };

        // Applied immediately: Application::ApplyFrameSettings -> IRenderer::
        // SetFrameSettings (alongside vsync above - same call). Index into
        // kFrameRatePresets; out-of-range values clamped back in LoadOrDefault.
        int frameRateIndex{ 0 };   // 60fps by default

        // Reads kSettingsFilePath-shaped `key=value` lines; a missing file or a
        // field that fails to parse just keeps that field's default. Never
        // throws - a corrupt settings file should never block startup.
        [[nodiscard]] static Settings LoadOrDefault(const std::string& path);

        // Overwrites `path` with the current values. Silently does nothing if
        // the file can't be opened for writing (e.g. read-only directory) -
        // settings still work for this run, just won't persist.
        void Save(const std::string& path) const;
    };
}
