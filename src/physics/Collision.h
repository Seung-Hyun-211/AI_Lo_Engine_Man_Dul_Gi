#pragma once

#include <cstdint>

// Shared vocabulary for the collision modules. The 2D module (physics/p2d) and
// the 3D module (physics/p3d) are peers - neither includes the other - and both
// build on the types here. Detection only; no response.
namespace engine::physics
{
    using ColliderId = std::uint32_t;
    inline constexpr ColliderId kInvalidCollider = 0;

    // Bitmask. A collider is on `layer` and tests against colliders whose layer
    // is in its `mask`. Two colliders are checked only when each is in the
    // other's mask.
    using CollisionLayer = std::uint32_t;
    inline constexpr CollisionLayer kAllLayers = 0xFFFFFFFFu;

    [[nodiscard]] inline bool LayersInteract(CollisionLayer layerA, CollisionLayer maskA,
                                             CollisionLayer layerB, CollisionLayer maskB)
    {
        return (maskA & layerB) != 0u && (maskB & layerA) != 0u;
    }

    // One overlapping pair found during a CollisionWorld step. `a < b` always.
    // The `user` values are copied from the colliders (typically an entity id)
    // so game code can react without an id->entity lookup.
    struct Contact
    {
        ColliderId a{};
        ColliderId b{};
        std::uint64_t userA{};
        std::uint64_t userB{};
    };
}
