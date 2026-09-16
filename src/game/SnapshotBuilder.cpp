#include "game/SnapshotBuilder.h"

#include "math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ENGINE_WITH_3D)
#include "game/vfx/ParticleSystem.h"
#include "math/Math3D.h"
#endif

namespace engine::game
{
    namespace
    {
#if defined(ENGINE_WITH_3D)
        constexpr float kPi = 3.14159265358979323846f;

        // Unity-chan's mesh faces +Z in its local space, same as the engine's
        // "forward"; if the character ever runs backwards, flip this to kPi.
        constexpr float kModelYawOffset = 0.0f;

        // --- crowd culling (docs/instanced-rendering.md §5) -------------------
        // A plane a*x + b*y + c*z + d >= 0 marks the inside half-space.
        struct Plane { float a, b, c, d; };
        struct Frustum { Plane p[6]; };

        Plane NormalizePlane(Plane p)
        {
            const float inv = 1.0f / std::sqrt(p.a * p.a + p.b * p.b + p.c * p.c);
            return { p.a * inv, p.b * inv, p.c * inv, p.d * inv };
        }

        // Gribb-Hartmann from a row-major, row-vector view*projection. Column k
        // of vp is (m[k], m[4+k], m[8+k], m[12+k]); clip.x = worldHom . col0,
        // clip.w = worldHom . col3. left = col3 + col0, right = col3 - col0, ...
        // near = col2 (this engine's projections put clip z in [0, w]).
        Frustum MakeFrustum(const math::Mat4& vp)
        {
            const auto plane = [&](int k, float s) {
                return NormalizePlane({ vp.m[3] + s * vp.m[k],
                                        vp.m[7] + s * vp.m[4 + k],
                                        vp.m[11] + s * vp.m[8 + k],
                                        vp.m[15] + s * vp.m[12 + k] });
            };
            Frustum f{};
            f.p[0] = plane(0, +1.0f);   // left
            f.p[1] = plane(0, -1.0f);   // right
            f.p[2] = plane(1, +1.0f);   // bottom
            f.p[3] = plane(1, -1.0f);   // top
            f.p[4] = NormalizePlane({ vp.m[2], vp.m[6], vp.m[10], vp.m[14] });   // near
            f.p[5] = plane(2, -1.0f);   // far
            return f;
        }

        bool SphereInFrustum(const Frustum& f, math::Vec3 c, float r)
        {
            for (const Plane& pl : f.p)
                if (pl.a * c.x + pl.b * c.y + pl.c * c.z + pl.d < -r) return false;
            return true;
        }

        // Camera position from a LookAtLH view matrix (orthonormal 3x3 +
        // translation row = -(axis . eye)).
        math::Vec3 EyeFromView(const math::Mat4& v)
        {
            const float tx = v.m[12], ty = v.m[13], tz = v.m[14];
            return { -(tx * v.m[0] + ty * v.m[1] + tz * v.m[2]),
                     -(tx * v.m[4] + ty * v.m[5] + tz * v.m[6]),
                     -(tx * v.m[8] + ty * v.m[9] + tz * v.m[10]) };
        }

