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

            const math::Vec3 focus = simulation.CharacterPosition() + math::Vec3{ 0.0f, 1.3f, 0.0f };
            const math::Vec3 eye = focus - forward * 3.6f;

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
            const math::Vec3 dir = math::Normalized(lighting.key.direction);
            const math::Vec3 center{ 0.0f, 1.0f, 0.0f };
            const math::Vec3 eye = center - dir * 16.0f;
            const math::Mat4 view = math::LookAtLH(eye, center, { 0.0f, 1.0f, 0.0f });
            const math::Mat4 proj = math::OrthographicLH(22.0f, 22.0f, 0.1f, 40.0f);
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

        void BuildScene3D(render::Scene3D& scene, const Simulation& simulation)
        {
            // Actor 0 is the player - drawn as the skinned model (ModelMeshPass3D
            // skins one instance). The local-time-scale demo actors (1..) are
            // plain cubes so their differing speeds are obvious without touching
            // the skinning path.
            render::ModelDraw model{};
            model.world = math::RotationY(simulation.CharacterFacingYaw() + kModelYawOffset)
                        * math::Translation(simulation.CharacterPosition());
            model.animClipIndex = simulation.HeroAnimClipIndex();
            model.animClipTime = simulation.HeroAnimClipTime();
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

            place("icon_a", 44.0f, 320.0f, 96.0f, {});                       // unclipped
            place("icon_b", 160.0f, 320.0f, 96.0f, {});                      // unclipped
            // Same sprite again, scissored to a rect that cuts it in half -
            // proves RSSetScissorRects.
            place("icon_a", 44.0f, 430.0f, 96.0f, { 24.0f, 430.0f, 320.0f, 40.0f });
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
