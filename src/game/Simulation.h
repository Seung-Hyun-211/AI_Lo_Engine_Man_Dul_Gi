#pragma once

#include "core/JobSystem.h"
#include "core/NonCopyable.h"
#include "math/Math.h"
#include "physics/p2d/CollisionWorld2D.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(ENGINE_WITH_3D)
#include "game/CharacterAnimationState.h"
#include "physics/p3d/CollisionWorld3D.h"
#endif

namespace engine::game
{
    // One frame's movement request for the player, already resolved from raw
    // input. Decouples the simulation from InputState.
    struct PlayerIntent
    {
        math::Vec2 move{};   // each axis in [-1, 1]
    };

    // Demo particle: advects at constant velocity, wraps at world edges.
    struct Particle
    {
        float x{}, y{}, vx{}, vy{};
    };

    // Owns the mutable game world and advances it on a fixed timestep. The only
    // writer of world state; the SnapshotBuilder reads it afterwards. Also owns
    // the collision worlds and runs detection each step (no response - contacts
    // only change demo colors).
    class Simulation final : private core::NonCopyable
    {
    public:
        static constexpr float kPlayerSpeed = 300.0f;   // pixels / second
        static constexpr float kPlayerSize = 64.0f;
        static constexpr std::size_t kParticleCount = 20'000;
        static constexpr int kObstacleCount = 3;
        static constexpr int kSatelliteCount = 2;

        Simulation(core::JobSystem& jobs, int worldWidth, int worldHeight);

        void SetWorldSize(int width, int height);
        void Step(float fixedDelta, const PlayerIntent& intent);

        // --- reads for the snapshot builder ---
        [[nodiscard]] float ElapsedTime() const { return m_elapsed; }
        [[nodiscard]] math::Vec2 PlayerPosition() const { return m_player; }
        [[nodiscard]] bool PlayerBlocked() const { return m_playerBlocked; }
        [[nodiscard]] const std::array<math::Rect, kObstacleCount>& Obstacles() const { return m_obstacles; }
        [[nodiscard]] const std::vector<Particle>& Particles() const { return m_particles; }

#if defined(ENGINE_WITH_3D)
        [[nodiscard]] math::Vec3 HeroCenter() const { return m_heroCenter; }
        [[nodiscard]] float HeroSpin() const { return m_elapsed; }
        [[nodiscard]] const std::array<math::Vec3, kSatelliteCount>& SatelliteCenters() const { return m_satelliteCenters; }
        [[nodiscard]] const std::array<bool, kSatelliteCount>& SatelliteHitsHero() const { return m_satelliteHitsHero; }
        [[nodiscard]] int HeroAnimClipIndex() const { return m_heroAnimation.ClipIndex(); }
        [[nodiscard]] float HeroAnimClipTime() const { return m_heroAnimation.ClipTime(); }
#endif

    private:
        void SeedParticles();
        void LayOutObstacles();
        void StepCollision2D();
#if defined(ENGINE_WITH_3D)
        void StepDemo3D(float fixedDelta);
#endif

        core::JobSystem& m_jobs;
        int m_worldWidth;
        int m_worldHeight;
        float m_elapsed{};

        math::Vec2 m_player;
        bool m_playerBlocked{};
        std::array<math::Rect, kObstacleCount> m_obstacles{};
        std::vector<Particle> m_particles;
        physics::CollisionWorld2D m_collision2d;

#if defined(ENGINE_WITH_3D)
        math::Vec3 m_heroCenter{ 0.0f, 0.9f, 0.0f };
        std::array<math::Vec3, kSatelliteCount> m_satelliteCenters{};
        std::array<bool, kSatelliteCount> m_satelliteHitsHero{};
        physics::CollisionWorld3D m_collision3d;
        CharacterAnimationState m_heroAnimation{ render::kUnityChanClips };
#endif
    };
}
