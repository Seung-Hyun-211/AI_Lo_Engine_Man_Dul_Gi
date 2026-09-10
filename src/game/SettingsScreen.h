#pragma once

#include "core/Settings.h"
#include "ui/UI.h"

#include <functional>
#include <memory>

// The settings overlay: sound, mouse, and graphics options laid out with
// Slider/CheckBox/Button. See docs/game-settings.md for what each field means
// and which ones actually change engine behavior today.
namespace engine::game
{
    // Only the handful of fields with an immediate side effect outside the
    // Settings struct itself need a callback (vsync -> IRenderer,
    // resolution -> the OS window). Everything else, this function mutates
    // directly on `settings` (a reference into Application's long-lived
    // member - see BuildSettingsScreen's definition for why that's safe).
    struct SettingsScreenActions
    {
        std::function<void()> onVolumeChanged;   // any of master/music/sfx moved
        std::function<void()> onVsyncToggled;
        std::function<void()> onResolutionChanged;
        std::function<void()> onClose;
    };

    [[nodiscard]] std::unique_ptr<ui::Widget> BuildSettingsScreen(core::Settings& settings, SettingsScreenActions actions);
}
