#include "game/SnapshotBuilder.h"

#include "math/Math.h"

#include <algorithm>
#include <cmath>

namespace engine::game
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        render::CameraView BuildCamera(float elapsed, int viewportWidth, int viewportHeight)
        {
            const float aspect = viewportHeight > 0
                ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
                : 1.0f;

            // Slow orbit around the scene origin.
            const float angle = elapsed * 0.5f;
            const math::Vec3 eye{ std::sin(angle) * 6.0f, 3.5f, -std::cos(angle) * 6.0f };
            const math::Vec3 target{ 0.0f, 0.5f, 0.0f };

            render::CameraView camera{};
            camera.view = math::LookAtLH(eye, target, { 0.0f, 1.0f, 0.0f });
            camera.projection = math::PerspectiveFovLH(kPi / 3.0f, aspect, 0.1f, 100.0f);
            camera.lightDirection = { 0.4f, -1.0f, 0.35f };
            return camera;
        }

        void BuildMeshes(std::vector<render::MeshDraw>& meshes, float elapsed)
        {
            // Ground plane.
            render::MeshDraw ground{};
            ground.mesh = render::MeshId::Plane;
            ground.world = math::Scaling({ 14.0f, 1.0f, 14.0f });
            ground.color = { 0.16f, 0.18f, 0.22f, 1.0f };
            meshes.push_back(ground);

            // Centre cube, spinning on two axes.
            render::MeshDraw hero{};
            hero.mesh = render::MeshId::Cube;
            hero.world = math::RotationX(elapsed * 0.6f) * math::RotationY(elapsed)
                       * math::Translation({ 0.0f, 0.9f, 0.0f });
            hero.color = { 0.95f, 0.55f, 0.25f, 1.0f };
            meshes.push_back(hero);

            // Two smaller satellite cubes.
            for (int i = 0; i < 2; ++i)
            {
                const float phase = elapsed * 0.8f + static_cast<float>(i) * kPi;
                render::MeshDraw satellite{};
                satellite.mesh = render::MeshId::Cube;
                satellite.world = math::Scaling({ 0.5f, 0.5f, 0.5f })
                                * math::Translation({ std::cos(phase) * 2.6f, 0.5f, std::sin(phase) * 2.6f });
                satellite.color = i == 0
                    ? math::Color{ 0.30f, 0.70f, 0.95f, 1.0f }
                    : math::Color{ 0.55f, 0.85f, 0.40f, 1.0f };
                meshes.push_back(satellite);
            }
        }
    }

    render::RenderSnapshot SnapshotBuilder::Build(std::uint64_t frameNumber,
                                                 const Simulation& simulation,
                                                 const ui::UIContext& ui,
                                                 int viewportWidth,
                                                 int viewportHeight) const
    {
        render::RenderSnapshot snapshot{};
        snapshot.frameNumber = frameNumber;

        // 3D scene.
        snapshot.camera = BuildCamera(simulation.ElapsedTime(), viewportWidth, viewportHeight);
        BuildMeshes(snapshot.meshDraws, simulation.ElapsedTime());

        // 2D world overlay: particle sample + player square.
        const std::vector<Particle>& particles = simulation.Particles();
        const std::size_t visible = std::min(particles.size(), kVisibleParticleSample);
        snapshot.worldQuads.reserve(visible + 1);
        for (std::size_t index = 0; index < visible; ++index)
        {
            const Particle& particle = particles[index];
            snapshot.worldQuads.push_back({ particle.x, particle.y, 3.0f, 3.0f,
                                           0.45f, 0.55f, 0.70f, 0.55f });
        }
        const math::Vec2 player = simulation.PlayerPosition();
        snapshot.worldQuads.push_back({ player.x, player.y,
                                       Simulation::kPlayerSize, Simulation::kPlayerSize,
                                       0.20f, 0.75f, 1.0f, 1.0f });

        ui.Build(snapshot.uiQuads);
        return snapshot;
    }
}
