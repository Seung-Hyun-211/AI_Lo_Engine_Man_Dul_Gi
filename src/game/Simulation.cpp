#include "game/Simulation.h"

#include <algorithm>
#include <cmath>
#include <optional>

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

        // A local timeScale > 1 is run as this many sub-steps of the fixed step
        // (so a hasted actor integrates in small increments, not one big jump).
        constexpr int kMaxActorSubSteps = 8;
        constexpr float kDemoActorSpeed = 3.0f;   // m/s the canned demo actors wander at

        // Demo-scene-2 crowd field box (z range the agents wander in) and the
        // pool churn cadence. Shared by SeedAgent / StepSimAgents.
        constexpr float kFieldAgentZLo = 8.0f;
        constexpr float kFieldAgentZHi = Simulation::kFieldHalf + 10.0f;
        constexpr int   kAgentChurnIntervalSteps = 12;   // recycle 1 crowd member every N fixed steps

        // Collision layer for the demo-scene-2 crowd colliders. The player
        // look-ray masks to exactly this.
        constexpr physics::CollisionLayer kLayerCrowd3D = 1u;
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
#if defined(ENGINE_WITH_3D)
        SpawnActors();
#endif
    }

#if defined(ENGINE_WITH_3D)
    void Simulation::SpawnActors()
    {
        Actor player;
        player.playerControlled = true;

        if constexpr (kDemoScene == 2)
        {
            // Stand on top of the mesa facing out over the field (+Z). The
            // x/z clamp is symmetric about the origin (halfRange), so the mesa
            // prop is centred there too. The orbit camera sits behind + above,
            // so the default view looks down the drop at the crowd.
            player.pos = { 0.0f, kCliffTop, -2.0f };
            player.facingYaw = 0.0f;
            player.groundY = kCliffTop;
            player.halfRange = kPlateauHalf;
            m_actors.push_back(std::move(player));

            m_cameraPitch = -0.5f;   // steeper default tilt for the overlook
            SpawnSimAgents();
        }
        else if constexpr (kDemoScene == 3)
        {
            // Just the player, centred - no wandering extras to clutter the
            // shadow/lighting showcase (SnapshotBuilder::BuildShadowShowcaseScene).
            player.pos = { 0.0f, 0.0f, 0.0f };
            player.facingYaw = 0.0f;
            m_actors.push_back(std::move(player));
        }
        else
        {
            m_actors.reserve(3);
            m_actors.push_back(std::move(player));

            // A "hasted" demo actor: 3x local time, wanders visibly faster than
            // the player even at global scale 1. Keeps moving while paused.
            Actor fast;
            fast.pos = { -3.0f, 0.0f, -2.5f };
            fast.facingYaw = 0.6f;
            fast.timeScale = 3.0f;
            fast.ignoreGlobalPause = true;
            m_actors.push_back(std::move(fast));

            // A "slowed" demo actor: 0.35x local time.
            Actor slow;
            slow.pos = { 3.0f, 0.0f, 2.5f };
            slow.facingYaw = -2.2f;
            slow.timeScale = 0.35f;
            m_actors.push_back(std::move(slow));
        }
    }

    void Simulation::SeedAgent(SimAgent& agent, float seed)
    {
        // Deterministic (no RNG) scatter across the field in front of the mesa.
        agent.pos = { std::fmod(seed * 7.13f, 2.0f * kFieldHalf) - kFieldHalf,
                      0.2f,
                      kFieldAgentZLo + std::fmod(seed * 3.7f, kFieldAgentZHi - kFieldAgentZLo) };
        agent.heading = std::fmod(seed * 2.399963f, 2.0f * kPi) - kPi;   // spread out
        agent.speed = 0.8f + std::fmod(seed, 5.0f) * 0.35f;              // 0.8 .. 2.2 m/s
        agent.phase = seed * 0.37f;
        agent.animTime = std::fmod(seed * 0.618f, 3.0f);                 // desync the walk cycle
    }

    void Simulation::SpawnSimAgents()
    {
        m_agents.Init(static_cast<std::size_t>(kActiveCrowd.capacity));
        m_agentHandles.clear();
        m_agentHandles.reserve(static_cast<std::size_t>(kActiveCrowd.count));
        m_agentChurnCursor = 0;

        for (int i = 0; i < kActiveCrowd.count; ++i)
        {
            const auto handle = m_agents.Acquire();
            m_agentHandles.push_back(handle);
            if (SimAgent* a = m_agents.Get(handle))
                SeedAgent(*a, static_cast<float>(i));
        }
    }
