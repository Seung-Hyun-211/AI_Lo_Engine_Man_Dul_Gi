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
        StepCharacter3D(fixedDelta, intent);
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
    void Simulation::UpdateCameraLook(math::Vec2 mouseDelta)
    {
        m_cameraYaw += mouseDelta.x * kMouseSensitivity;
        while (m_cameraYaw > kPi) m_cameraYaw -= 2.0f * kPi;
        while (m_cameraYaw < -kPi) m_cameraYaw += 2.0f * kPi;
        // Mouse down (positive y) tilts the view down; not inverted.
        m_cameraPitch = math::Clamp(m_cameraPitch - mouseDelta.y * kMouseSensitivity,
                                    kCamPitchMin, kCamPitchMax);
    }

    void Simulation::StepCharacter3D(float fixedDelta, const PlayerIntent& intent)
    {
        // Move relative to where the camera is looking: the camera's yaw rotates
        // the WASD axes, so "W" is always "into the screen" regardless of orbit.
        const float cy = std::cos(m_cameraYaw), sy = std::sin(m_cameraYaw);
        const math::Vec3 camForward{ sy, 0.0f, cy };   // ground-plane forward of the camera
        const math::Vec3 camRight{ cy, 0.0f, -sy };

        math::Vec3 wish = camForward * intent.move.y + camRight * intent.move.x;
        const float wishLen = math::Length(wish);
        const bool moving = wishLen > 1e-3f;
        if (moving) wish = wish * (1.0f / wishLen);

        const float speed = intent.run ? kCharRunSpeed : kCharWalkSpeed;
        if (moving)
        {
            m_charPos = m_charPos + wish * (speed * fixedDelta);

            // Turn toward the movement direction along the shortest arc.
            const float targetYaw = std::atan2(wish.x, wish.z);
            float delta = targetYaw - m_charFacingYaw;
            while (delta > kPi) delta -= 2.0f * kPi;
            while (delta < -kPi) delta += 2.0f * kPi;
            const float maxTurn = kCharTurnRate * fixedDelta;
            m_charFacingYaw += math::Clamp(delta, -maxTurn, maxTurn);
        }

        // Jump + gravity against the ground plane y = 0. The request is latched
        // (QueueJump) and consumed here regardless of grounded state so it never
        // carries over into a later step.
        if (m_jumpQueued && m_charGrounded)
        {
            m_charVerticalVel = kCharJumpSpeed;
            m_charGrounded = false;
        }
        m_jumpQueued = false;
        if (!m_charGrounded)
        {
            m_charVerticalVel -= kCharGravity * fixedDelta;
            m_charPos.y += m_charVerticalVel * fixedDelta;
            if (m_charPos.y <= 0.0f)
            {
                m_charPos.y = 0.0f;
                m_charVerticalVel = 0.0f;
                m_charGrounded = true;
            }
        }

        m_charPos.x = math::Clamp(m_charPos.x, -kCharHalfRange, kCharHalfRange);
        m_charPos.z = math::Clamp(m_charPos.z, -kCharHalfRange, kCharHalfRange);

        // Locomotion -> which clip the animation state plays. Only
        // (clipIndex, clipTime) crosses into the snapshot; ModelMeshPass3D owns
        // the clip data and does the pose evaluation + CPU skinning.
        Locomotion loco;
        if (!m_charGrounded)  loco = Locomotion::Jump;
        else if (!moving)     loco = Locomotion::Wait;
        else if (intent.run) loco = Locomotion::Run;
        else                 loco = Locomotion::Walk;
        m_heroAnimation.Update(fixedDelta, loco);
    }
#endif
}
