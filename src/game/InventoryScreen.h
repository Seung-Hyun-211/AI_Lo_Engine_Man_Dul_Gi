#pragma once

#include "ui/UI.h"

#include <functional>
#include <memory>

// A demo screen for ui::ScrollList: a long, wheel-scrollable list of coloured
// items. A free function returning ui::Widget keeps screen construction out of
// UIContext (docs/scene-flow-design.md), same as TitleScreen / InGameHud.
namespace engine::game
{
    // `onBack` returns to the title screen. The item data (names + per-category
    // colours) is generated and owned inside the screen's ListModel.
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildInventoryScreen(std::function<void()> onBack);
}
