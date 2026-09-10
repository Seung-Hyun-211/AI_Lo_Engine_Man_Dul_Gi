#include "game/InventoryScreen.h"

#include "ui/ScrollList.h"

#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace engine::game
{
    namespace
    {
        // Eight item "categories", each a flat colour. If a real texture atlas
        // for UI lands (docs/texture-atlas-and-sprite-pass.md) these become
        // 64x64 sprites; until then the row background IS the item swatch.
        constexpr std::array<ui::Color, 8> kCategoryColors{ {
            { 0.62f, 0.20f, 0.22f, 1.0f },   // ember
            { 0.20f, 0.44f, 0.60f, 1.0f },   // steel
            { 0.24f, 0.52f, 0.34f, 1.0f },   // moss
            { 0.58f, 0.46f, 0.20f, 1.0f },   // brass
            { 0.42f, 0.28f, 0.56f, 1.0f },   // amethyst
            { 0.58f, 0.36f, 0.24f, 1.0f },   // clay
            { 0.24f, 0.50f, 0.54f, 1.0f },   // teal
            { 0.46f, 0.46f, 0.50f, 1.0f },   // stone
        } };

        constexpr std::array<const char*, 8> kCategoryNames{ {
            "EMBER", "STEEL", "MOSS", "BRASS", "AMETHYST", "CLAY", "TEAL", "STONE",
        } };

        // Owns its rows (names + colour index). Generated once here; BindRow only
        // references it, so a viewport frame does no allocation.
        class DemoItemModel final : public ui::ListModel
        {
        public:
            explicit DemoItemModel(std::size_t count)
            {
                m_items.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t cat = i % kCategoryColors.size();
                    char buffer[48];
                    std::snprintf(buffer, sizeof(buffer), "ITEM %03zu  %s", i, kCategoryNames[cat]);
                    m_items.push_back({ buffer, cat });
                }
            }

            std::size_t Count() const override { return m_items.size(); }

            void BindRow(ui::RowView& row, std::size_t index) const override
            {
                const Item& item = m_items[index];
                row.SetText(item.label);
                ui::Color tint = kCategoryColors[item.category];
                // Slightly darken every other row so the list reads as rows even
                // within one category run.
                if (index & 1) { tint.r *= 0.82f; tint.g *= 0.82f; tint.b *= 0.82f; }
                row.SetTint(tint);
            }

        private:
            struct Item { std::string label; std::size_t category; };
            std::vector<Item> m_items;
        };
    }

    std::unique_ptr<ui::Widget> BuildInventoryScreen(std::function<void()> onBack)
    {
        auto panel = std::make_unique<ui::UIWindow>();
        panel->SetBounds({ 400.0f, 120.0f, 480.0f, 480.0f });

        auto title = std::make_unique<ui::TextLine>("ITEMS - WHEEL TO SCROLL");
        title->SetBounds({ 16.0f, 14.0f, 0.0f, 0.0f });
        title->pixelScale = 2.0f;
        panel->AddChild(std::move(title));

        auto list = std::make_unique<ui::ScrollList>();
        list->SetBounds({ 16.0f, 44.0f, 448.0f, 380.0f });   // = viewport (incl. scrollbar strip)
        list->SetRowHeight(30.0f);
        list->SetModel(std::make_unique<DemoItemModel>(200));
        list->onSelect = [](std::size_t) {};   // selection highlight only, for now
        panel->AddChild(std::move(list));

        auto back = std::make_unique<ui::Button>("BACK");
        back->SetBounds({ 16.0f, 432.0f, 448.0f, 36.0f });
        back->onClick = std::move(onBack);
        panel->AddChild(std::move(back));

        return panel;
    }
}