        std::uint32_t PackRgba(float r, float g, float b, float a)
        {
            const auto u8 = [](float v) {
                return static_cast<std::uint32_t>(math::Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return u8(r) | (u8(g) << 8) | (u8(b) << 16) | (u8(a) << 24);
        }

        // Scene 2 is first-person (no visible self - see BuildScene3D's ModelDraw
        // skip below): the eye sits exactly at the look-ray origin, no backward
        // pull, so muzzle flashes spawned at that same point read as "coming
        // from the camera" instead of floating beside a body you can't see
        // properly anyway. Scenes 1/3 keep the original third-person orbit
        // (sits behind + above the character, looks at a point near the chest).
        render::CameraView BuildCamera(const Simulation& simulation, int viewportWidth, int viewportHeight)
        {
            const float aspect = viewportHeight > 0
                ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
                : 1.0f;

            const float yaw = simulation.CameraYaw();
            const float pitch = simulation.CameraPitch();
            const float cp = std::cos(pitch), sp = std::sin(pitch);
            // Unit vector from the focus point toward where the camera looks.
            const math::Vec3 forward{ cp * std::sin(yaw), sp, cp * std::cos(yaw) };

            const math::Vec3 eyePoint = simulation.CharacterPosition() + math::Vec3{ 0.0f, 1.3f, 0.0f };

            render::CameraView camera{};
            if (simulation.ActiveScene() == DemoScene::DefenseCombat || simulation.ActiveScene() == DemoScene::EffectsTest)
            {
                camera.view = math::LookAtLH(eyePoint, eyePoint + forward, { 0.0f, 1.0f, 0.0f });
            }
            else
            {
                // Scene 3 (shadow showcase) sits between the two - far enough to
                // see the staircase/pillars around the player.
                const float orbitDistance =
                    simulation.ActiveScene() == DemoScene::ShadowShowcase ? 5.5f : 3.6f;
                const math::Vec3 eye = eyePoint - forward * orbitDistance;
                camera.view = math::LookAtLH(eye, eyePoint, { 0.0f, 1.0f, 0.0f });
            }
            camera.projection = math::PerspectiveFovLH(kPi / 3.0f, aspect, 0.05f, 100.0f);
            return camera;
        }

        // One [span, depth, eyeDist] ortho box, aimed back along `dir` at `center`.
        math::Mat4 FitShadowOrtho(const math::Vec3& dir, const math::Vec3& center,
                                  float span, float depth, float eyeDist)
        {
            const math::Vec3 eye = center - dir * eyeDist;
            const math::Mat4 view = math::LookAtLH(eye, center, { 0.0f, 1.0f, 0.0f });
            const math::Mat4 proj = math::OrthographicLH(span, span, 0.1f, depth);
            return view * proj;
        }

        render::Lighting BuildLighting(const Simulation& simulation, float elapsed)
        {
            render::Lighting lighting{};
            // Front-top 45 deg key that sweeps left<->right across the front so
            // the shading (and shadows) are visibly in motion.
            const float sweep = std::sin(elapsed * 0.5f) * 0.6f;   // ~+-34 deg around Y
            const float c = std::cos(sweep), s = std::sin(sweep);
            lighting.key.direction = { 0.70711f * s, -0.70711f, 0.70711f * c };
            lighting.key.color = { 1.0f, 0.97f, 0.90f, 1.35f };    // rgb, a = intensity
            lighting.ambient.sky = { 0.36f, 0.40f, 0.48f, 1.0f };
            lighting.ambient.ground = { 0.22f, 0.20f, 0.18f, 1.0f };

            // Rifle muzzle flash contrast (docs/defense-combat-design.md §5):
            // dim the scene briefly right after a shot so the additive flash
            // particle reads clearly against it instead of blending into an
            // already-bright frame - fades back to normal over
            // kMuzzleFlashDarkenTime. No-op (dim = 1) outside DefenseCombat -
            // MuzzleFlashDarken() is always 0 there since nothing sets the timer.
            const float dim = 1.0f - simulation.MuzzleFlashDarken() * Simulation::kMuzzleFlashDarkenAmount;
            lighting.key.color.a *= dim;
            lighting.ambient.sky.r *= dim; lighting.ambient.sky.g *= dim; lighting.ambient.sky.b *= dim;
            lighting.ambient.ground.r *= dim; lighting.ambient.ground.g *= dim; lighting.ambient.ground.b *= dim;

            // Cascaded directional shadow (docs/shadows.md "캐스케이드"):
            // cascade 0 is a tight box around the player in every scene - this
            // is what actually gives the character a crisp shadow. Cascade 1 is
            // a wider, per-scene box for whatever else needs a shadow at a
            // distance (the crowd field in scene 2, pillars in scene 3, or just
            // a safety margin in scene 1). SampleShadow (common3d.hlsli) tries
            // cascade 0 first and falls back to cascade 1.
            const math::Vec3 dir = math::Normalized(lighting.key.direction);
            const math::Vec3 focus = simulation.CharacterPosition() + math::Vec3{ 0.0f, 1.0f, 0.0f };
            lighting.cascadeViewProj[0] = FitShadowOrtho(dir, focus, 12.0f, 28.0f, 11.0f);

            if (simulation.ActiveScene() == DemoScene::DefenseCombat)
            {
                // The crowd spreads across a wide field ahead of the mesa - widen
                // the frustum and push its centre out toward it.
                lighting.cascadeViewProj[1] =
                    FitShadowOrtho(dir, { 0.0f, 1.0f, 18.0f }, 64.0f, 90.0f, 32.0f);
            }
            else if (simulation.ActiveScene() == DemoScene::ShadowShowcase)
            {
                // Wide enough to cover the far pillars (BuildShadowShowcaseScene,
                // out to ~24m) with margin, centred on the player like cascade 0.
                lighting.cascadeViewProj[1] = FitShadowOrtho(dir, focus, 50.0f, 80.0f, 28.0f);
            }
            else
            {
                // Scene 1 is small enough that cascade 0 covers almost
                // everything - this is just a safety margin past it.
                lighting.cascadeViewProj[1] = FitShadowOrtho(dir, focus, 30.0f, 60.0f, 22.0f);
            }

            lighting.shadowsEnabled = true;
            return lighting;
        }

        // AO strength/radius match what was already visually confirmed
        // (docs/post-process-gbuffer-research.md §4). Fog fades
        // toward the camera's far clip plane (100, see BuildCamera) so it also
        // masks the crowd's distance cull (instanced-rendering.md) instead of
        // objects just popping out of existence.
        render::PostProcessSettings BuildPostProcess()
        {
            render::PostProcessSettings settings{};
            settings.fogEnabled = true;
            settings.fogNear = 40.0f;
            settings.fogFar = 100.0f;
            return settings;
        }

        // Test-scene props: boxes of a few sizes for the model and each other to
        // cast shadows on. (The old 3D cube/collision demo is disabled.)
        struct DemoBox { math::Vec3 scale; math::Vec3 pos; math::Color color; };
        constexpr DemoBox kBoxes[] = {
            { { 2.5f, 1.0f, 2.0f }, { -3.2f, 0.50f,  1.0f }, { 0.60f, 0.62f, 0.68f, 1.0f } },
            { { 1.4f, 0.4f, 1.4f }, { -3.2f, 1.20f,  1.0f }, { 0.58f, 0.64f, 0.72f, 1.0f } },  // slab on top
            { { 1.0f, 2.4f, 1.0f }, {  3.0f, 1.20f, -0.6f }, { 0.72f, 0.56f, 0.50f, 1.0f } },
            { { 0.8f, 0.8f, 0.8f }, {  2.2f, 0.40f,  2.2f }, { 0.55f, 0.70f, 0.62f, 1.0f } },
            { { 0.55f,0.55f,0.55f}, {  2.35f,1.08f,  2.3f }, { 0.66f, 0.62f, 0.78f, 1.0f } },
            { { 4.0f, 0.5f, 1.0f }, {  0.2f, 0.25f, -3.2f }, { 0.68f, 0.68f, 0.68f, 1.0f } },
            { { 0.4f, 1.7f, 0.4f }, { -2.4f, 0.85f, -2.2f }, { 0.74f, 0.70f, 0.56f, 1.0f } },
        };

        // Demo scene 2: a large field with the player standing on a low hill
        // that ramps down in front of them at kHillSlopeDeg, and a wandering
        // simulation crowd climbing it from the field below. Props only - the
        // player model + crowd stepping live in Simulation. See docs/demo-scene.md.
        void BuildCliffScene(render::Scene3D& scene, const Simulation& simulation)
        {
            const float fieldHalf = Simulation::kFieldHalf;
            const float hillHeight = Simulation::kHillHeight;

            // Wide lower field, flat at y = 0.
            render::MeshDraw field{};
            field.mesh = render::MeshId::Plane;
            field.world = math::Scaling({ fieldHalf * 2.8f, 1.0f, fieldHalf * 2.8f })
                        * math::Translation({ 0.0f, 0.0f, fieldHalf * 0.6f });
            field.color = { 0.33f, 0.40f, 0.30f, 1.0f };
            scene.meshDraws.push_back(field);

            // Hill geometry (mirrors Simulation.cpp's HillHeightAtZ - std::tan
            // isn't constexpr, so this is the same 3-line derivation recomputed
            // here from the shared public constants, same convention the old
            // mesaFrontZ/mesaDepth locals used).
            const float hillTopZ = Simulation::kPlateauHalf + 1.0f;
            const float hillSlopeRad = Simulation::kHillSlopeDeg * (kPi / 180.0f);
            const float hillRun = hillHeight / std::tan(hillSlopeRad);
            const float hillBottomZ = hillTopZ + hillRun;
            const float hillWidth = Simulation::kPlateauHalf * 2.0f + 8.0f;

            // Flat plateau top the player stands on: a block whose top face is
            // y = hillHeight, front face flush with where the ramp begins
            // (hillTopZ) so the two meet with no gap. Extends back under the
            // camera the same way the old mesa did.
            const float plateauDepth = hillTopZ + 22.0f;
            render::MeshDraw plateau{};
            plateau.mesh = render::MeshId::Cube;
            plateau.world = math::Scaling({ hillWidth, hillHeight, plateauDepth })
                          * math::Translation({ 0.0f, hillHeight * 0.5f, hillTopZ - plateauDepth * 0.5f });
            plateau.color = { 0.42f, 0.38f, 0.34f, 1.0f };
            scene.meshDraws.push_back(plateau);

            // The ramp: a plane the width of the plateau, its unrotated length
            // equal to the slope's hypotenuse, tilted by RotationX(slopeRad) so
            // its near edge lands exactly on the plateau's front face (hillTopZ,
            // hillHeight) and its far edge on the field (hillBottomZ, 0) - see
            // Simulation.cpp's HillHeightAtZ comment for the same geometry used
            // to keep the crowd's feet on this surface as they climb it.
            const float hillSlopeLength = hillHeight / std::sin(hillSlopeRad);
            render::MeshDraw ramp{};
            ramp.mesh = render::MeshId::Plane;
            ramp.world = math::Scaling({ hillWidth, 1.0f, hillSlopeLength })
                       * math::RotationX(hillSlopeRad)
                       * math::Translation({ 0.0f, hillHeight * 0.5f, hillTopZ + hillRun * 0.5f });
            ramp.color = { 0.37f, 0.42f, 0.32f, 1.0f };
            scene.meshDraws.push_back(ramp);

            // A couple of markers for depth / scale reference, sitting on
            // whatever part of the hill/field their z lands on.
            struct PillarXZ { float x, z; };
            const PillarXZ pillarXz[] = { { -16.0f, 22.0f }, { 18.0f, 30.0f }, { 4.0f, 40.0f } };
            for (const auto& [px, pz] : pillarXz)
            {
                const float groundY = pz <= hillTopZ ? hillHeight
                                    : pz >= hillBottomZ ? 0.0f
                                    : hillHeight * (1.0f - (pz - hillTopZ) / hillRun);
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ 1.2f, 3.0f, 1.2f }) * math::Translation({ px, groundY + 1.5f, pz });
                draw.color = { 0.55f, 0.52f, 0.48f, 1.0f };
                scene.meshDraws.push_back(draw);
            }

            // The simulation crowd: one instanced draw per LOD batch
            // (docs/instanced-rendering.md §5). Sizing / mesh / height come from
            // game/CrowdConfig.h (kActiveCrowd) - Cube or the FBX MeshPass3D
            // loaded into MeshId::CrowdModel (bind pose, static; animation = VAT,
            // docs/horde-design.md §5). Frustum + distance culled. Distance LOD,
            // 2 tiers: bucket 0 = near (casts shadow), bucket 2 = far (no
            // shadow). Bucket 1 (reduced mid / billboard) reserved.
            const bool crowdIsModel = CrowdUsesModel();
            const render::MeshId crowdMesh =
                crowdIsModel ? render::MeshId::CrowdModel : render::MeshId::Cube;
            const render::InstanceShader crowdShader =
                kActiveCrowd.shading == CrowdShading::Toon ? render::InstanceShader::Toon
                                                          : render::InstanceShader::Lit;
            const float crowdHeight = kActiveCrowd.height;
            const math::Vec3 pivotLift =
                crowdIsModel ? math::Vec3{ 0.0f, 0.0f, 0.0f }               // FBX pivot at the feet
                             : math::Vec3{ 0.0f, crowdHeight * 0.5f, 0.0f }; // cube pivot at the centre
            constexpr float kAgentCullDist = 100.0f;    // past this, skip entirely
            constexpr float kAgentShadowDist = 34.0f;   // past this, LOD 2: no shadow cast
            const math::Mat4 viewProj = scene.camera.view * scene.camera.projection;
            const Frustum frustum = MakeFrustum(viewProj);
            const math::Vec3 eye = EyeFromView(scene.camera.view);

            const core::ObjectPool<SimAgent>& pool = simulation.SimAgents();
            const SimAgent* agentSlots = pool.Slots();
            const LookRayResult& look = simulation.LookRay();
            const std::vector<std::uint8_t>& touching = simulation.AgentTouching();

            std::vector<render::MeshInstance> lodBucket[3];
            for (const std::uint32_t slotIdx : pool.ActiveIndices())
            {
                const SimAgent& a = agentSlots[slotIdx];
                // Cull against a sphere around the agent's mid-height.
                const math::Vec3 mid = a.pos + math::Vec3{ 0.0f, crowdHeight * 0.5f, 0.0f };
                if (!SphereInFrustum(frustum, mid, crowdHeight * 0.6f)) continue;
                const math::Vec3 d = mid - eye;
                const float d2 = math::Dot(d, d);
                if (d2 > kAgentCullDist * kAgentCullDist) continue;

                render::MeshInstance inst{};
                inst.pos = a.pos + pivotLift;
                inst.yaw = a.heading;
                inst.scale = crowdHeight;
                inst.animTime = a.animTime;   // ignored by MeshPass3D when no crowd VAT is baked
                // `colorRgba` multiplies the diffuse texture in the shader, so
                // the base must be ~white (not a dark tint) or the textured
                // zombie goes muddy. Highlights are a saturated multiply -
                // still clearly readable against a near-white crowd.
                if (look.hitAgent && look.agentSlot == slotIdx)
                {
                    inst.colorRgba = PackRgba(1.0f, 0.82f, 0.15f, 1.0f);   // look-ray target (gold)
                }
                else if (a.burnTimeLeft > 0.0f)
                {
                    inst.colorRgba = PackRgba(1.0f, 0.35f, 0.05f, 1.0f);   // burning (§7)
                }
                else if (slotIdx < touching.size() && touching[slotIdx] != 0)
                {
                    inst.colorRgba = PackRgba(1.0f, 0.42f, 0.34f, 1.0f);   // overlapping a neighbour (red)
                }
                else
                {
                    const float hot = math::Clamp((a.speed - 0.8f) / 1.4f, 0.0f, 1.0f);
                    inst.colorRgba = crowdIsModel
                        ? PackRgba(0.94f + 0.06f * hot, 0.94f - 0.02f * hot, 0.90f - 0.10f * hot, 1.0f)  // let the texture show
                        : PackRgba(0.34f + 0.22f * hot, 0.44f + 0.10f * hot, 0.30f - 0.06f * hot, 1.0f); // mottled-green cube
                }

                const int lod = d2 <= kAgentShadowDist * kAgentShadowDist ? 0 : 2;
                lodBucket[lod].push_back(inst);
            }
            for (int lod = 0; lod < 3; ++lod)
            {
                if (lodBucket[lod].empty()) continue;
                render::InstanceBatch batch{};
                batch.mesh = crowdMesh;
                batch.shader = crowdShader;
                batch.first = static_cast<std::uint32_t>(scene.meshInstances.size());
                batch.count = static_cast<std::uint32_t>(lodBucket[lod].size());
                batch.lod = static_cast<std::uint16_t>(lod);
                scene.meshInstances.insert(scene.meshInstances.end(),
                                           lodBucket[lod].begin(), lodBucket[lod].end());
                scene.instanceBatches.push_back(batch);
            }

            // Gib pieces (docs/defense-combat-design.md §3): a handful of live
            // pieces at once, so plain non-instanced MeshDraw entries (own
            // batch by construction) are simplest - no new InstanceBatch
            // bookkeeping for a count this small. Cube stand-in per GibConfig.h
            // until real limb/head meshes exist.
            for (const std::uint32_t idx : simulation.Gibs().ActiveIndices())
            {
                const GibPiece& g = simulation.Gibs().Slots()[idx];
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ 0.18f, 0.18f, 0.18f }) * math::Translation(g.pos);
                draw.color = { 0.42f, 0.12f, 0.10f, 1.0f };   // dark red - reads as gore at this size
                scene.meshDraws.push_back(draw);
            }

