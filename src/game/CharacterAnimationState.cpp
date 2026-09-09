#include "game/CharacterAnimationState.h"

namespace engine::game
{
    void CharacterAnimationState::Tick(float fixedDeltaSeconds)
    {
        if (m_clips.empty()) return;

        m_clipTime += fixedDeltaSeconds;
        m_holdElapsed += fixedDeltaSeconds;

        // The condition: held this clip long enough. Real gameplay would check
        // player/AI state here instead and only reset the timers on an actual
        // transition (see the class comment).
        if (m_holdElapsed >= m_clips[static_cast<std::size_t>(m_clipIndex)].holdSeconds)
        {
            m_holdElapsed = 0.0f;
            m_clipTime = 0.0f;
            m_clipIndex = static_cast<int>((static_cast<std::size_t>(m_clipIndex) + 1) % m_clips.size());
        }
    }
}
