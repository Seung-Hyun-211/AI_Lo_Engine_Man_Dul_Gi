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

        render::CameraView BuildCamera(float elapsed, int viewportWidth, int viewportHeight)
        {
            const float aspect = viewportHeight > 0
                ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
                : 1.0f;

            const float angle = elapsed * 0.4f;   // slow orbit, framed on the model at the origin
            const math::Vec3 eye{ std::sin(angle) * 3.6f, 1.7f, -std::cos(angle) * 3.6f };
            const math::Vec3 target{ 0.0f, 0.9f, 0.0f };

            render::CameraView camera{};
            camera.view = math::LookAtLH(eye, target, { 0.0f, 1.0f, 0.0f });
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
            render::ModelDraw model{};
            model.world = math::RotationY(simulation.ElapsedTime() * 0.3f);
            scene.modelDraws.push_back(model);

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
    }

    render::RenderSnapshot SnapshotBuilder::Build(std::uint64_t frameNumber,
                                                 const Simulation& simulation,
                                                 const ui::UIContext& ui,
                                                 int viewportWidth,
                                                 int viewportHeight) const
    {
        render::RenderSnapshot snapshot{};
        snapshot.frameNumber = frameNumber;

#if defined(ENGINE_WITH_3D)
        snapshot.scene3d.camera = BuildCamera(simulation.ElapsedTime(), viewportWidth, viewportHeight);
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

        ui.Build(snapshot.uiQuads);
        return snapshot;
    }
}
