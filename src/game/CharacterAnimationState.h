#pragma once

#include "core/NonCopyable.h"
#include "render/r3d/CharacterAnimationClips.h"

#include <span>

// Condition-based animation playback, decoupled from what the condition is.
// See docs/model-animation-research.md §5.3, docs/animation-design.md §1/§5.
namespace engine::game
{
    // Picks which clip index (into a CharacterAnimationClipInfo table) a
    // character plays and for how long, advancing round-robin whenever
    // Tick()'s condition holds. The demo condition is "held this clip's
    // holdSeconds" (see CharacterAnimationClips.h); swap the body of Tick()
    // for a real gameplay signal (speed > 0, grounded, took damage, ...)
    // later without touching call sites - they only ever read
    // ClipIndex()/ClipTime(). Runs on the main/sim thread inside
    // Simulation::Step (fixed timestep, docs/time-design.md); only the
    // resulting (clipIndex, clipTime) values cross into the RenderSnapshot.
    class CharacterAnimationState final : private core::NonCopyable
    {
    public:
        explicit CharacterAnimationState(std::span<const render::CharacterAnimationClipInfo> clips)
            : m_clips(clips) {}

        void Tick(float fixedDeltaSeconds);

        [[nodiscard]] int ClipIndex() const { return m_clipIndex; }
        [[nodiscard]] float ClipTime() const { return m_clipTime; }

    private:
        std::span<const render::CharacterAnimationClipInfo> m_clips;   // non-owning; caller's table outlives this
        int m_clipIndex{ 0 };
        float m_clipTime{ 0.0f };
        float m_holdElapsed{ 0.0f };
    };
}
