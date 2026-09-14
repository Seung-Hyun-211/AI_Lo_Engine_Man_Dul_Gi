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

    // "START" on the title screen leads here instead of straight into a fixed
    // scene - one button per runtime-selectable Simulation::DemoScene (docs/
    // demo-scene.md "씬 선택"), plus a way back to the title without picking
    // one. Kept as its own screen (not a 4th title button) so growing the
    // scene list later doesn't crowd the title panel.
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildSceneSelectScreen(
        std::function<void()> onDefenseCombat,
        std::function<void()> onCharacterDemo,
        std::function<void()> onShadowShowcase,
        std::function<void()> onEffectsTest,
        std::function<void()> onBack);
}
