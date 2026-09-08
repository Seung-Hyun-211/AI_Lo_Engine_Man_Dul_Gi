#include "game/Simulation.h"

#include <algorithm>
#include <cmath>

namespace engine::game
{
    Simulation::Simulation(core::JobSystem& jobs, int worldWidth, int worldHeight)
        : m_jobs(jobs)
        , m_worldWidth(std::max(worldWidth, 1))
        , m_worldHeight(std::max(worldHeight, 1))
        , m_player{ static_cast<float>(m_worldWidth) * 0.5f - kPlayerSize * 0.5f,
                    static_cast<float>(m_worldHeight) * 0.5f - kPlayerSize * 0.5f }
    {
        m_particles.resize(kParticleCount);
        SeedParticles();
    }

    void Simulation::SetWorldSize(int width, int height)
    {
        m_worldWidth = std::max(width, 1);
        m_worldHeight = std::max(height, 1);
    }

    void Simulation::SeedParticles()
    {
        // Deterministic pseudo-spread so the demo looks the same every launch.
        for (std::size_t index = 0; index < m_particles.size(); ++index)
        {
            const float seed = static_cast<float>(index);
            m_particles[index] = {
                std::fmod(seed * 37.0f, static_cast<float>(m_worldWidth)),
                std::fmod(seed * 19.0f, static_cast<float>(m_worldHeight)),
                20.0f + std::fmod(seed, 90.0f),
                10.0f + std::fmod(seed * 0.5f, 70.0f),
            };
        }
    }

    void Simulation::Step(float fixedDelta, const PlayerIntent& intent)
    {
        m_elapsed += fixedDelta;

        const math::Vec2 direction = math::Normalized(intent.move);
        m_player = m_player + direction * (kPlayerSpeed * fixedDelta);
        m_player.x = math::Clamp(m_player.x, 0.0f, std::max(0.0f, static_cast<float>(m_worldWidth) - kPlayerSize));
        m_player.y = math::Clamp(m_player.y, 0.0f, std::max(0.0f, static_cast<float>(m_worldHeight) - kPlayerSize));

        // Benchmark stub for the JobSystem contiguous-range contract: the full
        // 20k particles are advected every step even though the snapshot only
        // shows a sample of them. Each job owns a distinct [begin, end) range and
        // touches no shared state; the fence is the step boundary.
        const float width = static_cast<float>(m_worldWidth);
        const float height = static_cast<float>(m_worldHeight);
        m_jobs.ParallelFor(0, m_particles.size(), 2'048,
            [this, fixedDelta, width, height](std::size_t begin, std::size_t end)
            {
                for (std::size_t index = begin; index < end; ++index)
                {
                    Particle& particle = m_particles[index];
                    particle.x += particle.vx * fixedDelta;
                    particle.y += particle.vy * fixedDelta;
                    if (particle.x > width) particle.x = 0.0f;
                    if (particle.y > height) particle.y = 0.0f;
                }
            }).Wait();
    }
}
