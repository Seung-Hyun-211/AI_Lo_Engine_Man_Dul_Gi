#pragma once

#include "ui/UI.h"

#include <functional>
#include <memory>

// The in-game screen's UI: currently just the way back into Settings. Grows
// into a real HUD (health, score, ...) alongside actual gameplay - kept
// separate from TitleScreen/SettingsScreen so each screen's reason to change
// stays its own (SRP).
namespace engine::game
{
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildInGameHud(std::function<void()> onOpenSettings);
}
