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
#include "game/GibConfig.h"
#include "game/Ordnance.h"
#include "game/SlowZone.h"
#include "game/vfx/ParticleSystem.h"
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
    // Which 3D demo/test scene is active - runtime-selectable from the title
    // screen's scene-select menu (docs/demo-scene.md), not a rebuild-time
    // constant anymore. Values match the old Simulation::kDemoScene numbering
    // so existing docs/comments referencing "씬 1/2/3" still line up.
    enum class DemoScene : int
    {
        CharacterDemo = 1,   // local-time-scale actors + a small slab
        DefenseCombat = 2,   // hill overlook + SimAgent crowd + weapons/wave loop
        ShadowShowcase = 3,  // no crowd - staircase/pillars for graphics work
        EffectsTest = 4,     // flat range + distance markers - on-demand VFX preview, no crowd/weapons/wave loop
    };

    // Which VFX burst Simulation::PreviewVfxEffect spawns - the EffectsTest
    // scene's whole purpose, decoupled from the rifle/explosion/gib call
    // sites that normally trigger these (docs/particle-system-research.md).
    enum class EffectPreview : std::uint8_t { MuzzleFlash, Explosion, GibBurst };

    // Which weapon Simulation::FireWeapon fires (docs/defense-combat-design.md
    // §8). Declared with all five up front so it does not need to grow again
    // as each is wired - only Rifle is implemented so far (§10 implementation
    // order); the rest are no-ops until their own step.
    enum class WeaponKind : std::uint8_t { Rifle, Mortar, Mine, WireFence, Flamethrower };

    // Combat vs. build/prep (docs/defense-combat-design.md §0). Owned by
    // Simulation, not GameState - Application only knows InGame/WaveResults
    // (scene-flow-design.md), never branches on this.
    enum class MatchPhase : std::uint8_t { Combat, Prep };

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
        math::Vec3 vel{};        // non-zero only while airborne (explosion knockback)
        bool  airborne{ false }; // true = ballistic arc; skip walk/bob until it lands

        // --- base combat layer (docs/defense-combat-design.md §1/§2, §0;
        // docs/horde-design.md §4.5 "무한 리스폰" applied to this AoS pool) ---
        math::Vec3 spawnPoint{};     // returns here on death/goal-reach; set once in SeedAgent, reused
        float      health{ 100.0f }; // Simulation::kAgentMaxHealth (duplicated literal - see Actor::halfRange for precedent)
        bool       reachedGoal{ false }; // this step only: set by the ParallelFor worker, consumed by
                                          // the caller after Wait() (rule 6 - objective HP is shared state)
        math::Vec3 pendingGibPos{};      // death position, captured before RespawnAgentInPlace overwrites pos
        bool       needsGibSpawn{ false }; // this step only: set by a worker-side death branch (airborne
                                            // landing or burn DoT, own slot only - rule 6), consumed after
                                            // Wait() like reachedGoal. DamageAgent's own-thread death path
                                            // spawns gibs directly instead.
        bool       needsKillCount{ false }; // this step only: set ONLY by the burn-DoT death branch, since
                                             // that is the one worker-side death whose kill was never
                                             // counted on the main thread already (unlike TriggerExplosion,
                                             // which bumps m_killCount itself before the agent goes airborne)

        // Flamethrower (docs/defense-combat-design.md §7). Refreshed every
        // cone tick (main thread, ApplyFlameCone) so the burn outlasts a
        // trigger release by burnTimeLeft seconds - ticked down + applied to
        // `health` inside StepSimAgents' worker (own slot only, rule 6).
        float      burnDps{ 0.0f };
        float      burnTimeLeft{ 0.0f };

        // Required by core::ObjectPool: return a recycled slot to spawn-ready
        // state (SpawnSimAgents / the churn pass then fill the fields).
        void Reset() { *this = SimAgent{}; }
    };

    // One flying zombie-part piece spawned on death (docs/defense-combat-
    // design.md §3). Own pool - SRP: gibs don't share SimAgent's health/AI
    // fields, and are stepped with the same ballistic fall as an airborne
    // agent. `selfIndex`/`selfGeneration` mirror the Handle Acquire() returned
    // for this slot so StepGibs can Release() itself without a second
    // parallel handle list (core::ObjectPool has no per-slot generation
    // getter, and GibPiece can't hold a core::ObjectPool<GibPiece>::Handle
    // member - GibPiece would still be incomplete at that point).
    struct GibPiece
    {
        math::Vec3 pos{}, vel{};
        float life{ 0.0f };
        std::uint32_t selfIndex{ 0 }, selfGeneration{ 0 };

        void Reset() { *this = GibPiece{}; }
    };

    // A handful of real 3D cubes at an explosion's core, alongside the vfx::
    // Particle billboards (docs/particle-system-research.md §12 구현 노트
    // "3D로 보이기" - billboards always face the camera, so no amount of
    // rotation/noise on them alone reads as a genuine 3D volume; a few actual
    // opaque cubes tumbling through the same space do, since MeshPass3D
    // shades and occludes them correctly from any angle). Same self-Release
    // pattern as GibPiece. Not scene-gated itself - callers (TriggerExplosion,
    // Simulation::PreviewVfxEffect) each gate on their own active scene.
    struct FireChunk
    {
        math::Vec3 pos{}, vel{};
        float life{ 0.0f };
        float maxLife{ 0.0f };        // life / maxLife drives the shrink-to-nothing "fade" (MeshPass3D is opaque, no alpha)
        float scale{ 0.3f };
        float spin{ 0.0f };           // accumulated tumble angle (drives both RotationX and RotationY, cheap 2-axis tumble)
        float angularVel{ 0.0f };
        // No stored colour - SnapshotBuilder derives a "cooling" yellow->red
        // gradient from life/maxLife instead (same reasoning as GibPiece
        // having no colour field: that is a render concern, not sim state).
        std::uint32_t selfIndex{ 0 }, selfGeneration{ 0 };

        void Reset() { *this = FireChunk{}; }
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

        static constexpr float kHillHeight = 10.0f;      // scene 2: plateau (player) height above the field
        static constexpr float kHillSlopeDeg = 20.0f;    // scene 2: slope angle of the ramp down to the field (from horizontal)
        static constexpr float kPlateauHalf = 5.0f;      // scene 2: player's walkable plateau half-size (flat top, before the slope)
        static constexpr float kFieldHalf = 30.0f;       // scene 2: lower field half-size
        static constexpr float kLookRayRange = 80.0f;    // scene 2: player look-ray max distance
        // Crowd size / mesh / collider come from game/CrowdConfig.h (kActiveCrowd)
        // so one preset drives Simulation + SnapshotBuilder + MeshPass3D.

        // Base combat layer (docs/defense-combat-design.md §0/§1/§2). The field
        // is open (no obstacles), so the crowd seeks straight at a fixed
        // objective point near the base of the mesa instead of a flow field
        // (YAGNI - see that doc's §1 "판단").
        // y = kHillHeight, not 0: the goal sits on the flat hilltop (z=4 is
        // inside the z <= kHillTopZ flat region, docs/demo-scene.md "언덕"), and
        // a climbing agent's own y already tracks the hill height (Simulation.cpp
        // HillHeightAtZ) - the goal must match that or the 3D distance check
        // below can never close while the agent is still partway up the slope.
        static constexpr math::Vec3 kCrowdGoal{ 0.0f, kHillHeight, 4.0f };
        static constexpr float kCrowdGoalRadius = 1.5f;      // "reached the objective" distance
        static constexpr float kAgentSeekTurnRate = 2.5f;    // rad/s toward the goal (gentler than the player's kCharTurnRate)
        static constexpr float kAgentMaxHealth = 100.0f;
        // ~40000 = about 1 minute of unattended breach pressure at the crowd's
        // measured organic steady-state rate (~750 HP/s once the swarm
        // saturates - docs/defense-combat-design.md §0 구현노트). A rough
        // stopgap, not real balance (§0.5 escalation is still deferred) -
        // real player weapon use only makes a wave last longer than this.
        static constexpr float kObjectiveMaxHealth = 40000.0f;
        static constexpr float kObjectiveDamagePerBreach = 20.0f;   // objective HP lost per agent that reaches kCrowdGoal
        static constexpr float kExplosionDamage = 60.0f;             // scaled by TriggerExplosion's existing falloff
        static constexpr float kRifleDamage = 34.0f;                  // 3 hits to kill (kAgentMaxHealth / 34 ~= 3)
        static constexpr float kRifleFireInterval = 0.1f;             // seconds between shots when held (10 rounds/s)
        static constexpr float kMuzzleForwardOffset = 0.4f;           // metres in front of the eye - keeps the flash off the near clip plane in first person
        // Lateral/vertical offset for the virtual muzzle socket (no FPS view-
        // model mesh exists, so this is VFX-only, not a bone attachment) -
        // held off-centre like a real FPS weapon instead of dead-centre on the
        // view axis, where continuous VFX (flamethrower jet) filled the whole
        // screen (playtest report). See MuzzleSocketPosition().
        static constexpr float kMuzzleRightOffset = 0.28f;
        static constexpr float kMuzzleDownOffset = 0.32f;
        static constexpr float kEffectsPreviewDistance = 8.0f;        // scene EffectsTest: metres ahead of the look-ray for Explosion/GibBurst previews
        static constexpr std::size_t kGibPoolCapacity = 256;          // a few dozen simultaneous deaths x a few pieces each

        // Explosion "3D core" chunks (real cubes, not billboards - see
        // FireChunk's comment). A handful per blast is enough to read as
        // volume; this is a look-and-feel accent, not a gameplay pool.
        static constexpr std::size_t kFireChunkPoolCapacity = 64;
        static constexpr int kFireChunkCountMin = 5, kFireChunkCountMax = 8;
        static constexpr float kFireChunkLifeMin = 0.35f, kFireChunkLifeMax = 0.6f;
        static constexpr float kFireChunkSpeedMin = 2.0f, kFireChunkSpeedMax = 5.0f;
        static constexpr float kFireChunkScaleMin = 0.25f, kFireChunkScaleMax = 0.55f;
        static constexpr float kFireChunkAngularVelMax = 10.0f;   // rad/s, tumble rate

        // Mortar/mine (docs/defense-combat-design.md §4) - both just wrap
        // TriggerExplosion with their own radius/power/damage (§4's PlacedOrdnance).
        static constexpr std::size_t kOrdnancePoolCapacity = 64;   // dozens placed at once is plenty (doc's own estimate)
        static constexpr float kMortarFuseSeconds = 1.2f;   // "shell in flight" delay before it lands
        static constexpr float kMortarRadius = 10.0f;
        static constexpr float kMortarPower = 16.0f;
        static constexpr float kMortarDamage = 70.0f;
        static constexpr float kMineRadius = 6.0f;          // also its proximity-trigger radius (one "R", §4)
        static constexpr float kMinePower = 12.0f;
        static constexpr float kMineDamage = 60.0f;

        // Barbed wire (docs/defense-combat-design.md §6) - pure CC, no damage.
        static constexpr std::size_t kMaxSlowZones = 64;   // matches kOrdnancePoolCapacity's "dozens" scale
        static constexpr float kWireRadius = 4.0f;
        static constexpr float kWireSpeedMul = 0.35f;      // 35% speed inside the patch

        // Flamethrower (docs/defense-combat-design.md §7).
        static constexpr float kFlameRange = 8.0f;
        static constexpr float kFlameHalfAngleCos = 0.9397f;   // cos(20 deg) - ~40 deg total cone width
        static constexpr float kFlameDps = 25.0f;              // damage per second while burning
        static constexpr float kFlameRefreshSeconds = 0.35f;   // burnTimeLeft refill per cone tick - keeps burning this long after leaving the cone

        // Wave loop (docs/defense-combat-design.md §0) - core state machine
        // only (§0.1 resource economy, §0.3 results screen, §0.5 escalation
        // are separate, not-yet-built follow-ups; see that doc's own §10).
        static constexpr float kCombatDuration = 120.0f;
        static constexpr float kPrepDuration = 60.0f;

        // Resource economy (docs/defense-combat-design.md §0.1) - v1 is a
        // flat per-kill reward (the crowd is homogeneous, no per-type table
        // yet) spent on placement. No shop UI exists, so the spend side is
        // enforced right in PlaceOrdnance/PlaceSlowZone (insufficient funds =
        // silent no-op, same shape as those already having a "drop, not
        // fatal" failure path for a full pool).
        static constexpr int kSupplyPerKill = 10;
        static constexpr int kMortarCost = 30;
        static constexpr int kMineCost = 20;
        static constexpr int kWireCost = 15;
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

        // Rebuilds the crowd collider world and runs the render-only crowd
        // queries (player look-ray + neighbour-overlap tint). Called ONCE per
        // frame from Application - not per fixed step: it is O(crowd) and not
        // gameplay state, so running it 1-5x per frame turned a slow frame into
        // a spiral (docs/demo-scene.md). No-op outside demo scene 2.
        void UpdateCrowdQueries();

        // Latches a jump request until the next fixed step consumes it, so a
        // Space press on a frame that runs zero steps is not dropped.
        void QueueJump() { m_actors[0].jumpQueued = true; }

        // Radial knockback. Every crowd member within `radius` of `center` gets
        // an outward + upward impulse (linear distance falloff) and goes
        // ballistic until it lands back on the field. Main-thread, applied
        // immediately; the next StepSimAgents integrates the arc. Seed of AoE
        // knockback for the defense genre. No-op outside demo scene 2.
        // `damage` defaults to the original demo blast's constant so the
        // existing call site (Application's F-key/auto blast) is untouched;
        // mortar/mine (§4) pass their own tuned value through the same falloff.
        void TriggerExplosion(math::Vec3 center, float radius, float power, float damage = kExplosionDamage);

        // Damages one crowd member by pool slot index (e.g. LookRayResult's
        // agentSlot - a gun would call this). Main-thread, called before this
        // frame's Step() like TriggerExplosion, so mutating health directly is
        // safe - no ParallelFor is in flight yet this frame (docs/defense-combat-design.md
        // §2). health <= 0 respawns the slot at its spawn point immediately
        // (no ragdoll fling - that is TriggerExplosion's airborne path only).
        // No-op outside demo scene 2 or for an out-of-range slot.
        void DamageAgent(std::uint32_t slot, float amount);

        // Fires `kind` using this frame's aim state (m_lookRay for hitscan
        // weapons - one frame latent, same as everything else that reads it;
        // see LookRayResult). Main-thread, called from Application before
        // Step() (docs/defense-combat-design.md §8). Returns true iff a shot/
        // effect actually happened this call (false when gated out - e.g. the
        // Rifle's fire-rate cooldown hasn't elapsed yet) so the caller can gate
        // one-shot feedback like SFX on the real fire event, not the input edge.
        bool FireWeapon(WeaponKind kind);

        // Scene EffectsTest only (no-op elsewhere): spawns `effect` along the
        // current look-ray, independent of any weapon/crowd/wave-loop state -
        // the whole point of that scene is inspecting VFX in isolation
        // (docs/particle-system-research.md). MuzzleFlash spawns right at the
        // eye (kMuzzleForwardOffset ahead, same as the real rifle); Explosion/
        // GibBurst spawn kEffectsPreviewDistance ahead so scale/visibility at
        // a real combat-ish distance can be judged, not just up close.
        void PreviewVfxEffect(EffectPreview effect);

        // Places a mortar (starts its landing-delay fuse) or a mine (arms,
        // waits for proximity) at the current look-ray's ground hit point
        // (docs/defense-combat-design.md §4 - same aim source as §4's other
        // reads of m_lookRay). Main-thread, called from Application like
        // FireWeapon. No-op outside demo scene 2, or if the look-ray missed
        // (nothing to place on), or if the ordnance pool is full.
        void PlaceOrdnance(OrdnanceKind kind);

        // Places a barbed-wire slow zone at the current look-ray's ground hit
        // point (docs/defense-combat-design.md §6 - same aim source as
        // PlaceOrdnance). Main-thread. No-op outside demo scene 2, if the
        // look-ray missed, or if kMaxSlowZones is already placed.
        void PlaceSlowZone();

        [[nodiscard]] float ObjectiveHealth() const { return m_objectiveHealth; }
        // Scene-gated: m_objectiveHealth can be left at 0 from a previous
        // DefenseCombat loss after switching to a different scene (EnterScene
        // does not reset it - only ResetMatch, entering DefenseCombat, does).
        // Without this check Application::Run() would see IsMatchLost() still
        // true and bounce straight back to the title every frame in whatever
        // scene came next.
        [[nodiscard]] bool IsMatchLost() const
        {
            return m_demoScene == DemoScene::DefenseCombat && m_objectiveHealth <= 0.0f;
        }
        [[nodiscard]] int KillCount() const { return m_killCount; }
        [[nodiscard]] MatchPhase Phase() const { return m_phase; }
        [[nodiscard]] float PhaseTimeLeft() const { return m_phaseTimeLeft; }
        [[nodiscard]] int WaveNumber() const { return m_waveNumber; }
        [[nodiscard]] int Supplies() const { return m_supplies; }

        // Loss handling (docs/defense-combat-design.md §0, "승패"): the caller
        // (Application, on IsMatchLost()) drops back to the title screen and
        // calls this before the next EnterInGame() so a fresh match starts at
        // wave 1 / full objective HP rather than resuming a lost one. Releases
        // all agents/gibs/ordnance/slow zones and re-spawns wave 1.
        void ResetMatch();

        // Switches the active 3D demo/test scene at runtime (docs/demo-scene.md
        // "씬 선택") - the title screen's scene-select menu calls this before
        // entering InGame, and it is safe to call again later to jump to a
        // different scene without restarting the app. Clears the actor list
        // and rebuilds it for `scene`; DefenseCombat additionally goes through
        // ResetMatch() so re-entering it always starts at wave 1. Any crowd/
        // gib/ordnance/particle state left over from a previous DefenseCombat
        // visit is harmless if left behind when leaving it - every function
        // that steps or renders that state is itself gated on the scene being
        // active, so it simply goes untouched until DefenseCombat is re-entered
        // (which resets it fully via ResetMatch() anyway).
        void EnterScene(DemoScene scene);
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
        [[nodiscard]] DemoScene ActiveScene() const { return m_demoScene; }
        [[nodiscard]] AnimPose HeroAnimPose() const { return m_actors[0].anim.Pose(); }
        [[nodiscard]] const std::vector<Actor>& Actors() const { return m_actors; }
        [[nodiscard]] const core::ObjectPool<SimAgent>& SimAgents() const { return m_agents; }
        [[nodiscard]] const LookRayResult& LookRay() const { return m_lookRay; }
        // Per pool slot: 1 if this crowd member overlapped another this step
        // (CollisionWorld3D broadphase). Indexed by slot; sized to the pool.
        [[nodiscard]] const std::vector<std::uint8_t>& AgentTouching() const { return m_agentTouch; }
        [[nodiscard]] const core::ObjectPool<GibPiece>& Gibs() const { return m_gibs; }
        [[nodiscard]] const core::ObjectPool<FireChunk>& FireChunks() const { return m_fireChunks; }
        [[nodiscard]] const core::ObjectPool<PlacedOrdnance>& Ordnance() const { return m_ordnance; }
        [[nodiscard]] const std::vector<SlowZone>& SlowZones() const { return m_slowZones; }
        // Muzzle flash/smoke, explosion, gib blood spray - docs/particle-system-research.md.
        [[nodiscard]] const core::ObjectPool<vfx::Particle>& VfxParticles() const { return m_vfx.Pool(); }
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
        // Returns one slot to spawn-ready state at its OWN spawnPoint - "died
        // or reached the objective, comes back" instead of despawning
        // (docs/horde-design.md §4.5). Touches only `agent`'s own fields, so
        // it is safe to call from inside a ParallelFor worker (rule 6).
        static void RespawnAgentInPlace(SimAgent& agent);
        // Advances every actor by its own local-time-scaled step (sub-stepped
        // when timeScale > 1). `globalPaused` restricts the pass to actors that
        // set `ignoreGlobalPause`.
        void StepActors(float fixedDelta, bool globalPaused, const PlayerIntent& intent);
        // One actor, one sub-step. `intent == nullptr` runs the canned path.
        void StepOneActor(Actor& actor, float dt, const PlayerIntent* intent) const;
        // Advances the demo-scene-2 crowd (JobSystem::ParallelFor, contiguous
        // ranges). No-op when the crowd is empty (scene 1).
        void StepSimAgents(float fixedDelta);
        // Acquires `kZombieGibs.countMin..Max` pieces at `pos` with a
        // deterministic outward+upward scatter (docs/defense-combat-design.md
        // §3). Main-thread only (core::ObjectPool::Acquire) - callers inside a
        // ParallelFor worker must defer via SimAgent::needsGibSpawn instead.
        void SpawnGibs(math::Vec3 pos);
        // Falls/lands/expires every live gib (serial - the pool is small, no
        // ParallelFor needed). Released once `life` runs out.
        void StepGibs(float fixedDelta);
        // Explosion 3D core cubes (FireChunk) - not scene-gated itself, see
        // that struct's comment. Called from TriggerExplosion and
        // PreviewVfxEffect's Explosion case, alongside vfx::SpawnExplosion.
        void SpawnFireChunks(math::Vec3 pos);
        void StepFireChunks(float fixedDelta);
        // Ticks every placed mortar's fuse / checks every mine's proximity
        // radius against the crowd (linear scan, same as TriggerExplosion's
        // own falloff scan - fine at "dozens of ordnance", see §4; a grid
        // would only be worth it at the scale docs/horde-design.md §3.4
        // describes). Triggers TriggerExplosion and releases the slot.
        void StepOrdnance(float fixedDelta);
        // Refreshes burnTimeLeft on every crowd member within `range` of
        // `origin` and inside the `dir`/`halfAngleCos` cone (docs/defense-
        // combat-design.md §7). Main-thread, called every frame the trigger
        // is held (unlike the other weapons' single-shot FireWeapon calls) -
        // linear scan, same reasoning as TriggerExplosion/StepOrdnance's own.
        void ApplyFlameCone(math::Vec3 origin, math::Vec3 dir, float range, float halfAngleCos);
        // Virtual first-person weapon socket for muzzle VFX (rifle flash,
        // flamethrower jet) - offset from the eye/look-ray origin by
        // kMuzzleForwardOffset/RightOffset/DownOffset so effects read as
        // coming from a held weapon instead of erupting dead-centre on the
        // view axis. No FPS view-model mesh exists, so this is VFX-only.
        [[nodiscard]] math::Vec3 MuzzleSocketPosition() const;
        // Ticks the Combat/Prep timer and transitions phases (docs/defense-
        // combat-design.md §0). Runs last in Step() so this frame's other
        // systems still saw the OLD phase - a transition takes effect next
        // frame, avoiding mid-frame reentrancy (e.g. SpawnSimAgents
        // re-Init()ing the pool while StepSimAgents might still be mid-scan).
        void StepMatchPhase(float fixedDelta);
        // Combat -> Prep: sweeps every live agent back to the pool (§0.2).
        // Not a kill - no gibs, no kill-count credit (doc's own call: a
        // few hundred simultaneous gibs would instantly fill that 256 pool).
        void EndCombatPhase();
        // Credits a weapon kill (docs/defense-combat-design.md §0.1/§2) -
        // ++m_killCount + the flat supply reward, together so every kill
        // path (DamageAgent, TriggerExplosion, the burn-DoT post-Wait()
        // pass) stays in sync. NOT called by EndCombatPhase's wave-sweep or
        // ReachedGoal - neither is a weapon kill (§0.1's own distinction).
        void AwardKill();
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
        float m_rifleCooldown{ 0.0f };              // seconds left before the rifle can fire again (§5, full-auto)
        std::vector<std::uint8_t> m_agentTouch;    // scene 2: per pool slot, 1 = overlapped another this step
        DemoScene m_demoScene{ DemoScene::DefenseCombat };   // runtime-selectable (EnterScene) - default is the constructor's first scene
        float m_cameraYaw{ 0.0f };                 // radians; orbit angle around the player
        float m_cameraPitch{ -0.28f };            // radians; negative looks down at the player
        float m_objectiveHealth{ kObjectiveMaxHealth };   // base combat layer (docs/defense-combat-design.md §0)
        int   m_killCount{ 0 };
        core::ObjectPool<GibPiece> m_gibs{ kGibPoolCapacity };   // death VFX pieces (§3); fixed capacity, no per-scene Init needed
        core::ObjectPool<FireChunk> m_fireChunks{ kFireChunkPoolCapacity };   // explosion 3D core cubes, fixed capacity
        core::ObjectPool<PlacedOrdnance> m_ordnance{ kOrdnancePoolCapacity };   // mortars/mines (§4)
        std::vector<SlowZone> m_slowZones;   // barbed wire (§6); never shrinks mid-match, capped at kMaxSlowZones
        vfx::ParticleSystem m_vfx;   // muzzle/explosion/gib particles, distinct from the unrelated m_particles 2D benchmark stub above

        MatchPhase m_phase{ MatchPhase::Combat };   // wave loop (§0)
        float m_phaseTimeLeft{ kCombatDuration };
        int   m_waveNumber{ 1 };
        int   m_supplies{ 0 };   // resource economy (§0.1)
#endif
    };
}
