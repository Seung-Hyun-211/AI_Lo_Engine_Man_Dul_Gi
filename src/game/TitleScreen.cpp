#include "game/TitleScreen.h"

#include <utility>

namespace engine::game
{
    std::unique_ptr<ui::Widget> BuildTitleScreen(
        std::function<void()> onStart,
        std::function<void()> onOpenSettings,
        std::function<void()> onQuit)
    {
        auto panel = std::make_unique<ui::UIWindow>();
        panel->SetBounds({ 480, 232, 320, 236 });

        auto title = std::make_unique<ui::TextLine>("AI LO ENGINE");
        title->SetBounds({ 16, 16, 0, 0 });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        auto start = std::make_unique<ui::Button>("START");
        start->SetBounds({ 16, 56, 288, 42 });
        start->onClick = std::move(onStart);
        panel->AddChild(std::move(start));

        auto settings = std::make_unique<ui::Button>("SETTINGS");
        settings->SetBounds({ 16, 108, 288, 42 });
        settings->onClick = std::move(onOpenSettings);
        panel->AddChild(std::move(settings));

        auto quit = std::make_unique<ui::Button>("QUIT");
        quit->SetBounds({ 16, 160, 288, 42 });
        quit->onClick = std::move(onQuit);
        panel->AddChild(std::move(quit));

        return panel;
    }

    std::unique_ptr<ui::Widget> BuildSceneSelectScreen(
        std::function<void()> onCircular,
        std::function<void()> onDefenseCombat,
        std::function<void()> onCharacterDemo,
        std::function<void()> onShadowShowcase,
        std::function<void()> onEffectsTest,
        std::function<void()> onBack)
    {
        auto panel = std::make_unique<ui::UIWindow>();
        panel->SetBounds({ 440, 170, 400, 400 });

        auto title = std::make_unique<ui::TextLine>("SELECT SCENE");
        title->SetBounds({ 16, 16, 0, 0 });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        // Rows stack top-down at a fixed cursor instead of hardcoded Y
        // offsets, so an empty callback (a scene not built into this
        // configuration) just skips its row and the rest re-flow upward -
        // e.g. a 2D-only build only ever passes onCircular and onBack.
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
        addButton("BACK", std::move(onBack));

        return panel;
    }
}
