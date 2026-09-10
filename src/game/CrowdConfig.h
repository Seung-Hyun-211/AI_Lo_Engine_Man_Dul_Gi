#pragma once

#include <cstdint>

// How the demo-scene-2 crowd is sized and drawn. Everything that used to be a
// scattered constant in Simulation / SnapshotBuilder now reads one of these
// presets. To change the demo, point kActiveCrowd at a different preset; to add
// a model, add a preset and (for a new FBX) set kCrowdModelFbx in
// render/r3d/MeshPass3D.cpp. Built only with the 3D module.
namespace engine::game
{
    enum class CrowdMesh : std::uint8_t
    {
        Cube,    // the renderer's built-in unit cube (MeshId::Cube)
        Model,   // the FBX MeshPass3D loads into MeshId::CrowdModel (kCrowdModelFbx)
    };

    struct CrowdConfig
    {
        int       count;           // live agents on the field
        int       capacity;        // core::ObjectPool slot count (>= count, spawn/despawn headroom)
        CrowdMesh mesh;            // which mesh each agent is drawn as
        float     height;          // rendered instance height in metres (inst.scale)
        float     colliderRadius;  // gameplay collision sphere radius (centre = height * 0.5 above the feet)
    };

    // --- presets -------------------------------------------------------------
    // The original demo: 600 cubes. Restore with `kActiveCrowd = kCrowdBoxes`.
    inline constexpr CrowdConfig kCrowdBoxes{
        /*count*/ 600, /*capacity*/ 1024, CrowdMesh::Cube, /*height*/ 0.5f, /*colliderRadius*/ 0.30f };

    // 1500 instanced zombie meshes (assets/models/zombie/Zombie1.FBX, static).
    inline constexpr CrowdConfig kCrowdZombies{
        /*count*/ 1500, /*capacity*/ 2048, CrowdMesh::Model, /*height*/ 1.8f, /*colliderRadius*/ 0.50f };

    // ---- the one knob ----
    inline constexpr CrowdConfig kActiveCrowd = kCrowdZombies;

    [[nodiscard]] inline constexpr bool CrowdUsesModel() { return kActiveCrowd.mesh == CrowdMesh::Model; }
}
