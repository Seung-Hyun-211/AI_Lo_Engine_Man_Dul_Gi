#include "game/LevelUpScreen.h"

#include <utility>

namespace engine::game
{
    std::unique_ptr<ui::Widget> BuildLevelUpScreen(
        const std::vector<std::string>& labels,
        std::function<void(std::size_t)> onChoose)
    {
        constexpr float kButtonHeight = 52.0f;
        constexpr float kButtonGap = 12.0f;
        constexpr float kHeader = 56.0f;
        constexpr float kPanelWidth = 440.0f;
        const float panelHeight = kHeader + static_cast<float>(labels.size()) * (kButtonHeight + kButtonGap) + 8.0f;

        auto panel = std::make_unique<ui::UIWindow>();
        // Same fixed-layout convention as the other screens (1280x720 design space).
        panel->SetBounds({ (1280.0f - kPanelWidth) * 0.5f, (720.0f - panelHeight) * 0.5f, kPanelWidth, panelHeight });

        auto title = std::make_unique<ui::TextLine>("LEVEL UP - PICK ONE");
        title->SetBounds({ 16.0f, 16.0f, 0.0f, 0.0f });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        float y = kHeader;
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            auto button = std::make_unique<ui::Button>(labels[i]);
            button->SetBounds({ 16.0f, y, kPanelWidth - 32.0f, kButtonHeight });
            button->onClick = [onChoose, i] { onChoose(i); };
            panel->AddChild(std::move(button));
            y += kButtonHeight + kButtonGap;
        }
        return panel;
    }
}
