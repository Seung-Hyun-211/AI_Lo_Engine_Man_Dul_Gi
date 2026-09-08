#include "game/SnapshotBuilder.h"

#include <algorithm>

namespace engine::game
{
    render::RenderSnapshot SnapshotBuilder::Build(std::uint64_t frameNumber,
                                                 const Simulation& simulation,
                                                 const ui::UIContext& ui) const
    {
        render::RenderSnapshot snapshot{};
        snapshot.frameNumber = frameNumber;

        const std::vector<Particle>& particles = simulation.Particles();
        const std::size_t visible = std::min(particles.size(), kVisibleParticleSample);
        snapshot.worldQuads.reserve(visible + 1);

        // Particles behind the player: small, dim, translucent dots.
        for (std::size_t index = 0; index < visible; ++index)
        {
            const Particle& particle = particles[index];
            snapshot.worldQuads.push_back({ particle.x, particle.y, 3.0f, 3.0f,
                                           0.45f, 0.55f, 0.70f, 0.60f });
        }

        // The player: an opaque cyan square, drawn last so it sits on top.
        const math::Vec2 player = simulation.PlayerPosition();
        snapshot.worldQuads.push_back({ player.x, player.y,
                                       Simulation::kPlayerSize, Simulation::kPlayerSize,
                                       0.20f, 0.75f, 1.0f, 1.0f });

        ui.Build(snapshot.uiQuads);
        return snapshot;
    }
}
