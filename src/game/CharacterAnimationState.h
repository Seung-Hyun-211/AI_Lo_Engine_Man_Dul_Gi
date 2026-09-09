#pragma once

#include "core/NonCopyable.h"
#include "render/r3d/CharacterAnimationClips.h"

#include <span>

// Gameplay-driven animation playback, decoupled from what drives it.
// See docs/demo-scene.md, docs/model-animation-research.md §5.3,
// docs/animation-design.md §1/§5.
namespace engine::game
{
    // The locomotion states the demo character can be in. Simulation decides
    // which one holds each fixed step (grounded + planar speed + run key) and
    // CharacterAnimationState turns that into a (clipIndex, clipTime) pair -
    // the only thing that crosses into the RenderSnapshot.
    enum class Locomotion
    {
        Wait,
        Walk,
        Run,
        Jump,
    };

    // Minimal animation state machine: on a state change it snaps to that
    // state's clip and restarts the clip clock; otherwise it just advances the
    // clock (AnimationSampler loops it over the clip duration). No crossfade /
    // blending yet - that is still design-only (docs/animation-design.md §5).
    // Runs on the sim thread inside Simulation::Step (fixed timestep).
    class CharacterAnimationState final : private core::NonCopyable
    {
    public:
        explicit CharacterAnimationState(std::span<const render::CharacterAnimationClipInfo> clips)
            : m_clips(clips) {}

        // desired: this fixed step's locomotion state. dt: the fixed step.
        void Update(float fixedDeltaSeconds, Locomotion desired);

        [[nodiscard]] int ClipIndex() const { return m_clipIndex; }
        [[nodiscard]] float ClipTime() const { return m_clipTime; }
        [[nodiscard]] Locomotion Current() const { return m_current; }

    private:
        [[nodiscard]] static int ClipFor(Locomotion state);

        std::span<const render::CharacterAnimationClipInfo> m_clips;   // non-owning; caller's table outlives this
        Locomotion m_current{ Locomotion::Wait };
        int m_clipIndex{ render::kUnityChanWaitClip };
        float m_clipTime{ 0.0f };
    };
}