            // Placed mortars/mines (docs/defense-combat-design.md §4): same
            // "small count, plain MeshDraw" reasoning as the gibs above - no
            // aim-preview visual yet (§8/§10 step 4's "조준 프리뷰" is deferred,
            // same YAGNI call as skipping WeaponIntent for now).
            for (const std::uint32_t idx : simulation.Ordnance().ActiveIndices())
            {
                const PlacedOrdnance& o = simulation.Ordnance().Slots()[idx];
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ 0.3f, 0.3f, 0.3f }) * math::Translation(o.pos);
                draw.color = o.kind == OrdnanceKind::Mortar
                    ? math::Color{ 1.0f, 0.55f, 0.1f, 1.0f }    // orange - counting down
                    : math::Color{ 0.85f, 0.1f, 0.1f, 1.0f };   // red - armed, proximity trigger
                scene.meshDraws.push_back(draw);
            }

            // Barbed wire slow zones (docs/defense-combat-design.md §6): flat
            // static patches, no rotation/animation - one MeshDraw each, same
            // "small count" reasoning as gibs/ordnance above.
            for (const SlowZone& zone : simulation.SlowZones())
            {
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ zone.radius, 0.05f, zone.radius }) * math::Translation(zone.center);
                draw.color = { 0.35f, 0.30f, 0.20f, 1.0f };   // dull rusty brown
                scene.meshDraws.push_back(draw);
            }

            // Rifle tracers (docs/defense-combat-design.md §5 "시각 피드백"):
            // short-lived bright lines from the muzzle to wherever the shot
            // landed, so a shot reads as travelling instead of an instant
            // silent hit. Reuses DebugDrawPass's line rendering (already used
            // for the look-ray below) rather than a new render pass/shader -
            // the existing particle billboard stretch caps at 2.5x, far too
            // short to read as a tracer over tens of metres.
            for (const TracerLine& tracer : simulation.Tracers())
            {
                const float alpha = math::Clamp(tracer.ageLeft / tracer.life, 0.0f, 1.0f);
                render::debug::Line(scene.debugLines, tracer.start, tracer.end, { 1.0f, 0.95f, 0.55f, alpha });
            }

            // Debug draw: player AABB + a yellow line along the top of the ramp
            // the camera looks over.
            const math::Vec3 feet = simulation.CharacterPosition();
            render::debug::Box(scene.debugLines, { feet.x, feet.y + 0.9f, feet.z },
                               { 0.30f, 0.90f, 0.30f }, { 0.20f, 1.0f, 0.35f, 1.0f });
            render::debug::Line(scene.debugLines, { -fieldHalf * 0.5f, hillHeight + 0.02f, hillTopZ },
                                { fieldHalf * 0.5f, hillHeight + 0.02f, hillTopZ }, { 1.0f, 0.85f, 0.2f, 1.0f });

            // The player look-ray (CollisionWorld3D raycast): green to the hit
            // point + a marker there, or grey out to the full range on a miss.
            const math::Color rayColor = look.hit ? math::Color{ 0.3f, 1.0f, 0.4f, 1.0f }
                                                  : math::Color{ 0.5f, 0.5f, 0.55f, 1.0f };
            render::debug::Ray(scene.debugLines, look.origin, look.dir, look.length, rayColor);
            if (look.hit)
                render::debug::Sphere(scene.debugLines, look.point, 0.35f, { 1.0f, 0.4f, 0.2f, 1.0f });
        }

        // Demo scene 3: a deliberate arrangement for graphics work - nothing here
        // is gameplay, it exists to put shadows, cel shading, rim light and the
        // shadow cascade seam somewhere easy to look at (docs/shadows.md "씬 3").
        // No crowd, no wandering extras (Simulation::SpawnActors) - just the
        // player + static props, all existing mesh/shader resources (Cube/Plane,
        // the same materials as scene 1's kBoxes).
        void BuildShadowShowcaseScene(render::Scene3D& scene)
        {
            render::MeshDraw ground{};
            ground.mesh = render::MeshId::Plane;
            ground.world = math::Scaling({ 28.0f, 1.0f, 28.0f });
            ground.color = { 0.58f, 0.60f, 0.64f, 1.0f };   // bright neutral so the shadow reads clearly
            scene.meshDraws.push_back(ground);

            // A tall back wall to catch long, low-angle shadows on a large flat
            // vertical surface - good for spotting acne/peter-panning at a glance.
            render::MeshDraw wall{};
            wall.mesh = render::MeshId::Cube;
            wall.world = math::Scaling({ 24.0f, 4.0f, 0.4f }) * math::Translation({ 0.0f, 2.0f, -14.0f });
            wall.color = { 0.50f, 0.48f, 0.46f, 1.0f };
            scene.meshDraws.push_back(wall);

            // A rising staircase of 5 plinths - shows the shadow gradient
            // lengthen/soften across a slope of increasing height, right next to
            // the player for a close look at cel shading + rim light.
            for (int i = 0; i < 5; ++i)
            {
                const float h = 0.5f + static_cast<float>(i) * 0.5f;   // 0.5 .. 2.5
                render::MeshDraw step{};
                step.mesh = render::MeshId::Cube;
                step.world = math::Scaling({ 2.0f, h, 2.0f })
                           * math::Translation({ -8.0f + static_cast<float>(i) * 2.2f, h * 0.5f, -3.0f });
                step.color = { 0.62f + 0.02f * static_cast<float>(i), 0.58f, 0.52f, 1.0f };
                scene.meshDraws.push_back(step);
            }

            // Thin pillars at graduated distances (3/7/11/17/24m) straddling the
            // near/far shadow cascade boundary (cascade 0 is a 12m box around the
            // player, BuildLighting) - the seam between crisp-near and
            // coarser-far shadow quality should be visible somewhere in this run.
            constexpr float kPillarDistances[] = { 3.0f, 7.0f, 11.0f, 17.0f, 24.0f };
            for (const float d : kPillarDistances)
            {
                render::MeshDraw pillar{};
                pillar.mesh = render::MeshId::Cube;
                pillar.world = math::Scaling({ 0.6f, 3.0f, 0.6f }) * math::Translation({ 6.0f, 1.5f, d });
                pillar.color = { 0.60f, 0.55f, 0.50f, 1.0f };
                scene.meshDraws.push_back(pillar);
            }

            // A couple of small varied shapes right next to the player, same
            // spirit as kBoxes (scene 1) - close-range detail for the character's
            // own cast shadow and cel/rim shading.
            constexpr DemoBox kNearProps[] = {
                { { 1.2f, 1.2f, 1.2f }, { 3.0f, 0.6f, 1.5f }, { 0.72f, 0.56f, 0.50f, 1.0f } },
                { { 0.7f, 0.7f, 0.7f }, { -2.5f, 0.35f, 2.0f }, { 0.55f, 0.70f, 0.62f, 1.0f } },
            };
            for (const DemoBox& box : kNearProps)
            {
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling(box.scale) * math::Translation(box.pos);
                draw.color = box.color;
                scene.meshDraws.push_back(draw);
            }
        }

        // Demo scene EffectsTest: a flat, empty range with distance markers so
        // muzzle/explosion/gib VFX (docs/particle-system-research.md) can be
        // inspected on their own - no crowd, no weapons, no wave loop. First-
        // person (BuildCamera), player centred at the near end facing +Z.
        // Application binds 1/2/3 to Simulation::PreviewVfxEffect, which spawns
        // along whatever the player is currently looking at.
        void BuildEffectsTestScene(render::Scene3D& scene)
        {
            render::MeshDraw ground{};
            ground.mesh = render::MeshId::Plane;
            ground.world = math::Scaling({ 25.0f, 1.0f, 25.0f }) * math::Translation({ 0.0f, 0.0f, 20.0f });
            ground.color = { 0.40f, 0.42f, 0.38f, 1.0f };   // neutral, not too dark/bright for gauging VFX colour
            scene.meshDraws.push_back(ground);

            // A low marker post + a flat "target" plate at each distance so
            // scale/visibility can be judged at a glance without a HUD ruler.
            // kEffectsPreviewDistance (8m) sits between the first two.
            constexpr float kMarkerDistances[] = { 5.0f, 10.0f, 20.0f, 40.0f };
            for (const float d : kMarkerDistances)
            {
                render::MeshDraw post{};
                post.mesh = render::MeshId::Cube;
                post.world = math::Scaling({ 0.15f, 1.5f, 0.15f }) * math::Translation({ -3.0f, 0.75f, d });
                post.color = { 0.85f, 0.75f, 0.20f, 1.0f };   // high-contrast yellow, easy to spot at range
                scene.meshDraws.push_back(post);

                render::MeshDraw plate{};
                plate.mesh = render::MeshId::Plane;
                plate.world = math::Scaling({ 1.0f, 1.0f, 1.0f }) * math::Translation({ 0.0f, 0.02f, d });
                plate.color = { 0.70f, 0.68f, 0.60f, 1.0f };
                scene.meshDraws.push_back(plate);
            }
        }

        void BuildScene3D(render::Scene3D& scene, const Simulation& simulation)
        {
            // Actor 0 is the player - drawn as the skinned model (ModelMeshPass3D
            // skins one instance). The local-time-scale demo actors (1..) are
            // plain cubes so their differing speeds are obvious without touching
            // the skinning path. DefenseCombat/EffectsTest are first-person
            // (BuildCamera above) - no one draws their own body in first
            // person, and it was occluding the muzzle flash spawned right at
            // the eye point anyway.
            if (simulation.ActiveScene() != DemoScene::DefenseCombat && simulation.ActiveScene() != DemoScene::EffectsTest)
            {
                render::ModelDraw model{};
                model.world = math::RotationY(simulation.CharacterFacingYaw() + kModelYawOffset)
                            * math::Translation(simulation.CharacterPosition());
                const AnimPose pose = simulation.HeroAnimPose();
                model.animClipIndex     = pose.clipIndex;
                model.animClipTime      = pose.clipTime;
                model.animPlayMode      = pose.playMode;
                model.animParametric    = pose.parametric;
                model.animFromClipIndex = pose.fromClipIndex;
                model.animFromClipTime  = pose.fromClipTime;
                model.animFromPlayMode  = pose.fromPlayMode;
                model.animBlend         = pose.blend;
                scene.modelDraws.push_back(model);
            }

            const std::vector<Actor>& actors = simulation.Actors();
            for (std::size_t i = 1; i < actors.size(); ++i)
            {
                const Actor& a = actors[i];
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ 0.4f, 0.4f, 0.4f })
                           * math::Translation(a.pos + math::Vec3{ 0.0f, 0.4f, 0.0f });
                // Warm = faster than real time, cool = slower.
                draw.color = a.timeScale >= 1.0f
                    ? math::Color{ 0.95f, 0.55f, 0.30f, 1.0f }
                    : math::Color{ 0.35f, 0.60f, 0.95f, 1.0f };
                scene.meshDraws.push_back(draw);
            }

            if (simulation.ActiveScene() == DemoScene::DefenseCombat)
            {
                BuildCliffScene(scene, simulation);
            }
            else if (simulation.ActiveScene() == DemoScene::ShadowShowcase)
            {
                BuildShadowShowcaseScene(scene);

                const math::Vec3 feet = simulation.CharacterPosition();
                render::debug::Box(scene.debugLines, { feet.x, feet.y + 0.9f, feet.z },
                                   { 0.30f, 0.90f, 0.30f }, { 0.20f, 1.0f, 0.35f, 1.0f });
            }
            else if (simulation.ActiveScene() == DemoScene::EffectsTest)
            {
                BuildEffectsTestScene(scene);
            }
            else
            {
                render::MeshDraw ground{};
                ground.mesh = render::MeshId::Plane;
                ground.world = math::Scaling({ 16.0f, 1.0f, 16.0f });
                ground.color = { 0.50f, 0.52f, 0.57f, 1.0f };
                scene.meshDraws.push_back(ground);

                for (const DemoBox& box : kBoxes)
                {
                    render::MeshDraw draw{};
                    draw.mesh = render::MeshId::Cube;
                    draw.world = math::Scaling(box.scale) * math::Translation(box.pos);
                    draw.color = box.color;
                    scene.meshDraws.push_back(draw);
                }

                // Temporary: exercises DebugDrawPass. The character's AABB, its
                // forward vector, and the downward "ground check" ray a raycast
                // would use. Replaced when gameplay drives debug draw (roadmap D1).
                const math::Vec3 feet = simulation.CharacterPosition();
                const math::Vec3 boxCenter{ feet.x, feet.y + 0.9f, feet.z };
                render::debug::Box(scene.debugLines, boxCenter, { 0.30f, 0.90f, 0.30f },
                                   { 0.20f, 1.0f, 0.35f, 1.0f });
                const float yaw = simulation.CharacterFacingYaw();
                const math::Vec3 fwd{ std::sin(yaw), 0.0f, std::cos(yaw) };
                render::debug::Ray(scene.debugLines, boxCenter, fwd, 1.2f, { 1.0f, 0.85f, 0.2f, 1.0f });
                render::debug::Ray(scene.debugLines, { feet.x, feet.y + 0.2f, feet.z }, { 0.0f, -1.0f, 0.0f },
                                   0.6f, { 0.4f, 0.7f, 1.0f, 1.0f });
            }
        }
