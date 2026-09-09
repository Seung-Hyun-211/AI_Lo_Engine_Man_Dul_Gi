#include "game/CharacterAnimationState.h"

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

    void CharacterAnimationState::Update(float fixedDeltaSeconds, Locomotion desired)
    {
        if (m_clips.empty()) return;

        if (desired != m_current)
        {
            m_current = desired;
            m_clipIndex = ClipFor(desired);
            m_clipTime = 0.0f;   // restart the new clip from its first frame
        }

        m_clipTime += fixedDeltaSeconds;
    }
}
