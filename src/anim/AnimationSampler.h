#pragma once

#include "import/Model.h"
#include "math/Math3D.h"

#include <vector>

// CPU pose evaluation. Turns (skeleton + clip + time) into per-bone skinning
// matrices in model space, ready to upload to a skinning shader as
// `v_skinned = sum_i weight_i * (v * skinMatrix[boneIndex_i])`.
//
// This is the "use the animation" core. GPU skinning, pose blending, and a
// state machine are designed in docs/model-animation-research.md.
namespace engine::anim
{
    // Interpolated local TRS of one track at `timeSeconds` (clamped to the
    // track's key range).
    [[nodiscard]] import::BoneKey SampleTrack(const import::BoneTrack& track, float timeSeconds);

    class AnimationSampler
    {
    public:
        // out is resized to skeleton.bones.size(). `timeSeconds` loops over
        // clip.duration.
        void Evaluate(const import::Skeleton& skeleton,
                      const import::AnimationClip& clip,
                      float timeSeconds,
                      std::vector<math::Mat4>& outSkinMatrices) const;

        // Bind pose, no clip.
        void EvaluateBindPose(const import::Skeleton& skeleton,
                              std::vector<math::Mat4>& outSkinMatrices) const;

    private:
        // Scratch reused across calls to avoid per-frame allocation.
        mutable std::vector<math::Mat4> m_modelMatrices;
    };
}
