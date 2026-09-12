#pragma once

#if defined(ENGINE_WITH_3D)

#include "math/Math3D.h"

#include <cstdint>

// A placed mortar shell or mine (docs/defense-combat-design.md §4). Both are
// "wait, then call Simulation::TriggerExplosion" - the only difference is
// what starts the countdown (a timer vs. a proximity check), so they share
// one struct instead of two.
namespace engine::game
{
    enum class OrdnanceKind : std::uint8_t { Mortar, Mine };

    struct PlacedOrdnance
    {
        OrdnanceKind kind{ OrdnanceKind::Mortar };
        math::Vec3   pos{};
        float        fuseSeconds{ 0.0f };   // mortar: counts down to 0. mine: unused (proximity trigger instead)
        float        radius{ 0.0f };        // also the mine's trigger radius - one "R" per §4
        float        power{ 0.0f };
        float        damage{ 0.0f };
        // Mirrors the Handle Acquire() returned for this slot, same reason as
        // GibPiece::selfIndex/selfGeneration (Simulation.h) - lets StepOrdnance
        // Release() itself without a second parallel handle list.
        std::uint32_t selfIndex{ 0 }, selfGeneration{ 0 };

        void Reset() { *this = PlacedOrdnance{}; }
    };
}

#endif
