#pragma once

#include "render/r3d/CharacterAnimationClips.h"

#include <span>

// Gameplay-driven animation playback, decoupled from what drives it.
// See docs/demo-scene.md, docs/model-animation-research.md §5.3,
// docs/animation-design.md §1/§5, docs/roadmap.md §2.1.
namespace engine::game
{
    enum class Locomotion
    {
        Wait,
        Walk,
        Run,
        Jump,
    };

    // Everything the render side needs to pose the character this frame. Crosses
    // into RenderSnapshot as values only (no skeleton/clip data). When `blend`
    // > 0 the renderer evaluates both `from*` and the current clip and lerps.
    // When `parametric`, `clipTime` is a 0..1 phase the renderer multiplies by
    // the clip's own duration (PlayMode::Once); otherwise it is seconds mapped
    // by `playMode`.
    struct AnimPose
    {
        int   clipIndex{ render::kUnityChanWaitClip };
        float clipTime{ 0.0f };
        int   playMode{ 0 };          // anim::PlayMode
        bool  parametric{ false };

        int   fromClipIndex{ -1 };
        float fromClipTime{ 0.0f };
        int   fromPlayMode{ 0 };
        float blend{ 0.0f };          // 0 = no crossfade, ->1 = fully on `clipIndex`
    };

    // Minimal animation state machine: on a Locomotion change it starts a short
    // crossfade to that state's clip. Jump is driven parametrically from the
    // jump arc phase (docs/roadmap.md §2.1) instead of wall-clock time, so it
    // stays natural regardless of air time. Runs on the sim thread inside
    // Simulation::Step (fixed timestep).
    class CharacterAnimationState final
    {
    public:
        explicit CharacterAnimationState(std::span<const render::CharacterAnimationClipInfo> clips)
            : m_clips(clips) {}

        CharacterAnimationState(const CharacterAnimationState&) = delete;
        CharacterAnimationState& operator=(const CharacterAnimationState&) = delete;
        CharacterAnimationState(CharacterAnimationState&&) = default;
        CharacterAnimationState& operator=(CharacterAnimationState&&) = default;

        // Time-driven playback: advances the clip clock by `dt`, crossfades on a
        // state change.
        void Update(float dt, Locomotion desired);

        // Phase-driven playback (jump): `phase01` in [0,1] is the position along
        // the state's clip. Still crossfades on entering/leaving the state.
        void UpdateParametric(float dt, Locomotion desired, float phase01);

        [[nodiscard]] AnimPose Pose() const;
        [[nodiscard]] Locomotion Current() const { return m_current; }

        // Kept for existing call sites; equivalent to Pose().clipIndex / .clipTime.
        [[nodiscard]] int ClipIndex() const { return m_clipIndex; }
        [[nodiscard]] float ClipTime() const { return m_clipTime; }

    private:
        [[nodiscard]] static int ClipFor(Locomotion state);
        [[nodiscard]] static int PlayModeFor(Locomotion state);   // anim::PlayMode value
        void BeginState(Locomotion desired);

        static constexpr float kBlendSeconds = 0.15f;

        std::span<const render::CharacterAnimationClipInfo> m_clips;
        Locomotion m_current{ Locomotion::Wait };
        int   m_clipIndex{ render::kUnityChanWaitClip };
        float m_clipTime{ 0.0f };
        bool  m_parametric{ false };

        // Outgoing clip during a crossfade.
        int   m_fromClipIndex{ -1 };
        float m_fromClipTime{ 0.0f };
        int   m_fromPlayMode{ 0 };
        float m_blendElapsed{ kBlendSeconds };   // >= kBlendSeconds -> no blend
    };
}
