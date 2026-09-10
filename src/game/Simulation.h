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
#include "core/ObjectPool.h"
#include "game/CharacterAnimationState.h"
#include "game/CrowdConfig.h"
#include "physics/p3d/CollisionWorld3D.h"
#endif

namespace engine::game
{
    // One frame's movement request for the player, already resolved from raw
    // input. Decouples the simulation from InputState.
    struct PlayerIntent
    {
        math::Vec2 move{};   // x = strafe (+ = right), y = forward (+ = forward), each [-1, 1]
        math::Vec2 look{};   // mouse delta in pixels this frame (x = yaw, y = pitch)
        bool run{ false };   // run modifier held (Shift)
        // Jump is not here: it is an edge that must not be lost on a frame that
        // runs zero fixed steps, so it is latched via Simulation::QueueJump().
    };

    // Demo particle: advects at constant velocity, wraps at world edges.
    struct Particle
    {
        float x{}, y{}, vx{}, vy{};
    };

#if defined(ENGINE_WITH_3D)
    // One moving thing in the 3D demo scene. Homogeneous, so it lives in a plain
    // std::vector (docs/entity-lifecycle-design.md §3A - no EntityId needed until
    // there are entity *kinds* with different component sets).
    //
    // `timeScale` is this actor's LOCAL time dilation, on top of the global
    // scale: its effective step is `fixedDelta * timeScale`, sub-stepped when
    // > 1 so integration stays stable (docs/time-design.md). `ignoreGlobalPause`
    // keeps it advancing while the global scale is 0 (a "time-stop caster").
    struct Actor
    {
        math::Vec3 pos{ 0.0f, 0.0f, 0.0f };   // feet on the ground plane (y = 0)
        float facingYaw{ 0.0f };              // radians; 0 faces +Z
        float verticalVel{ 0.0f };
        bool  grounded{ true };
        bool  jumpQueued{ false };            // set by QueueJump(), consumed by the next step
        bool  playerControlled{ false };      // [0] only: reads PlayerIntent; others run a canned path
        float timeScale{ 1.0f };
        bool  ignoreGlobalPause{ false };
        float phase{ 0.0f };                  // canned-behaviour clock (unused for the player)
        float groundY{ 0.0f };                // surface this actor rests / lands on (cliff top in demo scene 2)
        float halfRange{ 7.5f };              // half-size of the square the actor is clamped to (kCharHalfRange / plateau)
        CharacterAnimationState anim{ render::kUnityChanClips };
    };

    // A lightweight member of the simulation crowd on the lower field (demo
    // scene 2). Homogeneous, so it lives in a core::ObjectPool<SimAgent> with no
    // EntityId (docs/entity-lifecycle-design.md §3A, docs/instanced-rendering.md
    // §6). Stepped with JobSystem::ParallelFor over the pool's active indices -
    // each job owns a distinct [begin, end) range of that list.
    struct SimAgent
    {
        math::Vec3 pos{};        // on the field; y is a small bob above 0
        float heading{ 0.0f };   // radians; 0 faces +Z
        float speed{ 1.0f };     // m/s
        float phase{ 0.0f };     // bob / drift clock
        float animTime{ 0.0f };  // seconds into the crowd VAT clip (per-agent offset + speed-scaled)

        // Required by core::ObjectPool: return a recycled slot to spawn-ready
        // state (SpawnSimAgents / the churn pass then fill the fields).
        void Reset() { *this = SimAgent{}; }
    };

    // Result of the demo "what is the player looking at" raycast against the
    // crowd (CollisionWorld3D). Render-only - it does not feed the simulation.
    // SnapshotBuilder draws the ray + a marker + highlights the hit agent.
    struct LookRayResult
    {
        math::Vec3    origin{};
        math::Vec3    dir{ 0.0f, 0.0f, 1.0f };
        float         length{ 0.0f };          // to the hit, or the full range on a miss
        bool          hit{ false };
        math::Vec3    point{};
        math::Vec3    normal{};
        std::uint32_t agentSlot{ 0 };          // crowd slot index of the hit agent (valid iff hit)
    };
#endif

    // Owns the mutable game world and advances it on a fixed timestep. The only
    // writer of world state; the SnapshotBuilder reads it afterwards. Also owns
    // the collision worlds and runs detection each step (no response - contacts
    // only change demo colors).
    class Simulation final : private core::NonCopyable
    {
    public:
        static constexpr float kPlayerSpeed = 300.0f;   // pixels / second (legacy 2D overlay)
        static constexpr float kPlayerSize = 64.0f;
        static constexpr std::size_t kParticleCount = 20'000;
        static constexpr int kObstacleCount = 3;

#if defined(ENGINE_WITH_3D)
        // Demo character controller (docs/demo-scene.md). Metres / seconds.
        static constexpr float kCharWalkSpeed = 2.2f;
        static constexpr float kCharRunSpeed = 5.2f;
        static constexpr float kCharJumpSpeed = 4.6f;   // initial upward velocity
        static constexpr float kCharGravity = 14.0f;
        static constexpr float kCharTurnRate = 12.0f;   // rad/s toward the move direction
        static constexpr float kCharHalfRange = 7.5f;   // stays on the ground slab
        static constexpr float kMouseSensitivity = 0.0022f;   // rad per pixel of mouse motion
        static constexpr float kCamPitchMin = -1.15f;   // look down
        static constexpr float kCamPitchMax = 0.35f;    // look up

