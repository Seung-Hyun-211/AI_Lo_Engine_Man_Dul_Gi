#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

// Shared identity primitive for anything a system needs to name as "this one
// instance" - independent of how that system stores the instance's data. See
// docs/entity-lifecycle-design.md for the two storage strategies (plain
// struct array vs. component table) this is meant to support side by side.
namespace engine::core
{
    // An opaque handle, not a pointer/index by itself: `index` names a slot in
    // whatever EntityRegistry issued it, `generation` invalidates the handle
    // once that slot is destroyed and recycled for a new entity. A stale
    // EntityId therefore reads as dead forever via EntityRegistry::IsAlive -
    // it never silently starts pointing at a different entity.
    struct EntityId
    {
        static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t index{ kInvalidIndex };
        std::uint32_t generation{};

        [[nodiscard]] bool IsValid() const { return index != kInvalidIndex; }
        friend bool operator==(const EntityId&, const EntityId&) = default;
    };

    // The default-constructed value; also what a "no entity" field should
    // hold instead of a sentinel index chosen ad hoc per call site.
    inline constexpr EntityId kInvalidEntity{};
}

// Lets EntityId key an unordered_map/unordered_set directly - needed by the
// component-table storage strategy (docs/entity-lifecycle-design.md §3).
template <>
struct std::hash<engine::core::EntityId>
{
    [[nodiscard]] std::size_t operator()(const engine::core::EntityId& id) const noexcept
    {
        return (static_cast<std::size_t>(id.index) << 32) ^ static_cast<std::size_t>(id.generation);
    }
};
