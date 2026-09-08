#include "game/Simulation.h"

#include <algorithm>
#include <cmath>

namespace engine::game
{
    namespace
    {
        constexpr std::uint64_t kPlayerUser = 1;
        constexpr std::uint64_t kObstacleUserBase = 100;
        constexpr physics::CollisionLayer kLayerPlayer = 1u;
        constexpr physics::CollisionLayer kLayerObstacle = 2u;

#if defined(ENGINE_WITH_3D)
        constexpr std::uint64_t kHeroUser = 1;
        constexpr std::uint64_t kSatelliteUserBase = 10;
        constexpr physics::CollisionLayer kLayerHero = 1u;
        constexpr physics::CollisionLayer kLayerSatellite = 2u;
        constexpr float kPi = 3.14159265358979323846f;
#endif
    }

    Simulation::Simulation(core::JobSystem& jobs, int worldWidth, int worldHeight)
        : m_jobs(jobs)
        , m_worldWidth(std::max(worldWidth, 1))
        , m_worldHeight(std::max(worldHeight, 1))
        , m_player{ static_cast<float>(m_worldWidth) * 0.5f - kPlayerSize * 0.5f,
                    static_cast<float>(m_worldHeight) * 0.5f - kPlayerSize * 0.5f }
    {
        m_particles.resize(kParticleCount);
        SeedParticles();
        LayOutObstacles();
    }

    void Simulation::SetWorldSize(int width, int height)
    {
        m_worldWidth = std::max(width, 1);
        m_worldHeight = std::max(height, 1);
        LayOutObstacles();
    }

    void Simulation::SeedParticles()
    {
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

    void Simulation::LayOutObstacles()
    {
        const float w = static_cast<float>(m_worldWidth);
        const float h = static_cast<float>(m_worldHeight);
        m_obstacles[0] = { w * 0.28f, h * 0.24f, 130.0f, 130.0f };
        m_obstacles[1] = { w * 0.62f, h * 0.55f, 110.0f, 170.0f };
        m_obstacles[2] = { w * 0.44f, h * 0.72f, 170.0f, 90.0f };
    }

    void Simulation::Step(float fixedDelta, const PlayerIntent& intent)
    {
        m_elapsed += fixedDelta;

        const math::Vec2 direction = math::Normalized(intent.move);
        m_player = m_player + direction * (kPlayerSpeed * fixedDelta);
        m_player.x = math::Clamp(m_player.x, 0.0f, std::max(0.0f, static_cast<float>(m_worldWidth) - kPlayerSize));
        m_player.y = math::Clamp(m_player.y, 0.0f, std::max(0.0f, static_cast<float>(m_worldHeight) - kPlayerSize));

        // Benchmark stub for the JobSystem contiguous-range contract: the full
        // 20k particles advect every step even though the snapshot draws a
        // sample. Each job owns a distinct [begin, end) range, touches no shared
        // state; the fence is the step boundary.
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

        StepCollision2D();
#if defined(ENGINE_WITH_3D)
        StepDemo3D(fixedDelta);
#endif
    }

    void Simulation::StepCollision2D()
    {
        // Rebuild-every-step pattern: cheap for a handful of colliders and needs
        // no id bookkeeping. Colliders in the same layer never test each other,
        // so the only possible contacts are player vs obstacle.
        m_collision2d.Clear();

        physics::Collider2D player{};
        player.shape = physics::Collider2D::Shape::Box;
        player.center = m_player + math::Vec2{ kPlayerSize * 0.5f, kPlayerSize * 0.5f };
        player.halfExtents = { kPlayerSize * 0.5f, kPlayerSize * 0.5f };
        player.layer = kLayerPlayer;
        player.mask = kLayerObstacle;
        player.user = kPlayerUser;
        m_collision2d.Add(player);

        for (int i = 0; i < kObstacleCount; ++i)
        {
            const math::Rect& rect = m_obstacles[i];
            physics::Collider2D obstacle{};
            obstacle.shape = physics::Collider2D::Shape::Box;
            obstacle.center = { rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f };
            obstacle.halfExtents = { rect.width * 0.5f, rect.height * 0.5f };
            obstacle.layer = kLayerObstacle;
            obstacle.mask = kLayerPlayer;
            obstacle.user = kObstacleUserBase + static_cast<std::uint64_t>(i);
            m_collision2d.Add(obstacle);
        }

        m_collision2d.Step();
        m_playerBlocked = !m_collision2d.Contacts().empty();
    }

#if defined(ENGINE_WITH_3D)
    void Simulation::StepDemo3D(float /*fixedDelta*/)
    {
        // Satellites orbit the hero cube while their radius pulses in and out, so
        // they periodically enter and leave contact.
        for (int i = 0; i < kSatelliteCount; ++i)
        {
            const float phase = m_elapsed * 0.8f + static_cast<float>(i) * kPi;
            const float radius = 0.85f + 0.55f * std::sin(m_elapsed * 1.3f + static_cast<float>(i) * 2.0f);
            m_satelliteCenters[i] = { std::cos(phase) * radius, m_heroCenter.y, std::sin(phase) * radius };
        }

        m_collision3d.Clear();

        physics::Collider3D hero{};
        hero.shape = physics::Collider3D::Shape::Box;
        hero.center = m_heroCenter;
        hero.halfExtents = { 0.5f, 0.5f, 0.5f };   // unit cube, spin ignored (static AABB approx)
        hero.layer = kLayerHero;
        hero.mask = kLayerSatellite;
        hero.user = kHeroUser;
        m_collision3d.Add(hero);

        for (int i = 0; i < kSatelliteCount; ++i)
        {
            physics::Collider3D satellite{};
            satellite.shape = physics::Collider3D::Shape::Sphere;
            satellite.center = m_satelliteCenters[i];
            satellite.radius = 0.3f;
            satellite.layer = kLayerSatellite;
            satellite.mask = kLayerHero;
            satellite.user = kSatelliteUserBase + static_cast<std::uint64_t>(i);
            m_collision3d.Add(satellite);
        }

        m_collision3d.Step();

        m_satelliteHitsHero.fill(false);
        for (const physics::Contact& contact : m_collision3d.Contacts())
        {
            for (std::uint64_t user : { contact.userA, contact.userB })
            {
                if (user >= kSatelliteUserBase &&
                    user < kSatelliteUserBase + static_cast<std::uint64_t>(kSatelliteCount))
                {
                    m_satelliteHitsHero[user - kSatelliteUserBase] = true;
                }
            }
        }
    }
#endif
}
