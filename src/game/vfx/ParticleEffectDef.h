#pragma once

#include "game/vfx/Particle.h"

// Policy table for a burst (docs/particle-system-research.md §4.2). `Particle`
// carries pure per-instance state; a def is spawn-time-only tuning it copies
// onto each new particle - never mutated at runtime.
namespace engine::game::vfx
{
    struct ParticleEffectDef
    {
        ParticleBlend blend{ ParticleBlend::AlphaBlend };
        ParticleKind  kind{ ParticleKind::Generic };
        std::uint32_t colorStart{ 0xffffffffu };
        std::uint32_t colorEnd{ 0x00000000u };
        float sizeStart{ 0.15f };
        float sizeEnd{ 0.15f };
        float lifeMin{ 0.3f }, lifeMax{ 0.6f };
        float speedMin{ 1.0f }, speedMax{ 2.0f };
        float spreadDeg{ 15.0f };       // half-angle of the spawn direction cone
        float gravityScale{ 0.0f };
        float dragPerSec{ 0.0f };
        float angularVelMax{ 0.0f };     // rad/s spin, sign randomized per particle - 0 = no spin (very short-lived flashes)
        int   burstMin{ 4 }, burstMax{ 8 };
    };
}