        // Demo scene selector (docs/demo-scene.md). 1 = local-time-scale actors
        // on a small slab. 2 = player on a clifftop overlooking a large field
        // with a wandering simulation crowd below.
        static constexpr int   kDemoScene = 2;
        static constexpr float kCliffTop = 6.0f;         // scene 2: plateau (player) height
        static constexpr float kPlateauHalf = 5.0f;      // scene 2: player's walkable plateau half-size (to the cliff edge)
        static constexpr float kFieldHalf = 30.0f;       // scene 2: lower field half-size
        static constexpr float kLookRayRange = 80.0f;    // scene 2: player look-ray max distance
        // Crowd size / mesh / collider come from game/CrowdConfig.h (kActiveCrowd)
        // so one preset drives Simulation + SnapshotBuilder + MeshPass3D.
#endif

        Simulation(core::JobSystem& jobs, int worldWidth, int worldHeight);

        void SetWorldSize(int width, int height);

        // `globalPaused` (global time scale == 0): the world does not advance,
        // but actors flagged `ignoreGlobalPause` still get one step so a
        // "time-stop" effect can keep one thing moving. Normal frames pass
        // false. Global slow-mo / fast-forward is handled upstream by scaling
        // the delta fed to FixedTimestep::Advance, not here.
        void Step(float fixedDelta, const PlayerIntent& intent, bool globalPaused = false);

#if defined(ENGINE_WITH_3D)
        // Mouse-look for the orbit camera. Called once per frame (not per fixed
        // step) so a frame with 0 or >1 sim steps still turns the camera exactly
        // once by the accumulated mouse delta.
        void UpdateCameraLook(math::Vec2 mouseDelta);

        // Latches a jump request until the next fixed step consumes it, so a
        // Space press on a frame that runs zero steps is not dropped.
        void QueueJump() { m_actors[0].jumpQueued = true; }
#endif

        // --- reads for the snapshot builder ---
        [[nodiscard]] float ElapsedTime() const { return m_elapsed; }
        [[nodiscard]] math::Vec2 PlayerPosition() const { return m_player; }
        [[nodiscard]] bool PlayerBlocked() const { return m_playerBlocked; }
        [[nodiscard]] const std::array<math::Rect, kObstacleCount>& Obstacles() const { return m_obstacles; }
        [[nodiscard]] const std::vector<Particle>& Particles() const { return m_particles; }

#if defined(ENGINE_WITH_3D)
        // The player is actor 0; these stay as thin accessors so the snapshot
        // builder and camera code do not need to know about the actor list.
        [[nodiscard]] math::Vec3 CharacterPosition() const { return m_actors[0].pos; }   // feet on y = 0
        [[nodiscard]] float CharacterFacingYaw() const { return m_actors[0].facingYaw; }
        [[nodiscard]] float CameraYaw() const { return m_cameraYaw; }
        [[nodiscard]] float CameraPitch() const { return m_cameraPitch; }
        [[nodiscard]] AnimPose HeroAnimPose() const { return m_actors[0].anim.Pose(); }
        [[nodiscard]] const std::vector<Actor>& Actors() const { return m_actors; }
        [[nodiscard]] const core::ObjectPool<SimAgent>& SimAgents() const { return m_agents; }
        [[nodiscard]] const LookRayResult& LookRay() const { return m_lookRay; }
        // Per pool slot: 1 if this crowd member overlapped another this step
        // (CollisionWorld3D broadphase). Indexed by slot; sized to the pool.
        [[nodiscard]] const std::vector<std::uint8_t>& AgentTouching() const { return m_agentTouch; }
#endif

    private:
        void SeedParticles();
        void LayOutObstacles();
        void StepCollision2D();
#if defined(ENGINE_WITH_3D)
        void SpawnActors();
        void SpawnSimAgents();
        // Scatters one recycled agent across the field. `seed` (any changing
        // float) drives a deterministic no-RNG spread. Used by SpawnSimAgents
        // and the churn pass in StepSimAgents.
        static void SeedAgent(SimAgent& agent, float seed);
        // Advances every actor by its own local-time-scaled step (sub-stepped
        // when timeScale > 1). `globalPaused` restricts the pass to actors that
        // set `ignoreGlobalPause`.
        void StepActors(float fixedDelta, bool globalPaused, const PlayerIntent& intent);
        // One actor, one sub-step. `intent == nullptr` runs the canned path.
        void StepOneActor(Actor& actor, float dt, const PlayerIntent* intent) const;
        // Advances the demo-scene-2 crowd (JobSystem::ParallelFor, contiguous
        // ranges). No-op when the crowd is empty (scene 1).
        void StepSimAgents(float fixedDelta);
        // Rebuilds m_collision3d from the current crowd (rebuild-every-step, like
        // StepCollision2D) and casts the player look-ray into it, storing
        // m_lookRay for the snapshot. Detection only; does not touch sim state.
        // No-op outside demo scene 2.
        void StepCollision3D();
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
        std::vector<Actor> m_actors;               // [0] = player; [1..] = local-time-scale demo (scene 1)
        core::ObjectPool<SimAgent> m_agents;       // scene 2: wandering crowd on the lower field
        std::vector<core::ObjectPool<SimAgent>::Handle> m_agentHandles;   // one per live crowd member (for the churn pass)
        std::size_t m_agentChurnCursor{ 0 };       // round-robin index into m_agentHandles
        physics::CollisionWorld3D m_collision3d;   // scene 2: crowd colliders, rebuilt each step (raycast + broadphase)
        LookRayResult m_lookRay;                   // scene 2: last player look-ray result (render-only)
        std::vector<std::uint8_t> m_agentTouch;    // scene 2: per pool slot, 1 = overlapped another this step
        float m_cameraYaw{ 0.0f };                 // radians; orbit angle around the player
        float m_cameraPitch{ -0.28f };            // radians; negative looks down at the player
#endif
    };
}
