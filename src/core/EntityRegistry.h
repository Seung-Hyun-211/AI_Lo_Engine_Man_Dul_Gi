#pragma once

#include "core/EntityId.h"
#include "core/NonCopyable.h"

#include <cstdint>
#include <vector>

namespace engine::core
{
    // Owns entity IDENTITY only - who currently exists, and recycling freed
    // slots (generation bump, so a stale EntityId reads as dead forever
    // instead of silently starting to name a different entity). Owns no
    // gameplay/component data at all (SRP) - every subsystem stores its own
    // data keyed by EntityId, in whatever shape fits it (plain struct array
    // or component table - docs/entity-lifecycle-design.md).
    //
    // Main/sim thread only, same rule as CollisionWorld2D/3D
    // (docs/collider-design.md): never call Create/Destroy/Flush from inside
    // a JobSystem worker - that's already CLAUDE.md's invariant rule 6
    // ("워커 안에서... 엔티티 생성/파괴 금지").
    class EntityRegistry final : private NonCopyable
    {
    public:
        [[nodiscard]] EntityId Create();

        // Marks `id` dead immediately - IsAlive(id) is false from this call
        // on - but doesn't recycle its slot until Flush(). So every system
        // still running this step sees a consistent "who's alive" answer,
        // instead of an id disappearing out from under whichever system
        // happens to process it later in the same step.
        void Destroy(EntityId id);

        [[nodiscard]] bool IsAlive(EntityId id) const;

        // Recycles every slot queued by Destroy() since the last Flush(): its
        // index becomes reusable by a future Create(), generation already
        // bumped. Call once per fixed step, after every system has had its
        // turn (docs/entity-lifecycle-design.md "사용 방법").
        void Flush();

    private:
        struct Slot
        {
            std::uint32_t generation{};
            bool alive{ false };
        };

        std::vector<Slot> m_slots;
        std::vector<std::uint32_t> m_freeList;
        std::vector<std::uint32_t> m_pendingDestroy;
    };
}
