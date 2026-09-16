#include "game/Simulation.h"

#if defined(ENGINE_WITH_3D)
#include "game/vfx/VfxHooks.h"
#endif

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

        // Hill terrain (docs/demo-scene.md "씬 2"): the flat plateau the player
        // stands on (z <= kHillTopZ, height kHillHeight) ramps down at
        // kHillSlopeDeg to the flat field (z >= kHillBottomZ, height 0). Shared
        // by SeedAgent/StepSimAgents (crowd Y follows the slope as it climbs)
        // and SnapshotBuilder (the ramp mesh). std::tan isn't constexpr, so
        // the derived distances are plain consts, computed once at startup.
        constexpr float kHillTopZ = Simulation::kPlateauHalf + 1.0f;
        const float kHillSlopeRad = Simulation::kHillSlopeDeg * (kPi / 180.0f);
        const float kHillRun = Simulation::kHillHeight / std::tan(kHillSlopeRad);   // horizontal length of the slope
        const float kHillBottomZ = kHillTopZ + kHillRun;

        // Ground height at world Z: flat top, linear ramp, flat field.
        float HillHeightAtZ(float z)
        {
            if (z <= kHillTopZ) return Simulation::kHillHeight;
            if (z >= kHillBottomZ) return 0.0f;
            return Simulation::kHillHeight * (1.0f - (z - kHillTopZ) / kHillRun);
        }

        // Fallback ground hit against the hill terrain (HillHeightAtZ) for when
        // a look/fire ray misses every crowd agent - lets ordnance/wire/
        // explosions be aimed at bare ground instead of only at a live target
        // (docs/defense-combat-design.md §4/§5/§6). March-and-bisect: the
        // terrain here is a simple ramp, so a closed-form solve isn't worth the
        // complexity for a demo-grade raycast (same "cheap enough" call as the
        // linear scans elsewhere in this file, e.g. TriggerExplosion/StepOrdnance).
        bool RaycastTerrain(math::Vec3 origin, math::Vec3 dir, float maxDist, math::Vec3& outPoint)
        {
            constexpr float kStep = 0.5f;
            float prevT = 0.0f;
            float prevDiff = origin.y - HillHeightAtZ(origin.z);
            for (float t = kStep; t <= maxDist; t += kStep)
            {
                const math::Vec3 p = origin + dir * t;
                const float diff = p.y - HillHeightAtZ(p.z);
                if (diff <= 0.0f && prevDiff > 0.0f)
                {
                    float lo = prevT, hi = t;
                    for (int i = 0; i < 8; ++i)   // bisect for a tighter hit point
                    {
                        const float mid = 0.5f * (lo + hi);
                        const math::Vec3 pm = origin + dir * mid;
                        if (pm.y - HillHeightAtZ(pm.z) > 0.0f) lo = mid; else hi = mid;
                    }
                    outPoint = origin + dir * hi;
                    return true;
                }
                prevDiff = diff;
                prevT = t;
            }
            return false;
        }
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

        if (m_demoScene == DemoScene::DefenseCombat)
        {
            // Stand on top of the mesa facing out over the field (+Z). The
            // x/z clamp is symmetric about the origin (halfRange), so the mesa
            // prop is centred there too. First-person (SnapshotBuilder::
            // BuildCamera), so the default view looks straight down the drop
            // at the crowd.
            player.pos = { 0.0f, kHillHeight, -2.0f };
            player.facingYaw = 0.0f;
            player.groundY = kHillHeight;
            player.halfRange = kPlateauHalf;
            m_actors.push_back(std::move(player));

            m_cameraPitch = -0.5f;   // steeper default tilt for the overlook
            ResetMatch();   // fresh wave 1 / full objective HP every (re)entry, not just the first
        }
        else if (m_demoScene == DemoScene::ShadowShowcase)
        {
            // Just the player, centred - no wandering extras to clutter the
            // shadow/lighting showcase (SnapshotBuilder::BuildShadowShowcaseScene).
            player.pos = { 0.0f, 0.0f, 0.0f };
            player.facingYaw = 0.0f;
            m_actors.push_back(std::move(player));
        }
        else if (m_demoScene == DemoScene::EffectsTest)
        {
            // Centred on the range, facing the distance markers (+Z) that
            // SnapshotBuilder::BuildEffectsTestScene lays out at 5/10/20/40m -
            // look-ray previews land among them so scale/visibility at a real
            // distance can be judged, not just right next to the camera.
            player.pos = { 0.0f, 1.6f, 0.0f };
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

    void Simulation::EnterScene(DemoScene scene)
    {
        m_demoScene = scene;
        m_actors.clear();
        m_cameraYaw = 0.0f;
        m_cameraPitch = -0.28f;   // SpawnActors overrides this for DefenseCombat's steeper overlook

        // Release the crowd/gib/ordnance pools unconditionally, whatever the
        // previous scene was - a scene switch away from DefenseCombat must
        // not leave a live crowd behind still being ParallelFor-stepped every
        // frame in a scene that never renders it (same class of wasted-work
        // bug as the pool-reinit stutter fixed earlier this session). No-op
        // if they were already empty.
        for (const auto& handle : m_agentHandles) m_agents.Release(handle);
        m_agentHandles.clear();
        m_agentChurnCursor = 0;
        m_gibs.Init(kGibPoolCapacity);
        m_ordnance.Init(kOrdnancePoolCapacity);
        m_slowZones.clear();
        m_tracers.clear();
        m_muzzleFlashTimer = 0.0f;
        m_rifleSpread = 0.0f;

        SpawnActors();   // DefenseCombat's branch repopulates the crowd via ResetMatch()
    }

    void Simulation::SeedAgent(SimAgent& agent, float seed)
    {
        // Deterministic (no RNG) scatter across the field in front of the hill.
        const float z = kFieldAgentZLo + std::fmod(seed * 3.7f, kFieldAgentZHi - kFieldAgentZLo);
        agent.pos = { std::fmod(seed * 7.13f, 2.0f * kFieldHalf) - kFieldHalf,
                      HillHeightAtZ(z) + 0.2f,
                      z };
        agent.heading = std::fmod(seed * 2.399963f, 2.0f * kPi) - kPi;   // spread out
        agent.speed = 0.8f + std::fmod(seed, 5.0f) * 0.35f;              // 0.8 .. 2.2 m/s
        agent.phase = seed * 0.37f;
        agent.animTime = std::fmod(seed * 0.618f, 3.0f);                 // desync the walk cycle
        agent.spawnPoint = agent.pos;   // returns here on death/goal-reach (§4.5); health/reachedGoal
                                        // are already spawn-ready from ObjectPool::Acquire's Reset()
    }

    void Simulation::RespawnAgentInPlace(SimAgent& agent)
    {
        agent.pos = agent.spawnPoint;
        agent.vel = math::Vec3{};
        agent.airborne = false;
        agent.health = kAgentMaxHealth;
        agent.burnDps = 0.0f;
        agent.burnTimeLeft = 0.0f;   // a fresh spawn doesn't inherit the old slot's burn (§7)
        // reachedGoal is NOT touched here - a caller flagging it (StepSimAgents'
        // goal-reach branch) sets it right after this call returns, and it is
        // the post-Wait() pass that clears it once consumed (rule 6).
        agent.heading = std::atan2(kCrowdGoal.x - agent.pos.x, kCrowdGoal.z - agent.pos.z);
    }

    void Simulation::DamageAgent(std::uint32_t slot, float amount)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            if (amount <= 0.0f || slot >= m_agents.Capacity()) return;
            SimAgent& a = m_agents.Slots()[slot];
            if (a.health <= 0.0f) return;   // already dead this frame (e.g. explosion mid-air)
            a.health -= amount;
            if (a.health <= 0.0f)
            {
                AwardKill();
                SpawnGibs(a.pos);         // before RespawnAgentInPlace overwrites pos (§3); safe here - main thread, no worker in flight
                RespawnAgentInPlace(a);   // no ragdoll fling for a plain hit - that is TriggerExplosion's job
            }
        }
    }

    void Simulation::SpawnSimAgents()
    {
        // Init() destroys and reconstructs every slot - only needed once, on
        // the very first call. Every wave-transition call after that finds
        // the pool already fully free (EndCombatPhase released every handle),
        // so re-Init'ing here was a full pool rebuild every ~60s for nothing
        // (observed as a periodic stutter even while just walking, unrelated
        // to combat load).
        if (m_agents.Capacity() == 0)
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
        m_rifleCooldown = std::max(0.0f, m_rifleCooldown - fixedDelta);   // §5, full-auto fire-rate gate
        m_rifleSpread = std::max(0.0f, m_rifleSpread - kRifleSpreadDecayPerSec * fixedDelta);   // recoil bloom recovery
        m_muzzleFlashTimer = std::max(0.0f, m_muzzleFlashTimer - fixedDelta);
        for (std::size_t i = 0; i < m_tracers.size(); )   // swap-remove expired tracers (same idiom as elsewhere in this file)
        {
            m_tracers[i].ageLeft -= fixedDelta;
            if (m_tracers[i].ageLeft <= 0.0f) { m_tracers[i] = m_tracers.back(); m_tracers.pop_back(); }
            else ++i;
        }
        StepActors(fixedDelta, /*globalPaused=*/false, intent);
        StepOrdnance(fixedDelta);   // before StepSimAgents so a trigger this step still affects this step's crowd integration
        StepSimAgents(fixedDelta);
        StepGibs(fixedDelta);
        StepFireChunks(fixedDelta);
        m_vfx.Step(m_jobs, fixedDelta);
        StepMatchPhase(fixedDelta);
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
            [slots, &active, fixedDelta, &slowZones = m_slowZones](std::size_t begin, std::size_t end)
            {
                for (std::size_t k = begin; k < end; ++k)
                {
                    SimAgent& a = slots[active[k]];

                    if (a.airborne)
                    {
                        // Ballistic arc from an explosion impulse. Same gravity
                        // as the player so falls read consistently. animTime is
                        // left frozen - a running walk cycle mid-air looks wrong.
                        a.vel.y -= Simulation::kCharGravity * fixedDelta;
                        a.pos = a.pos + a.vel * fixedDelta;
                        if (a.pos.y <= 0.0f)
                        {
                            // TriggerExplosion may have already brought health
                            // to 0 while airborne - let the fling finish, then
                            // respawn on landing instead of resuming the walk
                            // (docs/defense-combat-design.md §3 "왜 이게 죽음 같은가").
                            if (a.health <= 0.0f)
                            {
                                // Gibs need core::ObjectPool::Acquire, unsafe inside a
                                // worker - flag it (own slot only) for the post-Wait()
                                // pass below, same pattern as reachedGoal (rule 6).
                                a.pendingGibPos = a.pos;
                                a.needsGibSpawn = true;
                                Simulation::RespawnAgentInPlace(a);
                            }
                            else { a.pos.y = 0.0f; a.vel = math::Vec3{}; a.airborne = false; }
                        }
                        continue;
                    }

                    // Flamethrower DoT (docs/defense-combat-design.md §7): a
                    // direct health write to this worker's own slot is safe
                    // (rule 6) - only the shared kill-count/gib hookup below
                    // defers to the post-Wait() pass. Paused while airborne
                    // (handled above) so the two death paths never overlap.
                    if (a.burnTimeLeft > 0.0f)
                    {
                        a.burnTimeLeft = std::max(0.0f, a.burnTimeLeft - fixedDelta);
                        if (a.health > 0.0f)
                        {
                            a.health -= Simulation::kFlameDps * fixedDelta;
                            if (a.health <= 0.0f)
                            {
                                a.pendingGibPos = a.pos;
                                a.needsGibSpawn = true;
                                a.needsKillCount = true;
                                Simulation::RespawnAgentInPlace(a);
                                continue;
                            }
                        }
                    }

                    a.phase += fixedDelta * 4.0f;
                    // Walk cycle advances with the agent's speed so the stride
                    // roughly matches its ground movement (kAnimRefSpeed = the
                    // clip's authored travel speed). VAT wraps by frame count.
                    a.animTime += fixedDelta * (a.speed / 1.4f);

                    // Seek the objective (docs/defense-combat-design.md §1) - the
                    // field has no obstacles, so a straight-line turn-toward is
                    // enough; a FlowField (horde-design.md) is only worth it once
                    // there is geometry to route around.
                    const math::Vec3 toGoal = Simulation::kCrowdGoal - a.pos;
                    const float distToGoal = math::Length(toGoal);
                    if (distToGoal <= Simulation::kCrowdGoalRadius)
                    {
                        // Reached the objective: respawn now (own slot only,
                        // safe), then flag it - m_objectiveHealth itself is
                        // shared state, decremented by the caller after
                        // Wait() (rule 6), never inside this worker.
                        Simulation::RespawnAgentInPlace(a);
                        a.reachedGoal = true;
                        continue;
                    }

                    const float targetHeading = std::atan2(toGoal.x, toGoal.z);
                    float turnDelta = targetHeading - a.heading;
                    while (turnDelta > kPi) turnDelta -= 2.0f * kPi;
                    while (turnDelta < -kPi) turnDelta += 2.0f * kPi;
                    const float maxTurn = Simulation::kAgentSeekTurnRate * fixedDelta;
                    a.heading += math::Clamp(turnDelta, -maxTurn, maxTurn);

                    // Barbed wire (docs/defense-combat-design.md §6): read-only
                    // scan, safe from inside a worker (m_slowZones is only ever
                    // mutated main-thread, before Step() starts - rule 6).
                    float speedMul = 1.0f;
                    for (const SlowZone& zone : slowZones)
                    {
                        const float zdx = a.pos.x - zone.center.x;
                        const float zdz = a.pos.z - zone.center.z;
                        if (zdx * zdx + zdz * zdz <= zone.radius * zone.radius)
                            speedMul = std::min(speedMul, zone.speedMul);
                    }

                    const math::Vec3 dir{ std::sin(a.heading), 0.0f, std::cos(a.heading) };
                    a.pos = a.pos + dir * (a.speed * speedMul * fixedDelta);
                    a.pos.x = math::Clamp(a.pos.x, -kFieldHalf, kFieldHalf);   // loose side rail only
                    a.pos.y = HillHeightAtZ(a.pos.z) + 0.2f + 0.15f * std::sin(a.phase);   // climb the slope + small bob
                }
            }).Wait();

        // Consume this step's "reached the objective" flags (rule 6 - shared
        // m_objectiveHealth only touched here, serially, after every worker's
        // Wait()). Same pattern as the churn pass right below.
        for (const std::uint32_t idx : active)
        {
            SimAgent& a = slots[idx];
            if (a.reachedGoal)
            {
                a.reachedGoal = false;
                m_objectiveHealth = std::max(0.0f, m_objectiveHealth - kObjectiveDamagePerBreach);
            }
            if (a.needsGibSpawn)
            {
                a.needsGibSpawn = false;
                SpawnGibs(a.pendingGibPos);
            }
            if (a.needsKillCount)
            {
                a.needsKillCount = false;
                AwardKill();   // burn-DoT death only (§7) - other death paths already award this themselves
            }
        }

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

    void Simulation::SpawnGibs(math::Vec3 pos)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            vfx::SpawnGibBurst(m_vfx, pos);   // blood spray alongside the strong gib pieces below (particle-system-research.md §7.3)

            // Deterministic scatter (no <random>, same fmod-hash convention as
            // SeedAgent), seeded off the running kill count so consecutive
            // deaths don't reuse the same directions/count.
            const float seed = static_cast<float>(m_killCount) * 5.437f + 1.0f;
            const int spread = kZombieGibs.countMax - kZombieGibs.countMin + 1;
            const int count = kZombieGibs.countMin + static_cast<int>(std::fmod(seed, static_cast<float>(spread)));
            for (int i = 0; i < count; ++i)
            {
                const float s = seed + static_cast<float>(i) * 13.7f;
                const auto handle = m_gibs.Acquire();
                GibPiece* g = m_gibs.Get(handle);
                if (!g) continue;   // pool full - drop this piece, not fatal (visual only)

                const float yaw = std::fmod(s * 2.399963f, 2.0f * kPi);
                const float speed = kZombieGibs.speedMin + std::fmod(s, 1.0f) * (kZombieGibs.speedMax - kZombieGibs.speedMin);
                g->pos = pos;
                g->vel = { std::sin(yaw) * speed, speed * 0.8f + 1.5f, std::cos(yaw) * speed };
                g->life = kZombieGibs.life;
                g->selfIndex = handle.index;
                g->selfGeneration = handle.generation;
            }
        }
    }

    void Simulation::StepGibs(float fixedDelta)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            GibPiece* slots = m_gibs.Slots();
            // Copy: Release() below swap-removes from the pool's own active
            // list, which would desync a live range-for over that same list.
            const std::vector<std::uint32_t> active = m_gibs.ActiveIndices();
            for (const std::uint32_t idx : active)
            {
                GibPiece& g = slots[idx];
                if (g.pos.y > 0.0f)
                {
                    g.vel.y -= kCharGravity * fixedDelta;   // same fall as the player/airborne agents
                    g.pos = g.pos + g.vel * fixedDelta;
                    if (g.pos.y <= 0.0f) { g.pos.y = 0.0f; g.vel = math::Vec3{}; }   // lands and stops - reads as debris until life runs out
                }
                g.life -= fixedDelta;
                if (g.life <= 0.0f) m_gibs.Release({ g.selfIndex, g.selfGeneration });
            }
        }
    }

    void Simulation::SpawnFireChunks(math::Vec3 pos)
    {
        // Deterministic scatter (no <random>), seeded off the running pool
        // size so consecutive explosions don't reuse the same pattern -
        // there's no kill counter to hook here the way SpawnGibs does, and
        // this needs to work in scene EffectsTest too, where nothing is dying.
        const float seed = static_cast<float>(m_fireChunks.Size()) * 7.919f
                          + static_cast<float>(m_elapsed) * 3.1f + 1.0f;
        const int spread = kFireChunkCountMax - kFireChunkCountMin + 1;
        const int count = kFireChunkCountMin + static_cast<int>(std::fmod(seed, static_cast<float>(spread)));
        for (int i = 0; i < count; ++i)
        {
            const float s = seed + static_cast<float>(i) * 13.7f;
            const auto handle = m_fireChunks.Acquire();
            FireChunk* c = m_fireChunks.Get(handle);
            if (!c) continue;   // pool full - drop this chunk, not fatal (visual only)

            const float yaw = std::fmod(s * 2.399963f, 2.0f * kPi);
            const float pitch = std::fmod(s * 1.618f, kPi * 0.5f);   // 0..pi/2, mostly-outward-and-up
            const float speed = kFireChunkSpeedMin + std::fmod(s, 1.0f) * (kFireChunkSpeedMax - kFireChunkSpeedMin);
            const float ch = std::cos(pitch);
            c->pos = pos;
            c->vel = { std::sin(yaw) * ch * speed, std::sin(pitch) * speed, std::cos(yaw) * ch * speed };
            c->life = kFireChunkLifeMin + std::fmod(s * 1.53f, 1.0f) * (kFireChunkLifeMax - kFireChunkLifeMin);
            c->maxLife = c->life;
            c->scale = kFireChunkScaleMin + std::fmod(s * 0.71f, 1.0f) * (kFireChunkScaleMax - kFireChunkScaleMin);
            c->spin = std::fmod(s * 4.3f, 2.0f * kPi);
            c->angularVel = (std::fmod(s * 2.71f, 2.0f) - 1.0f) * kFireChunkAngularVelMax;
            c->selfIndex = handle.index;
            c->selfGeneration = handle.generation;
        }
    }

    void Simulation::StepFireChunks(float fixedDelta)
    {
        FireChunk* slots = m_fireChunks.Slots();
        const std::vector<std::uint32_t> active = m_fireChunks.ActiveIndices();   // copy: Release() below swap-removes
        for (const std::uint32_t idx : active)
        {
            FireChunk& c = slots[idx];
            c.vel = c.vel * std::max(0.0f, 1.0f - 4.0f * fixedDelta);   // heavy drag - a burst, not a projectile
            c.pos = c.pos + c.vel * fixedDelta;
            c.spin += c.angularVel * fixedDelta;
            c.life -= fixedDelta;
            if (c.life <= 0.0f) m_fireChunks.Release({ c.selfIndex, c.selfGeneration });
        }
    }

    void Simulation::AwardKill()
    {
        ++m_killCount;
        m_supplies += kSupplyPerKill;
    }

    void Simulation::EndCombatPhase()
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            for (const auto& handle : m_agentHandles) m_agents.Release(handle);
            m_agentHandles.clear();
            m_agentChurnCursor = 0;
        }
    }

    void Simulation::ResetMatch()
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            EndCombatPhase();   // releases m_agentHandles, resets churn cursor
            m_gibs.Init(kGibPoolCapacity);
            m_ordnance.Init(kOrdnancePoolCapacity);
            m_slowZones.clear();

            m_objectiveHealth = kObjectiveMaxHealth;
            m_killCount = 0;
            m_supplies = 0;
            m_phase = MatchPhase::Combat;
            m_phaseTimeLeft = kCombatDuration;
            m_waveNumber = 1;

            SpawnSimAgents();
        }
    }

    void Simulation::StepMatchPhase(float fixedDelta)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            m_phaseTimeLeft -= fixedDelta;
            if (m_phaseTimeLeft > 0.0f) return;

            if (m_phase == MatchPhase::Combat)
            {
                EndCombatPhase();
                m_phase = MatchPhase::Prep;
                m_phaseTimeLeft = kPrepDuration;
            }
            else   // Prep -> Combat: next wave
            {
                SpawnSimAgents();
                ++m_waveNumber;
                m_phase = MatchPhase::Combat;
                m_phaseTimeLeft = kCombatDuration;
            }
        }
    }

    void Simulation::TriggerExplosion(math::Vec3 center, float radius, float power, float damage)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            if (radius <= 0.0f) return;
            vfx::SpawnExplosion(m_vfx, center);
            SpawnFireChunks(center);
            const float invR = 1.0f / radius;
            SimAgent* slots = m_agents.Slots();
            for (const std::uint32_t idx : m_agents.ActiveIndices())
            {
                SimAgent& a = slots[idx];
                const float dx = a.pos.x - center.x;
                const float dz = a.pos.z - center.z;
                const float dist2 = dx * dx + dz * dz;
                if (dist2 > radius * radius) continue;

                const float dist = std::sqrt(dist2);
                const float falloff = 1.0f - dist * invR;          // 1 at ground zero -> 0 at the rim
                float nx = 0.0f, nz = 0.0f;                        // outward XZ dir; centre = straight up
                if (dist > 1e-4f) { nx = dx / dist; nz = dz / dist; }

                a.vel.x += nx * power * falloff;
                a.vel.z += nz * power * falloff;
                a.vel.y += power * falloff * 1.1f + 2.0f;          // upward bias so even the rim lifts off
                a.airborne = true;

                // Damage reuses the same distance falloff already computed for
                // knockback (docs/defense-combat-design.md §2). Death while
                // airborne respawns on landing (StepSimAgents), not here - the
                // ragdoll fling should still play out before it returns.
                if (a.health > 0.0f)
                {
                    a.health -= damage * falloff;
                    if (a.health <= 0.0f) AwardKill();
                }
            }
        }
    }

    bool Simulation::FireWeapon(WeaponKind kind)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return false; }
        else
        {
            switch (kind)
            {
            case WeaponKind::Rifle:
            {
                // Full-auto (§5): Application holds the trigger down every
                // frame, this cooldown is what actually paces the rounds.
                if (m_rifleCooldown > 0.0f) return false;
                m_rifleCooldown = kRifleFireInterval;

                // Recoil bloom: the actual shot fires along a direction
                // perturbed within the current spread cone, not the crosshair's
                // exact m_lookRay.dir - that stays precise for the UI/ordnance
                // placement, only the fired bullet drifts under sustained fire.
                // Grows here every shot, recovers in Step() while not firing.
                const math::Vec3 fireDir = ApplyRifleSpread(m_lookRay.dir);
                m_rifleSpread = std::min(kRifleSpreadMax, m_rifleSpread + kRifleSpreadPerShot);

                // Own raycast (not the cached m_lookRay - that was built from
                // the un-perturbed crosshair direction): decides the real hit
                // and gives the tracer an endpoint, with the same agent/terrain
                // fallback as UpdateCrowdQueries.
                physics::Ray3D ray{};
                ray.origin = m_lookRay.origin;
                ray.dir = fireDir;
                ray.maxDistance = kLookRayRange;
                ray.mask = kLayerCrowd3D;

                math::Vec3 tracerEnd = m_lookRay.origin + fireDir * kLookRayRange;
                if (const std::optional<physics::RayHit3D> hit = m_collision3d.RaycastClosest(ray))
                {
                    tracerEnd = hit->point;
                    DamageAgent(static_cast<std::uint32_t>(hit->user), kRifleDamage);
                }
                else if (math::Vec3 groundPoint; RaycastTerrain(m_lookRay.origin, fireDir, kLookRayRange, groundPoint))
                {
                    tracerEnd = groundPoint;
                }

                const math::Vec3 muzzle = MuzzleSocketPosition();
                vfx::SpawnMuzzleFlash(m_vfx, muzzle, fireDir);
                if (m_tracers.size() < kMaxTracers)
                    m_tracers.push_back({ muzzle, tracerEnd, kTracerLife, kTracerLife });
                m_muzzleFlashTimer = kMuzzleFlashDarkenTime;
                return true;
            }
            case WeaponKind::Mortar:
            case WeaponKind::Mine:
                return false;   // placed via PlaceOrdnance instead (§4/§8 - not an instant fire-and-forget)
            case WeaponKind::WireFence:
                return false;   // placed via PlaceSlowZone instead (§6/§8, same reasoning as Mortar/Mine)
            case WeaponKind::Flamethrower:
                // §7 - continuous: Application calls FireWeapon every frame
                // the trigger is held, not just on the press edge like Rifle.
                vfx::SpawnFlameJet(m_vfx, MuzzleSocketPosition(), m_lookRay.dir);
                ApplyFlameCone(m_lookRay.origin, m_lookRay.dir, kFlameRange, kFlameHalfAngleCos);
                return true;
            }
            return false;
        }
    }

    void Simulation::PreviewVfxEffect(EffectPreview effect)
    {
        if (m_demoScene != DemoScene::EffectsTest) return;

        switch (effect)
        {
        case EffectPreview::MuzzleFlash:
            vfx::SpawnMuzzleFlash(m_vfx, m_lookRay.origin + m_lookRay.dir * kMuzzleForwardOffset, m_lookRay.dir);
            break;
        case EffectPreview::Explosion:
            vfx::SpawnExplosion(m_vfx, m_lookRay.origin + m_lookRay.dir * kEffectsPreviewDistance);
            SpawnFireChunks(m_lookRay.origin + m_lookRay.dir * kEffectsPreviewDistance);
            break;
        case EffectPreview::GibBurst:
            vfx::SpawnGibBurst(m_vfx, m_lookRay.origin + m_lookRay.dir * kEffectsPreviewDistance);
            break;
        }
    }

    void Simulation::PlaceOrdnance(OrdnanceKind kind)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            if (!m_lookRay.hit) return;   // nothing to place on
            const int cost = kind == OrdnanceKind::Mortar ? kMortarCost : kMineCost;
            if (m_supplies < cost) return;   // §0.1 - insufficient funds, no shop UI yet so this is the whole gate

            const auto handle = m_ordnance.Acquire();
            PlacedOrdnance* o = m_ordnance.Get(handle);
            if (!o) return;   // pool full - drop the placement, not fatal (funds NOT spent)

            m_supplies -= cost;
            o->kind = kind;
            o->pos = m_lookRay.point;
            if (kind == OrdnanceKind::Mortar)
            {
                o->fuseSeconds = kMortarFuseSeconds;
                o->radius = kMortarRadius;
                o->power = kMortarPower;
                o->damage = kMortarDamage;
            }
            else   // Mine
            {
                o->fuseSeconds = 0.0f;   // proximity trigger, not a timer - see StepOrdnance
                o->radius = kMineRadius;
                o->power = kMinePower;
                o->damage = kMineDamage;
            }
            o->selfIndex = handle.index;
            o->selfGeneration = handle.generation;
        }
    }

    void Simulation::StepOrdnance(float fixedDelta)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            PlacedOrdnance* slots = m_ordnance.Slots();
            // Copy: Release() below swap-removes from the pool's own active
            // list, which would desync a live range-for over that same list
            // (same reasoning as StepGibs).
            const std::vector<std::uint32_t> active = m_ordnance.ActiveIndices();
            for (const std::uint32_t idx : active)
            {
                PlacedOrdnance& o = slots[idx];
                bool trigger = false;
                if (o.kind == OrdnanceKind::Mortar)
                {
                    o.fuseSeconds -= fixedDelta;
                    trigger = o.fuseSeconds <= 0.0f;
                }
                else   // Mine: trigger the moment anything living enters its radius
                {
                    for (const std::uint32_t agentIdx : m_agents.ActiveIndices())
                    {
                        const SimAgent& a = m_agents.Slots()[agentIdx];
                        const float dx = a.pos.x - o.pos.x;
                        const float dz = a.pos.z - o.pos.z;
                        if (dx * dx + dz * dz <= o.radius * o.radius) { trigger = true; break; }
                    }
                }

                if (!trigger) continue;
                TriggerExplosion(o.pos, o.radius, o.power, o.damage);
                m_ordnance.Release({ o.selfIndex, o.selfGeneration });
            }
        }
    }

    void Simulation::PlaceSlowZone()
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            if (!m_lookRay.hit) return;                      // nothing to place on
            if (m_slowZones.size() >= kMaxSlowZones) return;  // fixed cap (§6) - drop, not fatal
            if (m_supplies < kWireCost) return;               // §0.1 - insufficient funds
            m_supplies -= kWireCost;
            m_slowZones.push_back({ m_lookRay.point, kWireRadius, kWireSpeedMul });
        }
    }

    math::Vec3 Simulation::MuzzleSocketPosition() const
    {
        // right = cross(up, forward), matching math::LookAtLH's xAxis (docs/
        // particle-system-research.md §12's camera-axis note) - the pitch
        // component of `forward` cancels out of this cross product, so plain
        // yaw is enough for a horizontal right vector (no camera roll exists
        // in this game).
        const float cy = std::cos(m_cameraYaw), sy = std::sin(m_cameraYaw);
        const math::Vec3 right{ cy, 0.0f, -sy };
        return m_lookRay.origin + m_lookRay.dir * kMuzzleForwardOffset
             + right * kMuzzleRightOffset
             + math::Vec3{ 0.0f, -kMuzzleDownOffset, 0.0f };
    }

    math::Vec3 Simulation::ApplyRifleSpread(math::Vec3 dir)
    {
        if (m_rifleSpread <= 0.0f) return dir;

        dir = math::Normalized(dir);
        const math::Vec3 up = std::fabs(dir.y) > 0.99f ? math::Vec3{ 1.0f, 0.0f, 0.0f } : math::Vec3{ 0.0f, 1.0f, 0.0f };
        const math::Vec3 tangent = math::Normalized(math::Cross(up, dir));
        const math::Vec3 bitangent = math::Cross(dir, tangent);

        // Same fmod-based, no-<random> convention as vfx::ParticleSystem::
        // NextRandom01/Simulation::SeedAgent - deterministic, and this is
        // purely visual/gameplay scatter, not anything needing real entropy.
        m_rifleSpreadSeed = std::fmod(m_rifleSpreadSeed * 16807.0f + 1.0f, 2147483647.0f);
        const float r1 = std::fmod(m_rifleSpreadSeed, 1000.0f) / 1000.0f;
        m_rifleSpreadSeed = std::fmod(m_rifleSpreadSeed * 16807.0f + 1.0f, 2147483647.0f);
        const float r2 = std::fmod(m_rifleSpreadSeed, 1000.0f) / 1000.0f;

        const float phi = r1 * 2.0f * kPi;
        const float theta = r2 * m_rifleSpread;
        const float st = std::sin(theta), ct = std::cos(theta);
        return (tangent * (st * std::cos(phi))) + (bitangent * (st * std::sin(phi))) + (dir * ct);
    }

    void Simulation::ApplyFlameCone(math::Vec3 origin, math::Vec3 dir, float range, float halfAngleCos)
    {
        if (m_demoScene != DemoScene::DefenseCombat) { return; }
        else
        {
            SimAgent* slots = m_agents.Slots();
            for (const std::uint32_t idx : m_agents.ActiveIndices())
            {
                SimAgent& a = slots[idx];
                const math::Vec3 toAgent = a.pos - origin;
                const float dist = math::Length(toAgent);
                if (dist > range || dist < 1e-4f) continue;
                if (math::Dot(toAgent * (1.0f / dist), dir) < halfAngleCos) continue;   // outside the cone

                a.burnDps = kFlameDps;
                a.burnTimeLeft = kFlameRefreshSeconds;   // refill - the burn outlasts release by this much
            }
        }
    }

    void Simulation::UpdateCrowdQueries()
    {
        // Also runs for EffectsTest (no crowd there - the collider loop below
        // just iterates zero and the raycast always misses) so m_lookRay.
        // origin/dir are still populated for PreviewVfxEffect to aim with.
        if (m_demoScene != DemoScene::DefenseCombat && m_demoScene != DemoScene::EffectsTest) { return; }
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
                m_lookRay.hitAgent = true;
                m_lookRay.length = hit->distance;
                m_lookRay.point = hit->point;
                m_lookRay.normal = hit->normal;
                m_lookRay.agentSlot = static_cast<std::uint32_t>(hit->user);
            }
            else if (math::Vec3 groundPoint; RaycastTerrain(origin, forward, kLookRayRange, groundPoint))
            {
                // Terrain fallback (RaycastTerrain above) - aims ordnance/wire
                // placement at bare ground. hitAgent stays false so the rifle
                // (FireWeapon) doesn't mistake this for an agent hit.
                m_lookRay.hit = true;
                m_lookRay.length = math::Length(groundPoint - origin);
                m_lookRay.point = groundPoint;
                m_lookRay.normal = { 0.0f, 1.0f, 0.0f };
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