#endif

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

    void Simulation::Step(float fixedDelta, const PlayerIntent& intent, bool globalPaused)
    {
        if (globalPaused)
        {
            // Global time scale is 0: the world is frozen. Only actors that
            // opted out of the pause take a step.
#if defined(ENGINE_WITH_3D)
            StepActors(fixedDelta, /*globalPaused=*/true, intent);
#else
            (void)fixedDelta;
            (void)intent;
#endif
            return;
        }

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
        StepActors(fixedDelta, /*globalPaused=*/false, intent);
        StepSimAgents(fixedDelta);
        // Crowd colliders + look-ray + overlap tint run ONCE per frame from
        // Application::UpdateCrowdQueries(), not here - they are O(crowd) and
        // render-only, so per-sub-step made a slow frame spiral.
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

    void Simulation::StepActors(float fixedDelta, bool globalPaused, const PlayerIntent& intent)
    {
        for (Actor& actor : m_actors)
        {
            if (globalPaused && !actor.ignoreGlobalPause) continue;

            // Local time dilation: the actor's own step is fixedDelta * timeScale.
            // timeScale > 1 is run as sub-steps so movement/gravity integrate in
            // small increments instead of one large jump (tunneling, blow-up).
            const float scaled = fixedDelta * actor.timeScale;
            const int sub = actor.timeScale <= 1.0f
                ? 1
                : std::min(static_cast<int>(std::ceil(actor.timeScale)), kMaxActorSubSteps);
            const float subDt = scaled / static_cast<float>(sub);

            const PlayerIntent* actorIntent = actor.playerControlled ? &intent : nullptr;
            for (int i = 0; i < sub; ++i)
                StepOneActor(actor, subDt, actorIntent);
        }
    }

    void Simulation::StepOneActor(Actor& actor, float dt, const PlayerIntent* intent) const
    {
        math::Vec3 wish{};
        bool moving = false;
        float speed = kCharWalkSpeed;
        bool run = false;

        if (intent != nullptr)
        {
            // Move relative to where the camera is looking: the camera's yaw
            // rotates the WASD axes, so "W" is always "into the screen".
            const float cy = std::cos(m_cameraYaw), sy = std::sin(m_cameraYaw);
            const math::Vec3 camForward{ sy, 0.0f, cy };
            const math::Vec3 camRight{ cy, 0.0f, -sy };
            wish = camForward * intent->move.y + camRight * intent->move.x;
            const float wishLen = math::Length(wish);
            moving = wishLen > 1e-3f;
            if (moving) wish = wish * (1.0f / wishLen);
            run = intent->run;
            speed = run ? kCharRunSpeed : kCharWalkSpeed;
        }
        else
        {
            // Canned path for the local-time-scale demo actors: wander along the
            // current heading, reflecting off the ground-slab edges, with a
            // gentle vertical bob. No input, no gravity.
            wish = { std::sin(actor.facingYaw), 0.0f, std::cos(actor.facingYaw) };
            moving = true;
            speed = kDemoActorSpeed;
            actor.phase += dt * 6.0f;
        }

        if (moving)
        {
            actor.pos = actor.pos + wish * (speed * dt);

            // Turn toward the movement direction along the shortest arc.
            const float targetYaw = std::atan2(wish.x, wish.z);
            float delta = targetYaw - actor.facingYaw;
            while (delta > kPi) delta -= 2.0f * kPi;
            while (delta < -kPi) delta += 2.0f * kPi;
            const float maxTurn = kCharTurnRate * dt;
            actor.facingYaw += math::Clamp(delta, -maxTurn, maxTurn);
        }

        if (intent != nullptr)
        {
            // Jump + gravity against the ground plane y = 0. Latched by
            // QueueJump(), consumed here regardless of grounded state.
            if (actor.jumpQueued && actor.grounded)
            {
                actor.verticalVel = kCharJumpSpeed;
                actor.grounded = false;
            }
            actor.jumpQueued = false;
            if (!actor.grounded)
            {
                actor.verticalVel -= kCharGravity * dt;
                actor.pos.y += actor.verticalVel * dt;
                if (actor.pos.y <= actor.groundY)
                {
                    actor.pos.y = actor.groundY;
                    actor.verticalVel = 0.0f;
                    actor.grounded = true;
                }
            }
        }
        else
        {
            actor.pos.y = actor.groundY + 0.4f + 0.25f * std::sin(actor.phase);
        }

        // Reflect the canned actors off the slab edge instead of sticking there.
        if (intent == nullptr)
        {
            if (actor.pos.x < -actor.halfRange || actor.pos.x > actor.halfRange)
                actor.facingYaw = std::atan2(-std::sin(actor.facingYaw), std::cos(actor.facingYaw));
            if (actor.pos.z < -actor.halfRange || actor.pos.z > actor.halfRange)
                actor.facingYaw = std::atan2(std::sin(actor.facingYaw), -std::cos(actor.facingYaw));
        }
        actor.pos.x = math::Clamp(actor.pos.x, -actor.halfRange, actor.halfRange);
        actor.pos.z = math::Clamp(actor.pos.z, -actor.halfRange, actor.halfRange);

        // Locomotion -> which clip the animation state plays. Only
        // (clipIndex, clipTime) crosses into the snapshot; ModelMeshPass3D owns
        // the clip data and does the pose evaluation + CPU skinning.
        if (intent != nullptr && !actor.grounded)
        {
            // Jump: drive the clip by the arc phase (launch 0 -> apex 0.5 ->
            // land 1) instead of wall time, so it stays natural whatever the
            // air time. docs/roadmap.md §2.1.
            const float jumpPhase = math::Clamp(
                0.5f - 0.5f * (actor.verticalVel / kCharJumpSpeed), 0.0f, 1.0f);
            actor.anim.UpdateParametric(dt, Locomotion::Jump, jumpPhase);
        }
        else
        {
            Locomotion loco;
            if (!moving)          loco = Locomotion::Wait;
            else if (run)         loco = Locomotion::Run;
            else                  loco = Locomotion::Walk;
            actor.anim.Update(dt, loco);
        }
    }

    void Simulation::StepSimAgents(float fixedDelta)
    {
        const std::vector<std::uint32_t>& active = m_agents.ActiveIndices();
        if (active.empty()) return;

        SimAgent* slots = m_agents.Slots();

        // Same contiguous-range contract as the particle advect: each job owns a
        // distinct [begin, end) slice of the active-index list. Every entry is a
        // unique slot, so writes to slots[active[k]] never overlap (invariant 6).
        // This is the seed of the mass-object path (docs/instanced-rendering.md §6).
        m_jobs.ParallelFor(0, active.size(), 32,
            [slots, &active, fixedDelta](std::size_t begin, std::size_t end)
            {
                for (std::size_t k = begin; k < end; ++k)
                {
                    SimAgent& a = slots[active[k]];
                    a.phase += fixedDelta * 4.0f;
                    // Walk cycle advances with the agent's speed so the stride
                    // roughly matches its ground movement (kAnimRefSpeed = the
                    // clip's authored travel speed). VAT wraps by frame count.
                    a.animTime += fixedDelta * (a.speed / 1.4f);
                    // Lazy heading drift so the crowd churns without a RNG.
                    a.heading += std::sin(a.phase * 0.11f + static_cast<float>(active[k])) * fixedDelta * 0.9f;

                    const math::Vec3 dir{ std::sin(a.heading), 0.0f, std::cos(a.heading) };
                    a.pos = a.pos + dir * (a.speed * fixedDelta);
                    a.pos.y = 0.2f + 0.15f * std::sin(a.phase);   // small bob above the field

                    // Bounce the heading off the field box edges.
                    if (a.pos.x < -kFieldHalf || a.pos.x > kFieldHalf)
                    {
                        a.heading = -a.heading;
                        a.pos.x = math::Clamp(a.pos.x, -kFieldHalf, kFieldHalf);
                    }
                    if (a.pos.z < kFieldAgentZLo || a.pos.z > kFieldAgentZHi)
                    {
                        a.heading = kPi - a.heading;
                        a.pos.z = math::Clamp(a.pos.z, kFieldAgentZLo, kFieldAgentZHi);
                    }
                }
            }).Wait();

        // Demo churn: recycle one crowd member through the pool every N steps so
        // Acquire / Release / stale-handle rejection stay exercised (this is not
        // a game mechanic - a wave director would own spawn/despawn). Release
        // before Acquire so it is safe even if the pool is at capacity.
        if (!m_agentHandles.empty() && (m_agentChurnCursor % kAgentChurnIntervalSteps) == 0)
        {
            const std::size_t k =
                (m_agentChurnCursor / kAgentChurnIntervalSteps) % m_agentHandles.size();
            m_agents.Release(m_agentHandles[k]);
            const auto handle = m_agents.Acquire();
            m_agentHandles[k] = handle;
            if (SimAgent* a = m_agents.Get(handle))
                SeedAgent(*a, static_cast<float>(m_agentChurnCursor) * 1.37f
                              + static_cast<float>(k) * 2.11f);
        }
        ++m_agentChurnCursor;
    }

    void Simulation::UpdateCrowdQueries()
    {
        if constexpr (kDemoScene != 2) { return; }
        else
        {
            const std::vector<std::uint32_t>& active = m_agents.ActiveIndices();

            // Rebuild-every-step (same pattern as StepCollision2D): cheap for a
            // few hundred colliders, no id bookkeeping needed. One sphere per
            // crowd member; `user` carries the pool slot index so the snapshot
            // can highlight the hit agent. Step() is NOT called - the raycast
            // queries scan the collider list directly.
            m_collision3d.Clear();
            const SimAgent* slots = m_agents.Slots();
            for (const std::uint32_t slotIdx : active)
            {
                const SimAgent& a = slots[slotIdx];
                physics::Collider3D c{};
                c.shape = physics::Collider3D::Shape::Sphere;
                c.center = a.pos + math::Vec3{ 0.0f, kActiveCrowd.height * 0.5f, 0.0f };   // mid-height
                c.radius = kActiveCrowd.colliderRadius;
                c.layer = kLayerCrowd3D;
                c.user = slotIdx;
                m_collision3d.Add(c);
            }

            // "What is the player looking at": a ray from the head along the
            // camera's forward (same basis SnapshotBuilder::BuildCamera uses).
            const float cp = std::cos(m_cameraPitch);
            const float sp = std::sin(m_cameraPitch);
            const math::Vec3 forward{ cp * std::sin(m_cameraYaw), sp, cp * std::cos(m_cameraYaw) };
            const math::Vec3 origin = m_actors[0].pos + math::Vec3{ 0.0f, 1.3f, 0.0f };

            physics::Ray3D ray{};
            ray.origin = origin;
            ray.dir = forward;   // unit: cp^2(s^2+c^2) + sp^2 == 1
            ray.maxDistance = kLookRayRange;
            ray.mask = kLayerCrowd3D;

            m_lookRay = LookRayResult{};
            m_lookRay.origin = origin;
            m_lookRay.dir = forward;
            m_lookRay.length = kLookRayRange;
            if (const std::optional<physics::RayHit3D> hit = m_collision3d.RaycastClosest(ray))
            {
                m_lookRay.hit = true;
                m_lookRay.length = hit->distance;
                m_lookRay.point = hit->point;
                m_lookRay.normal = hit->normal;
                m_lookRay.agentSlot = static_cast<std::uint32_t>(hit->user);
            }

            // Crowd-vs-crowd overlaps via the uniform-grid broadphase. Purely
            // for the demo tint - a real game would drive separation / damage
            // off this. `user` on each collider is the pool slot index.
            m_collision3d.Step();
            m_agentTouch.assign(m_agents.Capacity(), 0);
            for (const physics::Contact& contact : m_collision3d.Contacts())
            {
                if (contact.userA < m_agentTouch.size()) m_agentTouch[contact.userA] = 1;
                if (contact.userB < m_agentTouch.size()) m_agentTouch[contact.userB] = 1;
            }
        }
    }
#endif
}