#endif

        // The old 2D overlay demo (player + obstacles + particles) is disabled;
        // set true to bring it back.
        constexpr bool kDrawLegacy2D = false;

        // Temporary: exercises the SpritePass2D path end to end (white-sprite
        // solid rect, two atlas sprites, one scissor-clipped). Replaced by real
        // widget output once ui::DrawList lands.
        void BuildDemoUiSprites(std::vector<render::SpriteDraw>& out, const render::AtlasIndex* atlas)
        {
            // Solid panel via the built-in white texture (atlasId 0).
            render::SpriteDraw panel{};
            panel.x = 24.0f; panel.y = 300.0f; panel.width = 320.0f; panel.height = 176.0f;
            panel.r = 0.10f; panel.g = 0.13f; panel.b = 0.20f; panel.a = 0.92f;
            out.push_back(panel);

            if (atlas == nullptr || !atlas->Loaded()) return;

            auto place = [&](const char* name, float x, float y, float size, math::Rect clip)
            {
                const render::SpriteRect* r = atlas->Find(name);
                if (r == nullptr) return;
                render::SpriteDraw s{};
                s.x = x; s.y = y; s.width = size; s.height = size;
                s.u0 = r->u0; s.v0 = r->v0; s.u1 = r->u1; s.v1 = r->v1;
                s.atlasId = atlas->AtlasId();
                s.clip = clip;
                out.push_back(s);
            };

            // Sprite names come from tools/atlas_pack (file stems under
            // assets/src/ui/); see assets/atlas/ui.atlas.
            place("icon_play", 44.0f, 320.0f, 96.0f, {});                    // unclipped
            place("icon_settings", 160.0f, 320.0f, 96.0f, {});              // unclipped
            // Same sprite again, scissored to a rect that cuts it in half -
            // proves RSSetScissorRects.
            place("icon_play", 44.0f, 430.0f, 96.0f, { 24.0f, 430.0f, 320.0f, 40.0f });
        }

        // Converts simulation.VfxParticles() (muzzle/explosion/gib blood spray,
        // docs/particle-system-research.md) into the render-side instance/batch
        // arrays. Age->size/colour lerp happens here (main thread), not in
        // ParticleSystem::Step, so the sim stays pure physics. Two batches: all
        // Additive particles first (order doesn't matter, the blend equation is
        // commutative), then AlphaBlend sorted back-to-front by camera distance
        // (translucent overlap must draw far-to-near, §4.3).
        void BuildVfxParticles(render::Scene3D& scene, const Simulation& simulation)
        {
            const core::ObjectPool<vfx::Particle>& pool = simulation.VfxParticles();
            const std::vector<std::uint32_t>& active = pool.ActiveIndices();
            if (active.empty()) return;

            const vfx::Particle* slots = pool.Slots();
            const math::Vec3 eye = EyeFromView(scene.camera.view);

            std::vector<std::uint32_t> additiveIdx;
            struct Sortable { std::uint32_t idx; float distSq; };
            std::vector<Sortable> alphaIdx;
            additiveIdx.reserve(active.size());
            alphaIdx.reserve(active.size());

            for (const std::uint32_t idx : active)
            {
                const vfx::Particle& p = slots[idx];
                if (p.blend == vfx::ParticleBlend::Additive)
                {
                    additiveIdx.push_back(idx);
                }
                else
                {
                    const math::Vec3 d = p.pos - eye;
                    alphaIdx.push_back({ idx, d.x * d.x + d.y * d.y + d.z * d.z });
                }
            }
            std::sort(alphaIdx.begin(), alphaIdx.end(),
                      [](const Sortable& a, const Sortable& b) { return a.distSq > b.distSq; });

            const auto unpack = [](std::uint32_t c, float out[4]) {
                out[0] = static_cast<float>((c >> 24) & 0xffu) / 255.0f;
                out[1] = static_cast<float>((c >> 16) & 0xffu) / 255.0f;
                out[2] = static_cast<float>((c >> 8) & 0xffu) / 255.0f;
                out[3] = static_cast<float>(c & 0xffu) / 255.0f;
            };

            // Camera-space axes - COLUMNS of view, not rows (math::LookAtLH
            // packs xAxis/yAxis/zAxis one component per row: m[0..2] is
            // (xAxis.x, yAxis.x, zAxis.x), not xAxis itself - column 0
            // (m[0], m[4], m[8]) is. Used below to decide whether a fast
            // particle's screen-space travel direction should override its
            // simulated spin (see kStretchMinSpeed comment).
            const math::Vec3 camRight{ scene.camera.view.m[0], scene.camera.view.m[4], scene.camera.view.m[8] };
            const math::Vec3 camUp{ scene.camera.view.m[1], scene.camera.view.m[5], scene.camera.view.m[9] };
            // Below this speed a particle's motion barely reads on screen -
            // use its simulated tumble (Particle::rotation) instead of trying
            // to align a near-zero direction. Above it, orient + stretch along
            // the direction of travel so fast debris reads as streaking
            // chunks, not sliding stickers (§12 구현 노트 "3D로 보이기").
            constexpr float kStretchMinSpeed = 1.5f;
            constexpr float kStretchSpeedRef = 8.0f;
            constexpr float kMaxStretch = 2.5f;

            const auto append = [&](const vfx::Particle& p) {
                const float t = p.life > 0.0f ? math::Clamp(p.age / p.life, 0.0f, 1.0f) : 1.0f;
                float c0[4], c1[4];
                unpack(p.colorStart, c0);
                unpack(p.colorEnd, c1);

                const float speed = math::Length(p.vel);
                float rotation = p.rotation;
                float stretch = 1.0f;
                // Smoke is a big soft cloud, not directional debris - stretching
                // it into an elongated plank made the "this is a flat card" read
                // worse, not better (playtest: looked wrong/plane-like from the
                // side as the camera moved). Only small fast chunks (embers,
                // fireball bits, blood) stretch; smoke just tumbles.
                if (speed > kStretchMinSpeed && p.kind != vfx::ParticleKind::ExplosionSmokeStem)
                {
                    const float sx = math::Dot(p.vel, camRight);
                    const float sy = math::Dot(p.vel, camUp);
                    rotation = std::atan2(sy, sx);
                    stretch = 1.0f + std::min(1.0f, (speed - kStretchMinSpeed) / kStretchSpeedRef) * (kMaxStretch - 1.0f);
                }

                render::ParticleInstance inst{};
                inst.pos = p.pos;
                inst.size = p.sizeStart + (p.sizeEnd - p.sizeStart) * t;
                inst.rotation = rotation;
                inst.stretch = stretch;
                inst.colorRgba = PackRgba(c0[0] + (c1[0] - c0[0]) * t, c0[1] + (c1[1] - c0[1]) * t,
                                          c0[2] + (c1[2] - c0[2]) * t, c0[3] + (c1[3] - c0[3]) * t);
                scene.particleInstances.push_back(inst);
            };

            for (const std::uint32_t idx : additiveIdx) append(slots[idx]);
            const auto additiveCount = static_cast<std::uint32_t>(scene.particleInstances.size());
            if (additiveCount > 0)
                scene.particleBatches.push_back({ render::ParticleBlend::Additive, 0, additiveCount });

            for (const Sortable& s : alphaIdx) append(slots[s.idx]);
            const auto alphaCount = static_cast<std::uint32_t>(scene.particleInstances.size()) - additiveCount;
            if (alphaCount > 0)
                scene.particleBatches.push_back({ render::ParticleBlend::AlphaBlend, additiveCount, alphaCount });
        }

        // Explosion "3D core" cubes (Simulation::FireChunk) - real opaque
        // MeshDraws, not billboards, so they read as genuine volume from any
        // angle (docs/particle-system-research.md §12 구현 노트 "3D로 보이기").
        // Tumbles on two axes (spin drives both RotationX/Y) and shrinks to
        // nothing near the end of its life (MeshPass3D is opaque, no alpha to
        // fade) while cooling from bright yellow to dark red.
        void BuildFireChunks(render::Scene3D& scene, const Simulation& simulation)
        {
            const core::ObjectPool<FireChunk>& pool = simulation.FireChunks();
            const std::vector<std::uint32_t>& active = pool.ActiveIndices();
            if (active.empty()) return;

            const FireChunk* slots = pool.Slots();
            for (const std::uint32_t idx : active)
            {
                const FireChunk& c = slots[idx];
                const float t = c.maxLife > 0.0f ? math::Clamp(c.life / c.maxLife, 0.0f, 1.0f) : 0.0f;
                const float scale = c.scale * (0.3f + 0.7f * t);   // shrink toward the end, never fully to 0 (avoids a degenerate world matrix)

                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ scale, scale, scale })
                           * math::RotationX(c.spin * 0.7f) * math::RotationY(c.spin)
                           * math::Translation(c.pos);
                // Cooling gradient: bright yellow-white when fresh -> dark red as it dies.
                draw.color = { 1.0f, 0.35f + t * 0.55f, 0.05f + t * 0.35f, 1.0f };
                scene.meshDraws.push_back(draw);
            }
        }
    }

    render::RenderSnapshot SnapshotBuilder::Build(std::uint64_t frameNumber,
                                                 const Simulation& simulation,
                                                 const ui::UIContext& ui,
                                                 int viewportWidth,
                                                 int viewportHeight,
                                                 const render::AtlasIndex* uiAtlas,
                                                 float fps) const
    {
        render::RenderSnapshot snapshot{};
        snapshot.frameNumber = frameNumber;

#if defined(ENGINE_WITH_3D)
        snapshot.scene3d.camera = BuildCamera(simulation, viewportWidth, viewportHeight);
        snapshot.scene3d.lighting = BuildLighting(simulation, simulation.ElapsedTime());
        snapshot.scene3d.postProcess = BuildPostProcess();
        BuildScene3D(snapshot.scene3d, simulation);
        BuildVfxParticles(snapshot.scene3d, simulation);
        BuildFireChunks(snapshot.scene3d, simulation);
#else
        (void)viewportWidth;
        (void)viewportHeight;
        if constexpr (!kDrawLegacy2D) (void)simulation;
#endif

        // 2D test overlay (player + obstacles + particle sample) - disabled.
        if constexpr (kDrawLegacy2D)
        {
            for (const math::Rect& rect : simulation.Obstacles())
                snapshot.worldQuads.push_back({ rect.x, rect.y, rect.width, rect.height,
                                               0.30f, 0.32f, 0.38f, 0.85f });

            const std::vector<Particle>& particles = simulation.Particles();
            const std::size_t visible = std::min(particles.size(), kVisibleParticleSample);
            for (std::size_t index = 0; index < visible; ++index)
            {
                const Particle& particle = particles[index];
                snapshot.worldQuads.push_back({ particle.x, particle.y, 3.0f, 3.0f,
                                               0.45f, 0.55f, 0.70f, 0.55f });
            }

            const math::Vec2 player = simulation.PlayerPosition();
            const math::Color c = simulation.PlayerBlocked()
                ? math::Color{ 1.0f, 0.45f, 0.30f, 1.0f }
                : math::Color{ 0.20f, 0.75f, 1.0f, 1.0f };
            snapshot.worldQuads.push_back({ player.x, player.y,
                                           Simulation::kPlayerSize, Simulation::kPlayerSize,
                                           c.r, c.g, c.b, c.a });
        }

        ui.Build(snapshot.uiQuads, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
        BuildDemoUiSprites(snapshot.uiSprites, uiAtlas);

#if defined(ENGINE_WITH_3D)
        // Wave-loop readout (docs/defense-combat-design.md §0/§0.1): a plain
        // text line, same DrawText/DrawRect primitives as the FPS counter
        // below - no GameState::WaveResults screen yet, just a live glance at
        // wave/phase/objective HP/supplies while playing. Scene 2 only.
        if (simulation.ActiveScene() == DemoScene::DefenseCombat)
        {
            char text[96];
            const char* phaseName = simulation.Phase() == MatchPhase::Combat ? "COMBAT" : "PREP";
            std::snprintf(text, sizeof(text), "WAVE %d  %s %.0fs  OBJ HP %.0f  SUPPLIES %d  KILLS %d",
                          simulation.WaveNumber(), phaseName, simulation.PhaseTimeLeft(),
                          simulation.ObjectiveHealth(), simulation.Supplies(), simulation.KillCount());
            constexpr float scale = 2.0f;
            const float glyph = 6.0f * scale;
            const float width = static_cast<float>(std::strlen(text)) * glyph;
            const float x = (static_cast<float>(viewportWidth) - width) * 0.5f;
            const float y = 12.0f;
            ui::DrawRect(snapshot.uiQuads, { x - 8.0f, y - 4.0f, width + 16.0f, 7.0f * scale + 8.0f },
                         { 0.0f, 0.0f, 0.0f, 0.45f });
            ui::DrawText(snapshot.uiQuads, text, { x, y }, scale, { 1.0f, 0.95f, 0.35f, 1.0f });

            // TEMP debug text (user-requested playtesting aid): current weapon
            // + a numeric readout of this frame's look-ray, to go with the
            // existing 3D debug ray/hit-sphere drawn above in BuildCliffScene.
            // Remove once real weapon-select UI / crosshair exist.
            char aimText[128];
            const LookRayResult& look = simulation.LookRay();
            if (look.hit)
            {
                std::snprintf(aimText, sizeof(aimText),
                              "WEAPON: RIFLE (AUTO, LMB)   AIM: HIT slot=%u (%.1f, %.1f, %.1f) dist=%.1fm",
                              look.agentSlot, look.point.x, look.point.y, look.point.z, look.length);
            }
            else
            {
                std::snprintf(aimText, sizeof(aimText),
                              "WEAPON: RIFLE (AUTO, LMB)   AIM: NO HIT (range %.0fm)", look.length);
            }
            constexpr float aimScale = 1.5f;
            const float aimGlyph = 6.0f * aimScale;
            const float aimWidth = static_cast<float>(std::strlen(aimText)) * aimGlyph;
            const float aimX = (static_cast<float>(viewportWidth) - aimWidth) * 0.5f;
            const float aimY = y + 7.0f * scale + 14.0f;
            ui::DrawRect(snapshot.uiQuads, { aimX - 8.0f, aimY - 4.0f, aimWidth + 16.0f, 7.0f * aimScale + 8.0f },
                         { 0.0f, 0.0f, 0.0f, 0.45f });
            ui::DrawText(snapshot.uiQuads, aimText, { aimX, aimY }, aimScale,
                         look.hit ? math::Color{ 0.4f, 1.0f, 0.5f, 1.0f } : math::Color{ 0.7f, 0.7f, 0.75f, 1.0f });

            // Weapon key-mapping legend, upper-right (user-requested): a static
            // reference list so the 5 weapons' keys (defense-combat-design.md
            // §0.1/§4/§5/§6/§7) don't have to be memorised. Not tied to look-ray
            // like the aim readout above.
            {
                static const char* const kWeaponLines[] = {
                    "WEAPONS",
                    "LMB   RIFLE",
                    "RMB   FLAMETHROWER",
                    "1     MORTAR",
                    "2     MINE",
                    "3     BARBED WIRE",
                };
                constexpr std::size_t kWeaponLineCount = sizeof(kWeaponLines) / sizeof(kWeaponLines[0]);
                constexpr float wScale = 1.5f;
                const float wGlyph = 6.0f * wScale;
                const float lineHeight = 7.0f * wScale + 4.0f;
                float maxWidth = 0.0f;
                for (const char* line : kWeaponLines)
                    maxWidth = std::max(maxWidth, static_cast<float>(std::strlen(line)) * wGlyph);
                const float wx = static_cast<float>(viewportWidth) - maxWidth - 16.0f;
                const float wy = 60.0f;
                const float boxHeight = lineHeight * static_cast<float>(kWeaponLineCount) + 8.0f;
                ui::DrawRect(snapshot.uiQuads, { wx - 8.0f, wy - 4.0f, maxWidth + 16.0f, boxHeight },
                             { 0.0f, 0.0f, 0.0f, 0.45f });
                for (std::size_t i = 0; i < kWeaponLineCount; ++i)
                {
                    const math::Color color = (i == 0)
                        ? math::Color{ 1.0f, 0.95f, 0.35f, 1.0f }
                        : math::Color{ 0.85f, 0.9f, 0.95f, 1.0f };
                    ui::DrawText(snapshot.uiQuads, kWeaponLines[i],
                                 { wx, wy + lineHeight * static_cast<float>(i) }, wScale, color);
                }
            }
        }
        else if (simulation.ActiveScene() == DemoScene::EffectsTest)
        {
            // Key hint for the whole point of this scene - on-demand VFX
            // preview (Simulation::PreviewVfxEffect, Application's 1/2/3 keys).
            const char* text = "1: MUZZLE FLASH   2: EXPLOSION   3: GIB BLOOD SPRAY";
            constexpr float scale = 1.5f;
            const float glyph = 6.0f * scale;
            const float width = static_cast<float>(std::strlen(text)) * glyph;
            const float x = (static_cast<float>(viewportWidth) - width) * 0.5f;
            const float y = 12.0f;
            ui::DrawRect(snapshot.uiQuads, { x - 8.0f, y - 4.0f, width + 16.0f, 7.0f * scale + 8.0f },
                         { 0.0f, 0.0f, 0.0f, 0.45f });
            ui::DrawText(snapshot.uiQuads, text, { x, y }, scale, { 0.6f, 0.9f, 1.0f, 1.0f });
        }
#endif

        // Frame-rate readout, top-right, over every screen.
        if (fps > 0.0f)
        {
            char text[24];
            std::snprintf(text, sizeof(text), "%d FPS", static_cast<int>(fps + 0.5f));
            constexpr float scale = 2.0f;
            const float glyph = 6.0f * scale;                       // ui::DrawText advance
            const float width = static_cast<float>(std::strlen(text)) * glyph;
            const float x = static_cast<float>(viewportWidth) - width - 12.0f;
            const float y = 12.0f;
            ui::DrawRect(snapshot.uiQuads, { x - 6.0f, y - 4.0f, width + 8.0f, 7.0f * scale + 8.0f },
                         { 0.0f, 0.0f, 0.0f, 0.45f });
            ui::DrawText(snapshot.uiQuads, text, { x, y }, scale, { 1.0f, 0.95f, 0.35f, 1.0f });
        }

        return snapshot;
    }
}
