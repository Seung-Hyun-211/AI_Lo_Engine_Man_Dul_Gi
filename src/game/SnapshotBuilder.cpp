#include "game/SnapshotBuilder.h"

#include "math/Math.h"

#include <algorithm>
#include <cmath>

#if defined(ENGINE_WITH_3D)
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

        // Third-person orbit camera: sits behind + above the character at the
        // yaw/pitch the mouse drives (Simulation::UpdateCameraLook) and always
        // looks at a point near the character's chest.
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

            // Scene 2 (clifftop overlook) pulls the camera back so the field and
            // the crowd below are in frame, not just the player's back.
            const float orbitDistance = Simulation::kDemoScene == 2 ? 6.0f : 3.6f;
            const math::Vec3 focus = simulation.CharacterPosition() + math::Vec3{ 0.0f, 1.3f, 0.0f };
            const math::Vec3 eye = focus - forward * orbitDistance;

            render::CameraView camera{};
            camera.view = math::LookAtLH(eye, focus, { 0.0f, 1.0f, 0.0f });
            camera.projection = math::PerspectiveFovLH(kPi / 3.0f, aspect, 0.05f, 100.0f);
            return camera;
        }

        render::Lighting BuildLighting(float elapsed)
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

            // Directional shadow map: an ortho frustum fitted around the scene.
            // Scene 2 spreads the crowd across a wide field, so widen the frustum
            // and push its centre out toward it.
            const bool scene2 = Simulation::kDemoScene == 2;
            const math::Vec3 dir = math::Normalized(lighting.key.direction);
            const math::Vec3 center = scene2 ? math::Vec3{ 0.0f, 1.0f, 18.0f } : math::Vec3{ 0.0f, 1.0f, 0.0f };
            const float span = scene2 ? 64.0f : 22.0f;
            const float depth = scene2 ? 90.0f : 40.0f;
            const math::Vec3 eye = center - dir * (scene2 ? 32.0f : 16.0f);
            const math::Mat4 view = math::LookAtLH(eye, center, { 0.0f, 1.0f, 0.0f });
            const math::Mat4 proj = math::OrthographicLH(span, span, 0.1f, depth);
            lighting.lightViewProj = view * proj;
            lighting.shadowsEnabled = true;
            return lighting;
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

        // Demo scene 2: a large field with the player standing on a mesa that
        // drops away in front of them, and a wandering simulation crowd on the
        // field below. Props only - the player model + crowd stepping live in
        // Simulation. See docs/demo-scene.md.
        void BuildCliffScene(render::Scene3D& scene, const Simulation& simulation)
        {
            const float fieldHalf = Simulation::kFieldHalf;
            const float cliffTop = Simulation::kCliffTop;

            // Wide lower field.
            render::MeshDraw field{};
            field.mesh = render::MeshId::Plane;
            field.world = math::Scaling({ fieldHalf * 2.8f, 1.0f, fieldHalf * 2.8f })
                        * math::Translation({ 0.0f, 0.0f, fieldHalf * 0.6f });
            field.color = { 0.33f, 0.40f, 0.30f, 1.0f };
            scene.meshDraws.push_back(field);

            // The mesa the player stands on: a block rising from the field, its
            // flat top at y = cliffTop and its +Z face the "cliff" the view
            // looks down. The player's x/z clamp is symmetric about the origin,
            // so the mesa is centred there and overhangs it by a few metres.
            const float mesaFrontZ = Simulation::kPlateauHalf + 4.0f;
            const float mesaDepth = mesaFrontZ + 20.0f;   // extends far back under the camera
            render::MeshDraw mesa{};
            mesa.mesh = render::MeshId::Cube;
            mesa.world = math::Scaling({ Simulation::kPlateauHalf * 2.0f + 8.0f, cliffTop, mesaDepth })
                       * math::Translation({ 0.0f, cliffTop * 0.5f, mesaFrontZ - mesaDepth * 0.5f });
            mesa.color = { 0.42f, 0.38f, 0.34f, 1.0f };
            scene.meshDraws.push_back(mesa);

            // A couple of markers on the field for depth / scale reference.
            const math::Vec3 pillars[] = {
                { -16.0f, 1.5f, 22.0f }, { 18.0f, 1.5f, 30.0f }, { 4.0f, 1.5f, 40.0f },
            };
            for (const math::Vec3& p : pillars)
            {
                render::MeshDraw draw{};
                draw.mesh = render::MeshId::Cube;
                draw.world = math::Scaling({ 1.2f, 3.0f, 1.2f }) * math::Translation(p);
                draw.color = { 0.55f, 0.52f, 0.48f, 1.0f };
                scene.meshDraws.push_back(draw);
            }

            // The simulation crowd: instanced cubes, one DrawIndexedInstanced per
            // LOD batch (docs/instanced-rendering.md §5). Frustum + distance
            // culled here so off-screen agents never reach the GPU. Distance
            // LOD, 2 tiers for now: bucket 0 = near (casts shadow), bucket 2 =
            // far (drawn, but MeshPass3D skips it in the shadow pass). Bucket 1
            // (a reduced mid representation / billboard) is reserved.
            constexpr float kAgentScale = 0.5f;
            constexpr float kAgentCullRadius = 0.5f;    // bounding sphere for the frustum test
            constexpr float kAgentCullDist = 90.0f;     // past this, skip entirely
            constexpr float kAgentShadowDist = 34.0f;   // past this, LOD 2: no shadow cast
            const math::Mat4 viewProj = scene.camera.view * scene.camera.projection;
            const Frustum frustum = MakeFrustum(viewProj);
            const math::Vec3 eye = EyeFromView(scene.camera.view);

            const core::ObjectPool<SimAgent>& pool = simulation.SimAgents();
            const SimAgent* agentSlots = pool.Slots();
            const LookRayResult& look = simulation.LookRay();

            std::vector<render::MeshInstance> lodBucket[3];
            for (const std::uint32_t slotIdx : pool.ActiveIndices())
            {
                const SimAgent& a = agentSlots[slotIdx];
                const math::Vec3 center = a.pos + math::Vec3{ 0.0f, kAgentScale * 0.5f, 0.0f };
                if (!SphereInFrustum(frustum, center, kAgentCullRadius)) continue;
                const math::Vec3 d = center - eye;
                const float d2 = math::Dot(d, d);
                if (d2 > kAgentCullDist * kAgentCullDist) continue;

                render::MeshInstance inst{};
                inst.pos = center;
                inst.yaw = a.heading;
                inst.scale = kAgentScale;
                if (look.hit && look.agentSlot == slotIdx)
                {
                    inst.colorRgba = PackRgba(1.0f, 0.9f, 0.2f, 1.0f);   // look-ray target
                }
                else
                {
                    const float hot = math::Clamp((a.speed - 0.8f) / 1.4f, 0.0f, 1.0f);
                    inst.colorRgba = PackRgba(0.25f + 0.65f * hot, 0.62f - 0.22f * hot,
                                              0.70f - 0.45f * hot, 1.0f);
                }

                const int lod = d2 <= kAgentShadowDist * kAgentShadowDist ? 0 : 2;
                lodBucket[lod].push_back(inst);
            }
            for (int lod = 0; lod < 3; ++lod)
            {
                if (lodBucket[lod].empty()) continue;
                render::InstanceBatch batch{};
                batch.mesh = render::MeshId::Cube;
                batch.first = static_cast<std::uint32_t>(scene.meshInstances.size());
                batch.count = static_cast<std::uint32_t>(lodBucket[lod].size());
                batch.lod = static_cast<std::uint16_t>(lod);
                scene.meshInstances.insert(scene.meshInstances.end(),
                                           lodBucket[lod].begin(), lodBucket[lod].end());
                scene.instanceBatches.push_back(batch);
            }

            // Debug draw: player AABB + a yellow line along the cliff edge the
            // camera looks over.
            const math::Vec3 feet = simulation.CharacterPosition();
            render::debug::Box(scene.debugLines, { feet.x, feet.y + 0.9f, feet.z },
                               { 0.30f, 0.90f, 0.30f }, { 0.20f, 1.0f, 0.35f, 1.0f });
            render::debug::Line(scene.debugLines, { -fieldHalf * 0.5f, cliffTop + 0.02f, mesaFrontZ },
                                { fieldHalf * 0.5f, cliffTop + 0.02f, mesaFrontZ }, { 1.0f, 0.85f, 0.2f, 1.0f });

            // The player look-ray (CollisionWorld3D raycast): green to the hit
            // point + a marker there, or grey out to the full range on a miss.
            const math::Color rayColor = look.hit ? math::Color{ 0.3f, 1.0f, 0.4f, 1.0f }
                                                  : math::Color{ 0.5f, 0.5f, 0.55f, 1.0f };
            render::debug::Ray(scene.debugLines, look.origin, look.dir, look.length, rayColor);
            if (look.hit)
                render::debug::Sphere(scene.debugLines, look.point, 0.35f, { 1.0f, 0.4f, 0.2f, 1.0f });
        }

        void BuildScene3D(render::Scene3D& scene, const Simulation& simulation)
        {
            // Actor 0 is the player - drawn as the skinned model (ModelMeshPass3D
            // skins one instance). The local-time-scale demo actors (1..) are
            // plain cubes so their differing speeds are obvious without touching
            // the skinning path.
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

            if constexpr (Simulation::kDemoScene == 2)
            {
                BuildCliffScene(scene, simulation);
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
    }

    render::RenderSnapshot SnapshotBuilder::Build(std::uint64_t frameNumber,
                                                 const Simulation& simulation,
                                                 const ui::UIContext& ui,
                                                 int viewportWidth,
                                                 int viewportHeight,
                                                 const render::AtlasIndex* uiAtlas) const
    {
        render::RenderSnapshot snapshot{};
        snapshot.frameNumber = frameNumber;

#if defined(ENGINE_WITH_3D)
        snapshot.scene3d.camera = BuildCamera(simulation, viewportWidth, viewportHeight);
        snapshot.scene3d.lighting = BuildLighting(simulation.ElapsedTime());
        BuildScene3D(snapshot.scene3d, simulation);
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
        return snapshot;
    }
}
