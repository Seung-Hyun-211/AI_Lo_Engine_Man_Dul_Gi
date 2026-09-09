#include "game/InGameHud.h"

#include <utility>

namespace engine::game
{
    std::unique_ptr<ui::Widget> BuildInGameHud(std::function<void()> onOpenSettings)
    {
        auto panel = std::make_unique<ui::UIWindow>();
        panel->SetBounds({ 20, 20, 168, 56 });
        panel->background = { 0.10f, 0.13f, 0.20f, 0.80f };

        auto settings = std::make_unique<ui::Button>("SETTINGS");
        settings->SetBounds({ 8, 8, 152, 40 });
        settings->onClick = std::move(onOpenSettings);
        panel->AddChild(std::move(settings));

        return panel;
    }
}
