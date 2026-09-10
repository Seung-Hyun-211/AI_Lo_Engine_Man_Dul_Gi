#pragma once

#include "game/Simulation.h"
#include "render/RenderSnapshot.h"
#include "render/r2d/TextureAtlas.h"
#include "ui/UI.h"

#include <cstddef>
#include <cstdint>

namespace engine::game
{
    // Turns the finished simulation and UI tree into a value-only RenderSnapshot.
    // This is the single place that decides how a game concept (the player, a
    // particle) becomes a renderer primitive (a Quad), which keeps that mapping
    // out of both the simulation and the renderer.
    class SnapshotBuilder final
    {
    public:
        // Upper bound on particles copied into the snapshot. The rest still run
        // in the simulation as the JobSystem benchmark; they are just not drawn.
        static constexpr std::size_t kVisibleParticleSample = 2'048;

        // viewportWidth/Height set the 3D camera's aspect ratio. `uiAtlas` (may
        // be nullptr) is the resident UI atlas manifest used to resolve named
        // sprites into SpriteDraw uv rects. `fps` (> 0) is drawn top-right.
        [[nodiscard]] render::RenderSnapshot Build(std::uint64_t frameNumber,
                                                   const Simulation& simulation,
                                                   const ui::UIContext& ui,
                                                   int viewportWidth,
                                                   int viewportHeight,
                                                   const render::AtlasIndex* uiAtlas = nullptr,
                                                   float fps = 0.0f) const;
    };
}
