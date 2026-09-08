#pragma once

#include "math/Math2D.h"
#include "math/Math3D.h"

#include <cstdint>
#include <string>
#include <vector>

// Engine-side model representation. The FBX loader (ModelImporter) fills these;
// no ufbx type ever escapes into the rest of the engine. Built only when the 3D
// module is present.
namespace engine::import
{
    inline constexpr int kMaxBoneInfluences = 4;

    // One skinned vertex. `boneIndices` refer into Skeleton::bones; unused slots
    // have weight 0. Weights are normalised to sum to 1.
    struct ModelVertex
    {
        math::Vec3 position{};
        math::Vec3 normal{};
        math::Vec2 uv{};
        std::uint32_t boneIndices[kMaxBoneInfluences]{};
        float boneWeights[kMaxBoneInfluences]{};
    };

    struct ModelMesh
    {
        std::string name;
        std::vector<ModelVertex> vertices;
        std::vector<std::uint32_t> indices;   // triangle list
        int materialIndex{ -1 };
        bool skinned{ false };
    };

    struct ModelMaterial
    {
        std::string name;
        math::Color baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string diffuseTexture;   // filename as referenced by the FBX (may be empty)
    };

    // A joint. `parent` is an index into Skeleton::bones, or -1 for a root.
    // `inverseBind` takes a vertex from model space into this bone's local space
    // at bind pose; the skinning matrix is `inverseBind * boneModelTransform`.
    struct Bone
    {
        std::string name;
        int parent{ -1 };
        math::Mat4 inverseBind{ math::Mat4::Identity() };
        // Local bind transform (relative to parent), used as the default pose
        // for bones a clip does not animate.
        math::Vec3 bindTranslation{};
        math::Quat bindRotation{};
        math::Vec3 bindScale{ 1.0f, 1.0f, 1.0f };
    };

    struct Skeleton
    {
        std::vector<Bone> bones;   // topologically sorted: parent index < child index
        [[nodiscard]] bool Empty() const { return bones.empty(); }
    };

    // One bone's local TRS at a keyframe time (seconds).
    struct BoneKey
    {
        float time{};
        math::Vec3 translation{};
        math::Quat rotation{};
        math::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    // Keyframes for a single bone, sorted by time.
    struct BoneTrack
    {
        int boneIndex{ -1 };
        std::vector<BoneKey> keys;
    };

    // A named animation. Keys are baked at a fixed sample rate by the importer;
    // AnimationSampler interpolates between them.
    struct AnimationClip
    {
        std::string name;
        float duration{};          // seconds
        float sampleRate{ 30.0f }; // keys per second used when baking
        std::vector<BoneTrack> tracks;
    };

    struct Model
    {
        std::vector<ModelMesh> meshes;
        std::vector<ModelMaterial> materials;
        Skeleton skeleton;
        std::vector<AnimationClip> animations;

        [[nodiscard]] bool HasSkeleton() const { return !skeleton.Empty(); }
        [[nodiscard]] bool HasAnimation() const { return !animations.empty(); }
    };
}
