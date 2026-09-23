#pragma once

#include "ui/UI.h"

#include <functional>
#include <memory>

// Builds the widget tree for the lobby menu. A free function returning
// std::unique_ptr<ui::Widget> keeps screen construction out of UIContext
// (which must stay ignorant of game concepts, docs/scene-flow-design.md) and
// out of Application's constructor body.
//
// There is no title screen: the app boots straight into the Circular scene
// (Application::Run) and this menu is reached from Settings -> "LOBBY" (or
// after a DefenseCombat loss). It doubles as the app's only "main menu".
//
// Only two paths on purpose (user request, 2026-09): GAME (the normal
// Circular run) and TEST SCENE (Simulation::EnterCircularTestScene - one
// 99999-HP dummy mob, no swarm/charge pattern, for isolating weapon damage/
// behaviour). The other demo/test scenes this app used to list here
// (DefenseCombat, CharacterDemo, ShadowShowcase, EffectsTest) and the
// Settings/Quit rows that used to live on this screen are gone from this
// menu - their code is untouched, just not reachable from here. Settings is
// still reachable via ESC once in a scene; quitting is the window's own
// close control.
namespace engine::game
{
    // Font is the built-in 5x7 ASCII bitmap (ui/UI.cpp) - uppercase letters,
    // digits, `: - . %` only. Labels are English until a glyph atlas lands
    // (docs/ui-architecture.md).
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildLobbyScreen(
        std::function<void()> onGame,
        std::function<void()> onTestScene);
}
