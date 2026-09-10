#include "game/CharacterAnimationState.h"

#include "anim/AnimationSampler.h"   // anim::PlayMode values

#include <algorithm>

namespace engine::game
{
    int CharacterAnimationState::ClipFor(Locomotion state)
    {
        switch (state)
        {
        case Locomotion::Walk: return render::kUnityChanWalkClip;
        case Locomotion::Run:  return render::kUnityChanRunClip;
        case Locomotion::Jump: return render::kUnityChanJumpClip;
        case Locomotion::Wait:
        default:               return render::kUnityChanWaitClip;
        }
    }

    int CharacterAnimationState::PlayModeFor(Locomotion state)
    {
        // Jump is driven parametrically (phase), so its play mode is moot; give
        // it Once anyway. Everything else loops.
        return state == Locomotion::Jump
            ? static_cast<int>(anim::PlayMode::Once)
            : static_cast<int>(anim::PlayMode::Loop);
    }

    void CharacterAnimationState::BeginState(Locomotion desired)
    {
        // Snapshot the current clip as the outgoing side of a fresh crossfade.
        m_fromClipIndex = m_clipIndex;
        m_fromClipTime = m_clipTime;
        m_fromPlayMode = PlayModeFor(m_current);
        m_blendElapsed = 0.0f;

        m_current = desired;
        m_clipIndex = ClipFor(desired);
        m_clipTime = 0.0f;
    }

    void CharacterAnimationState::Update(float dt, Locomotion desired)
    {
        if (m_clips.empty()) return;
        if (desired != m_current) BeginState(desired);

        m_parametric = false;
        m_clipTime += dt;
        m_blendElapsed = std::min(m_blendElapsed + dt, kBlendSeconds);
    }

    void CharacterAnimationState::UpdateParametric(float dt, Locomotion desired, float phase01)
    {
        if (m_clips.empty()) return;
        if (desired != m_current) BeginState(desired);

        m_parametric = true;
        m_clipTime = std::clamp(phase01, 0.0f, 1.0f);   // renderer scales by clip duration
        m_fromClipTime += dt;                            // outgoing clip keeps advancing while it fades
        m_blendElapsed = std::min(m_blendElapsed + dt, kBlendSeconds);
    }

    AnimPose CharacterAnimationState::Pose() const
    {
        AnimPose p;
        p.clipIndex = m_clipIndex;
        p.clipTime = m_clipTime;
        p.playMode = m_parametric ? static_cast<int>(anim::PlayMode::Once) : PlayModeFor(m_current);
        p.parametric = m_parametric;

        const float blend = kBlendSeconds > 1e-6f ? m_blendElapsed / kBlendSeconds : 1.0f;
        if (blend < 1.0f && m_fromClipIndex >= 0)
        {
            p.fromClipIndex = m_fromClipIndex;
            p.fromClipTime = m_fromClipTime;
            p.fromPlayMode = m_fromPlayMode;
            p.blend = blend;
        }
        return p;
    }
}
