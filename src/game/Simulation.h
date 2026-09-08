#pragma once

#include "core/JobSystem.h"
#include "core/NonCopyable.h"
#include "math/Math.h"

#include <cstddef>
#include <vector>

namespace engine::game
{
    // One frame's worth of movement request for the player, already resolved
    // from raw input (and from whether the UI consumed the pointer). Decouples
    // the simulation from InputState so it can be unit-tested and driven by a
    // replay or an AI just as easily.
    struct PlayerIntent
    {
        math::Vec2 move{};   // each axis in [-1, 1]; length may exceed 1 before normalisation
    };

    // Demo payload: a particle that advects at constant velocity and wraps at
    // the world edges. Contiguous in a vector so JobSystem::ParallelFor can split
    // it into non-overlapping [begin, end) ranges.
    struct Particle
    {
        float x{}, y{}, vx{}, vy{};
    };

    // Owns the mutable game world and advances it on a fixed timestep. The only
    // thing that writes world state; the SnapshotBuilder reads it afterwards.
    class Simulation final : private core::NonCopyable
    {
    public:
        static constexpr float kPlayerSpeed = 300.0f;   // pixels / second
        static constexpr float kPlayerSize = 64.0f;
        static constexpr std::size_t kParticleCount = 20'000;

        Simulation(core::JobSystem& jobs, int worldWidth, int worldHeight);

        void SetWorldSize(int width, int height);

        // Advance exactly one fixed step. Call once per accumulated step.
        void Step(float fixedDelta, const PlayerIntent& intent);

        [[nodiscard]] math::Vec2 PlayerPosition() const { return m_player; }
        [[nodiscard]] const std::vector<Particle>& Particles() const { return m_particles; }
        // Seconds of simulation time elapsed. Drives the demo's 3D camera orbit
        // and cube spin in the SnapshotBuilder.
        [[nodiscard]] float ElapsedTime() const { return m_elapsed; }

    private:
        void SeedParticles();

        core::JobSystem& m_jobs;
        int m_worldWidth;
        int m_worldHeight;
        float m_elapsed{};
        math::Vec2 m_player;
        std::vector<Particle> m_particles;
    };
}
