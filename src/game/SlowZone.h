#pragma once

#if defined(ENGINE_WITH_3D)

#include "math/Math3D.h"

// A placed barbed-wire patch (docs/defense-combat-design.md §6) - pure crowd
// control, no damage. Plain value type in a plain std::vector (not an
// ObjectPool): placed zones are never removed mid-match, so there is nothing
// to Release - see that doc's own "새 콜라이더 타입도 필요 없다" call.
namespace engine::game
{
    struct SlowZone
    {
        math::Vec3 center{};
        float      radius{ 0.0f };
        float      speedMul{ 1.0f };   // multiplies an agent's speed while inside; min() across overlapping zones
    };
}

#endif
