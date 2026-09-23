#include "game/LobbyScreen.h"

#include <utility>

namespace engine::game
{
    std::unique_ptr<ui::Widget> BuildLobbyScreen(
        std::function<void()> onGame,
        std::function<void()> onTestScene)
    {
        auto panel = std::make_unique<ui::UIWindow>();

        auto title = std::make_unique<ui::TextLine>("LOBBY");
        title->SetBounds({ 16, 16, 0, 0 });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        constexpr float kRowHeight = 52.0f;   // 42 button + 10 gap
        float y = 56.0f;
        const auto addButton = [&](const char* label, std::function<void()> onClick)
        {
            auto button = std::make_unique<ui::Button>(label);
            button->SetBounds({ 16, y, 368, 42 });
            button->onClick = std::move(onClick);
            panel->AddChild(std::move(button));
            y += kRowHeight;
        };

        addButton("GAME", std::move(onGame));
        addButton("TEST SCENE", std::move(onTestScene));

        const float height = y + 6.0f;
        panel->SetBounds({ 440.0f, (720.0f - height) * 0.5f, 400.0f, height });
        return panel;
    }
}
