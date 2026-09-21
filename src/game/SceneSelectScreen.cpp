#include "game/SceneSelectScreen.h"

#include <utility>

namespace engine::game
{
    std::unique_ptr<ui::Widget> BuildSceneSelectScreen(
        std::function<void()> onCircular,
        std::function<void()> onDefenseCombat,
        std::function<void()> onCharacterDemo,
        std::function<void()> onShadowShowcase,
        std::function<void()> onEffectsTest,
        std::function<void()> onSettings,
        std::function<void()> onQuit)
    {
        auto panel = std::make_unique<ui::UIWindow>();

        auto title = std::make_unique<ui::TextLine>("SELECT SCENE");
        title->SetBounds({ 16, 16, 0, 0 });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        // Rows stack top-down at a fixed cursor instead of hardcoded Y
        // offsets, so an empty callback (a scene not built into this
        // configuration) just skips its row and the rest re-flow upward -
        // e.g. a 2D-only build only ever passes onCircular + the tail rows.
        float y = 56.0f;
        constexpr float kRowHeight = 52.0f;   // 42 button + 10 gap
        const auto addButton = [&](const char* label, std::function<void()> onClick)
        {
            if (!onClick) return;
            auto button = std::make_unique<ui::Button>(label);
            button->SetBounds({ 16, y, 368, 42 });
            button->onClick = std::move(onClick);
            panel->AddChild(std::move(button));
            y += kRowHeight;
        };

        addButton("CIRCULAR", std::move(onCircular));
        addButton("DEFENSE COMBAT", std::move(onDefenseCombat));
        addButton("CHARACTER DEMO", std::move(onCharacterDemo));
        addButton("SHADOW SHOWCASE", std::move(onShadowShowcase));
        addButton("EFFECTS TEST", std::move(onEffectsTest));
        addButton("SETTINGS", std::move(onSettings));
        addButton("QUIT", std::move(onQuit));

        // Size to the rows actually added and centre in the 1280x720 design space.
        const float height = y + 6.0f;
        panel->SetBounds({ 440.0f, (720.0f - height) * 0.5f, 400.0f, height });
        return panel;
    }
}
