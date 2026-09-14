#include "game/TitleScreen.h"

#include <utility>

namespace engine::game
{
    std::unique_ptr<ui::Widget> BuildTitleScreen(
        std::function<void()> onStart,
        std::function<void()> onOpenItems,
        std::function<void()> onOpenSettings,
        std::function<void()> onQuit)
    {
        auto panel = std::make_unique<ui::UIWindow>();
        panel->SetBounds({ 480, 232, 320, 288 });

        auto title = std::make_unique<ui::TextLine>("AI LO ENGINE");
        title->SetBounds({ 16, 16, 0, 0 });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        auto start = std::make_unique<ui::Button>("START");
        start->SetBounds({ 16, 56, 288, 42 });
        start->onClick = std::move(onStart);
        panel->AddChild(std::move(start));

        auto items = std::make_unique<ui::Button>("ITEMS");
        items->SetBounds({ 16, 108, 288, 42 });
        items->onClick = std::move(onOpenItems);
        panel->AddChild(std::move(items));

        auto settings = std::make_unique<ui::Button>("SETTINGS");
        settings->SetBounds({ 16, 160, 288, 42 });
        settings->onClick = std::move(onOpenSettings);
        panel->AddChild(std::move(settings));

        auto quit = std::make_unique<ui::Button>("QUIT");
        quit->SetBounds({ 16, 212, 288, 42 });
        quit->onClick = std::move(onQuit);
        panel->AddChild(std::move(quit));

        return panel;
    }

    std::unique_ptr<ui::Widget> BuildSceneSelectScreen(
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

        auto combat = std::make_unique<ui::Button>("DEFENSE COMBAT");
        combat->SetBounds({ 16, 56, 368, 42 });
        combat->onClick = std::move(onDefenseCombat);
        panel->AddChild(std::move(combat));

        auto character = std::make_unique<ui::Button>("CHARACTER DEMO");
        character->SetBounds({ 16, 108, 368, 42 });
        character->onClick = std::move(onCharacterDemo);
        panel->AddChild(std::move(character));

        auto shadow = std::make_unique<ui::Button>("SHADOW SHOWCASE");
        shadow->SetBounds({ 16, 160, 368, 42 });
        shadow->onClick = std::move(onShadowShowcase);
        panel->AddChild(std::move(shadow));

        auto effects = std::make_unique<ui::Button>("EFFECTS TEST");
        effects->SetBounds({ 16, 212, 368, 42 });
        effects->onClick = std::move(onEffectsTest);
        panel->AddChild(std::move(effects));

        auto back = std::make_unique<ui::Button>("BACK");
        back->SetBounds({ 16, 316, 368, 42 });
        back->onClick = std::move(onBack);
        panel->AddChild(std::move(back));

        return panel;
    }
}
