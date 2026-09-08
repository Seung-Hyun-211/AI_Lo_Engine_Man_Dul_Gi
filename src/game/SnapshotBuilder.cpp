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
            // the shading (and terminator) is visibly in motion.
            const float sweep = std::sin(elapsed * 0.5f) * 0.6f;   // ~+-34 deg around Y
            const float c = std::cos(sweep), s = std::sin(sweep);
            lighting.key.direction = { 0.70711f * s, -0.70711f, 0.70711f * c };
            lighting.key.color = { 1.0f, 0.97f, 0.90f, 1.35f };    // rgb, a = intensity (brighter)
            lighting.ambient.sky = { 0.36f, 0.40f, 0.48f, 1.0f };
            lighting.ambient.ground = { 0.22f, 0.20f, 0.18f, 1.0f };
            return lighting;
        }

        // The cube/collision demo lives off to the side so the FBX model has the
        // centre of the frame.
        constexpr math::Vec3 kDemoOffset{ 2.5f, 0.0f, 1.5f };

        void BuildScene3D(render::Scene3D& scene, const Simulation& simulation)
        {
            // FBX model (loaded by ModelMeshPass3D), slowly turning at the origin.
            render::ModelDraw model{};
            model.world = math::RotationY(simulation.ElapsedTime() * 0.3f);
            scene.modelDraws.push_back(model);

            render::MeshDraw ground{};
            ground.mesh = render::MeshId::Plane;
            ground.world = math::Scaling({ 14.0f, 1.0f, 14.0f });
            ground.color = { 0.46f, 0.48f, 0.53f, 1.0f };
            scene.meshDraws.push_back(ground);

            const float spin = simulation.HeroSpin();
            render::MeshDraw hero{};
            hero.mesh = render::MeshId::Cube;
            hero.world = math::RotationX(spin * 0.6f) * math::RotationY(spin)
                       * math::Translation(simulation.HeroCenter() + kDemoOffset);
            hero.color = { 0.95f, 0.55f, 0.25f, 1.0f };
            scene.meshDraws.push_back(hero);

            const auto& centers = simulation.SatelliteCenters();
            const auto& hits = simulation.SatelliteHitsHero();
            for (int i = 0; i < Simulation::kSatelliteCount; ++i)
            {
                render::MeshDraw satellite{};
                satellite.mesh = render::MeshId::Cube;
                satellite.world = math::Scaling({ 0.6f, 0.6f, 0.6f })
                                * math::Translation(centers[i] + kDemoOffset);
                satellite.color = hits[i]
                    ? math::Color{ 0.95f, 0.30f, 0.30f, 1.0f }   // in contact with the hero cube
                    : math::Color{ 0.35f, 0.75f, 0.95f, 1.0f };
                scene.meshDraws.push_back(satellite);
            }
        }
#endif
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
#endif

        // 2D overlay: obstacles, then the particle sample, then the player.
        const auto& obstacles = simulation.Obstacles();
        for (const math::Rect& rect : obstacles)
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
        const math::Color playerColor = simulation.PlayerBlocked()
            ? math::Color{ 1.0f, 0.45f, 0.30f, 1.0f }   // overlapping an obstacle
            : math::Color{ 0.20f, 0.75f, 1.0f, 1.0f };
        snapshot.worldQuads.push_back({ player.x, player.y,
                                       Simulation::kPlayerSize, Simulation::kPlayerSize,
                                       playerColor.r, playerColor.g, playerColor.b, playerColor.a });

        ui.Build(snapshot.uiQuads);
        return snapshot;
    }
}
