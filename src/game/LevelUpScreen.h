#pragma once

#include "ui/UI.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The Circular scene's level-up modal (docs/circular-design.md §1/§6): a
// panel with one button per option. Installed via UIContext::SetOverlay, so
// the world underneath is frozen and only these buttons take input.
namespace engine::game
{
    // `labels` = one button each (font: A-Z 0-9 : - . %). `onChoose(i)` fires
    // for the clicked option's index - the caller must NOT clear the overlay
    // from inside it (that destroys the very button that is calling); record
    // the pick and resolve it after event handling (Application does this).
    // `title` lets the character-select modal reuse the same layout.
    [[nodiscard]] std::unique_ptr<ui::Widget> BuildLevelUpScreen(
        const std::vector<std::string>& labels,
        std::function<void(std::size_t)> onChoose,
        const std::string& title = "LEVEL UP - PICK ONE");
}
