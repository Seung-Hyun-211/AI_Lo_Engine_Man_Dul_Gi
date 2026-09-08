#pragma once

#include "import/Model.h"
#include "import/TgaImage.h"
#include "math/Math2D.h"
#include "math/Math3D.h"

#include <vector>

// Builds interior "crease" line geometry for a mesh: for every edge shared by
// exactly two triangles whose face normals differ by more than a threshold
// angle, emits a thin surface ribbon. Thickness scales with the angle; the
// colour is the desaturated, slightly darkened average of the two adjacent
// faces' surface colours, so the lines read like baked ambient occlusion.
//
// Pure CPU / geometry - no D3D. Runs once at model load.
namespace engine::import
{
    struct CreaseVertex
    {
        math::Vec3 position{};   // model space, same as ModelVertex.position
        math::Color color{};
    };

    struct CreaseOptions
    {
        // Draw a crease when the angle between the two face normals exceeds this.
        float thresholdDegrees{ 90.0f };
        // Model-space half-width at the threshold angle and at 180 degrees.
        float minHalfWidth{ 0.00133f };
        float maxHalfWidth{ 0.00667f };
        // Line colour = lerp(luma, avgFaceColor, saturationScale) * valueScale.
        float saturationScale{ 0.65f };
        float valueScale{ 0.80f };
        // Push the ribbon off the surface along the fold normal to beat z-fight.
        float surfaceOffset{ 0.0015f };
        // Positions within this distance are treated as the same welded vertex.
        float weldEpsilon{ 1.0e-4f };
        // Safety cap on emitted vertices.
        std::size_t maxVertices{ 600'000 };
    };

    [[nodiscard]] std::vector<CreaseVertex> BuildCreaseLines(
        const ModelMesh& mesh,
        const TgaImage* texture,        // nullptr -> material colour only
        const ModelMaterial* material,  // nullptr -> white
        const CreaseOptions& options = {});

    // Per-vertex smoothed normal, same order/length as mesh.vertices: the
    // area-weighted average of the geometric face normals of every triangle that
    // shares the vertex position. Used to inflate the silhouette hull so it
    // stays continuous across hard-normal seams (a jagged outline otherwise).
    [[nodiscard]] std::vector<math::Vec3> BuildSmoothNormals(const ModelMesh& mesh, float weldEpsilon = 1.0e-4f);
}
