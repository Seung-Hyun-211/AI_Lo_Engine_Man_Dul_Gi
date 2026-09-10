#pragma once

#include "ui/UI.h"

#include <functional>
#include <memory>

// Builds the widget tree for one screen/overlay. A free function returning
// std::unique_ptr<ui::Widget> keeps screen construction out of UIContext
// (which must stay ignorant of game concepts, docs/scene-flow-design.md) and
// out of Application's constructor body.
namespace engine::game
{
    // Font is the built-in 5x7 ASCII bitmap (ui/UI.cpp) - uppercase letters,
    // digits, `: - . %` only. Labels are English until a glyph atlas lands
    // (docs/ui-architecture.md).
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildTitleScreen(
        std::function<void()> onStart,
        std::function<void()> onOpenItems,
        std::function<void()> onOpenSettings,
        std::function<void()> onQuit);
}
