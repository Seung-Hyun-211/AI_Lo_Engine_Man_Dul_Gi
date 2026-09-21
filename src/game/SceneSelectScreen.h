#pragma once

#include "ui/UI.h"

#include <functional>
#include <memory>

// Builds the widget tree for the scene-select menu. A free function returning
// std::unique_ptr<ui::Widget> keeps screen construction out of UIContext
// (which must stay ignorant of game concepts, docs/scene-flow-design.md) and
// out of Application's constructor body.
//
// There is no title screen: the app boots straight into the Circular scene
// (Application::Run) and this menu is reached from Settings -> "SCENE SELECT"
// (or after a DefenseCombat loss). It doubles as the app's only "main menu".
namespace engine::game
{
    // Font is the built-in 5x7 ASCII bitmap (ui/UI.cpp) - uppercase letters,
    // digits, `: - . %` only. Labels are English until a glyph atlas lands
    // (docs/ui-architecture.md).
    //
    // One button per runtime-selectable Simulation::DemoScene (docs/demo-scene.md
    // "씬 선택"), then SETTINGS and QUIT.
    //
    // Circular (docs/circular-design.md) is 2D-baseline and always shown; the
    // other four need ENGINE_WITH_3D. An empty (default-constructed)
    // std::function for any of onDefenseCombat/onCharacterDemo/
    // onShadowShowcase/onEffectsTest skips that button entirely (OCP - this
    // function does not itself know about ENGINE_WITH_3D, the caller decides
    // by which callbacks it passes) and the remaining rows re-flow upward, so
    // a 2D-only build's menu is just CIRCULAR + SETTINGS + QUIT. The
    // panel height follows the row count and is centred in the 1280x720
    // design space.
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildSceneSelectScreen(
        std::function<void()> onCircular,
        std::function<void()> onDefenseCombat,
        std::function<void()> onCharacterDemo,
        std::function<void()> onShadowShowcase,
        std::function<void()> onEffectsTest,
        std::function<void()> onSettings,
        std::function<void()> onQuit);
}
