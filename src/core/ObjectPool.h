#pragma once

#include "core/NonCopyable.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::core
{
    // Fixed-capacity pool of reusable T. Storage is allocated once (Init /
    // constructor); Acquire() and Release() never allocate, construct, or
    // destroy - a recycled slot has T::Reset() called on it and keeps whatever
    // heap buffers T holds. Stale references are rejected by a per-slot
    // generation carried in Handle.
    //
    // Slots do NOT move: Handle::index is the slot index for the object's life,
    // so external code may hold Handles across frames. Live slots are listed,
    // densely, by ActiveIndices(); Slots()[i] is the raw slot array. To drive a
    // JobSystem::ParallelFor, split ActiveIndices() into non-overlapping
    // [begin, end) ranges - each entry is a distinct slot index, so workers
    // writing Slots()[ActiveIndices()[i]] never collide (invariant 6).
    //
    // Thread: main/sim thread only. Never Acquire/Release inside a ParallelFor
    // worker (same rule as CollisionWorld / EntityRegistry).
    //
    // Requirements on T: default-constructible and `void Reset()` (put the slot
    // back to a spawn-ready state without freeing buffers).
    template <class T>
    class ObjectPool final : private NonCopyable
    {
    public:
        struct Handle
        {
            std::uint32_t index{ 0 };
            std::uint32_t generation{ 0 };   // 0 == invalid / never issued

            [[nodiscard]] bool Valid() const { return generation != 0; }
        };

        ObjectPool() = default;
        explicit ObjectPool(std::size_t capacity) { Init(capacity); }

        // (Re)initialise to an empty pool of `capacity` slots. Drops all current
        // contents. Call once before first use; calling again is a full reset.
        void Init(std::size_t capacity)
        {
            m_slots.assign(capacity, T{});
            m_generation.assign(capacity, 0u);
            m_slotActive.assign(capacity, 0u);
            m_activePos.assign(capacity, 0u);
            m_active.clear();
            m_active.reserve(capacity);
            m_free.clear();
            m_free.reserve(capacity);
            // Low indices handed out first: push the free stack high-to-low.
            for (std::size_t i = capacity; i-- > 0;)
                m_free.push_back(static_cast<std::uint32_t>(i));
        }

        [[nodiscard]] std::size_t Capacity() const { return m_slots.size(); }
        [[nodiscard]] std::size_t Size() const { return m_active.size(); }
        [[nodiscard]] bool Empty() const { return m_active.empty(); }
        [[nodiscard]] bool Full() const { return m_free.empty(); }

        // Recycles a free slot: calls T::Reset() on it and returns a live
        // Handle. Returns an invalid Handle (generation 0) when the pool is full.
        Handle Acquire()
        {
            if (m_free.empty()) return {};

            const std::uint32_t index = m_free.back();
            m_free.pop_back();

            assert(m_slotActive[index] == 0u && "free slot was flagged active");

            std::uint32_t& gen = m_generation[index];
            ++gen;
            if (gen == 0u) gen = 1u;   // a live handle never carries generation 0

            m_slotActive[index] = 1u;
            m_activePos[index] = static_cast<std::uint32_t>(m_active.size());
            m_active.push_back(index);

            m_slots[index].Reset();
            return { index, gen };
        }

        // Returns a slot to the free list. A stale or invalid handle is a
        // no-op (safe to call twice). O(1) - swap-removes from the active list.
        void Release(Handle handle)
        {
            if (!IsLive(handle)) return;

            const std::uint32_t index = handle.index;
            m_slotActive[index] = 0u;

            // swap-remove `index` out of m_active
            const std::uint32_t pos = m_activePos[index];
            const std::uint32_t moved = m_active.back();
            m_active[pos] = moved;
            m_activePos[moved] = pos;
            m_active.pop_back();

            m_free.push_back(index);
        }

        [[nodiscard]] bool IsLive(Handle h) const
        {
            return h.generation != 0u
                && h.index < m_slots.size()
                && m_slotActive[h.index] != 0u
                && m_generation[h.index] == h.generation;
        }

        [[nodiscard]] T* Get(Handle h) { return IsLive(h) ? &m_slots[h.index] : nullptr; }
        [[nodiscard]] const T* Get(Handle h) const { return IsLive(h) ? &m_slots[h.index] : nullptr; }

        // Raw slot array (Capacity() entries; only ActiveIndices() are live).
        [[nodiscard]] T* Slots() { return m_slots.data(); }
        [[nodiscard]] const T* Slots() const { return m_slots.data(); }

        // Dense list of live slot indices. Order is unspecified and changes as
        // slots are released (swap-remove), so do not persist positions in it.
        [[nodiscard]] const std::vector<std::uint32_t>& ActiveIndices() const { return m_active; }

    private:
        std::vector<T>             m_slots;       // stable storage, never reordered
        std::vector<std::uint32_t> m_generation;  // per slot; bumped on Acquire
        std::vector<std::uint8_t>  m_slotActive;  // per slot 0/1
        std::vector<std::uint32_t> m_activePos;   // slot index -> its position in m_active (valid while active)
        std::vector<std::uint32_t> m_active;      // dense live-slot indices
        std::vector<std::uint32_t> m_free;        // free-slot stack
    };
}
