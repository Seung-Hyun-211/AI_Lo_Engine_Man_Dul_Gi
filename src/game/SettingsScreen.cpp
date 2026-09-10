#include "game/SettingsScreen.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace engine::game
{
    namespace
    {
        [[nodiscard]] std::string FormatPercent(float v01)
        {
            return std::to_string(static_cast<int>(std::lround(v01 * 100.0f))) + "%";
        }

        [[nodiscard]] std::string FormatSensitivity(float v)
        {
            const int tenths = std::max(0, static_cast<int>(std::lround(v * 10.0f)));
            return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "X";
        }

        [[nodiscard]] std::string FormatResolution(core::Resolution r)
        {
            return std::to_string(r.width) + "X" + std::to_string(r.height);
        }

        // One "label ... value" line with a Slider underneath it. Wires the
        // slider's onChanged to both refresh the value label and forward the
        // new value to the caller (which mutates the Settings field it owns).
        void AddSliderRow(ui::Widget& panel, float y, const char* label,
                          float minValue, float maxValue, float initial,
                          std::function<std::string(float)> formatter,
                          std::function<void(float)> onChanged)
        {
            auto labelText = std::make_unique<ui::TextLine>(label);
            labelText->SetBounds({ 16, y, 0, 0 });
            labelText->pixelScale = 2.0f;
            panel.AddChild(std::move(labelText));

            auto valueText = std::make_unique<ui::TextLine>(formatter(initial));
            valueText->SetBounds({ 420, y, 0, 0 });
            valueText->pixelScale = 2.0f;
            ui::TextLine* valueLabel = valueText.get();
            panel.AddChild(std::move(valueText));

            auto slider = std::make_unique<ui::Slider>(minValue, maxValue);
            slider->SetBounds({ 16, y + 24, 528, 20 });
            slider->value = initial;
            slider->onChanged = [valueLabel, formatter = std::move(formatter), onChanged = std::move(onChanged)](float v)
            {
                valueLabel->SetText(formatter(v));
                onChanged(v);
            };
            panel.AddChild(std::move(slider));
        }
    }

    std::unique_ptr<ui::Widget> BuildSettingsScreen(core::Settings& settings, SettingsScreenActions actions)
    {
        // Every callback below captures `settings` by reference. That's only
        // safe because the caller (Application) owns `settings` as a member
        // that outlives this widget tree - the tree is torn down (ClearOverlay)
        // well before Application itself is. Do not call this with a Settings
        // that might not outlive the returned widgets.
        auto panel = std::make_unique<ui::UIWindow>();
        panel->SetBounds({ 360, 90, 560, 540 });

        auto title = std::make_unique<ui::TextLine>("SETTINGS");
        title->SetBounds({ 16, 16, 0, 0 });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        const auto onVolume = actions.onVolumeChanged;
        float y = 60.0f;
        AddSliderRow(*panel, y, "MASTER VOLUME", 0.0f, 1.0f, settings.masterVolume, FormatPercent,
                     [&settings, onVolume](float v) { settings.masterVolume = v; if (onVolume) onVolume(); });
        y += 60.0f;
        AddSliderRow(*panel, y, "MUSIC VOLUME", 0.0f, 1.0f, settings.musicVolume, FormatPercent,
                     [&settings, onVolume](float v) { settings.musicVolume = v; if (onVolume) onVolume(); });
        y += 60.0f;
        AddSliderRow(*panel, y, "SFX VOLUME", 0.0f, 1.0f, settings.sfxVolume, FormatPercent,
                     [&settings, onVolume](float v) { settings.sfxVolume = v; if (onVolume) onVolume(); });
        y += 60.0f;
        AddSliderRow(*panel, y, "MOUSE SENSITIVITY", 0.1f, 3.0f, settings.mouseSensitivity, FormatSensitivity,
                     [&settings](float v) { settings.mouseSensitivity = v; });
        y += 60.0f;

        auto invertY = std::make_unique<ui::CheckBox>("INVERT MOUSE Y");
        invertY->SetBounds({ 16, y, 400, 28 });
        invertY->checked = settings.invertMouseY;
        invertY->onChanged = [&settings](bool checked) { settings.invertMouseY = checked; };
        panel->AddChild(std::move(invertY));
        y += 40.0f;

        auto vsync = std::make_unique<ui::CheckBox>("VSYNC");
        vsync->SetBounds({ 16, y, 400, 28 });
        vsync->checked = settings.vsync;
        vsync->onChanged = [&settings, onVsyncToggled = actions.onVsyncToggled](bool checked)
        {
            settings.vsync = checked;
            if (onVsyncToggled) onVsyncToggled();
        };
        panel->AddChild(std::move(vsync));
        y += 50.0f;

        auto resolutionLabel = std::make_unique<ui::TextLine>("RESOLUTION");
        resolutionLabel->SetBounds({ 16, y, 0, 0 });
        resolutionLabel->pixelScale = 2.0f;
        panel->AddChild(std::move(resolutionLabel));

        auto resolutionValue = std::make_unique<ui::TextLine>(FormatResolution(
            core::kResolutionPresets[static_cast<std::size_t>(settings.resolutionIndex)]));
        resolutionValue->SetBounds({ 260, y, 0, 0 });
        resolutionValue->pixelScale = 2.0f;
        ui::TextLine* resolutionValueLabel = resolutionValue.get();
        panel->AddChild(std::move(resolutionValue));
        y += 24.0f;

        auto cycleResolution = [&settings, resolutionValueLabel, onResolutionChanged = actions.onResolutionChanged](int step)
        {
            const int count = static_cast<int>(core::kResolutionPresets.size());
            settings.resolutionIndex = ((settings.resolutionIndex + step) % count + count) % count;
            resolutionValueLabel->SetText(FormatResolution(
                core::kResolutionPresets[static_cast<std::size_t>(settings.resolutionIndex)]));
            if (onResolutionChanged) onResolutionChanged();
        };

        auto prev = std::make_unique<ui::Button>("PREV");
        prev->SetBounds({ 16, y, 100, 36 });
        prev->onClick = [cycleResolution] { cycleResolution(-1); };
        panel->AddChild(std::move(prev));

        auto next = std::make_unique<ui::Button>("NEXT");
        next->SetBounds({ 444, y, 100, 36 });
        next->onClick = [cycleResolution] { cycleResolution(1); };
        panel->AddChild(std::move(next));
        y += 50.0f;

        auto close = std::make_unique<ui::Button>("CLOSE");
        close->SetBounds({ 16, y, 528, 44 });
        close->onClick = std::move(actions.onClose);
        panel->AddChild(std::move(close));

        return panel;
    }
}
