#pragma once

#include "import/Model.h"
#include "math/Math3D.h"

#include <cstdint>
#include <vector>

// CPU pose evaluation. Turns (skeleton + clip + time) into per-bone skinning
// matrices in model space, ready to upload to a skinning shader as
// `v_skinned = sum_i weight_i * (v * skinMatrix[boneIndex_i])`.
//
// This is the "use the animation" core. GPU skinning, pose blending, and a
// state machine are designed in docs/model-animation-research.md.
namespace engine::anim
{
    // How `timeSeconds` maps onto [0, clip.duration]. See docs/roadmap.md §2.1.
    //   Loop     : wrap (t % D)                     - continuous cycles
    //   Once     : clamp to [0, D], hold last frame - one-shot (jump, hit, ...)
    //   PingPong : 0 -> D -> 0 -> D ...             - symmetric back-and-forth
    enum class PlayMode : std::uint8_t { Loop = 0, Once = 1, PingPong = 2 };

    // Interpolated local TRS of one track at `timeSeconds` (clamped to the
    // track's key range).
    [[nodiscard]] import::BoneKey SampleTrack(const import::BoneTrack& track, float timeSeconds);

    class AnimationSampler
    {
    public:
        // out is resized to skeleton.bones.size(). `timeSeconds` is mapped onto
        // the clip by `mode` (default Loop, so existing callers are unchanged).
        void Evaluate(const import::Skeleton& skeleton,
                      const import::AnimationClip& clip,
                      float timeSeconds,
                      std::vector<math::Mat4>& outSkinMatrices,
                      PlayMode mode = PlayMode::Loop) const;

        // Crossfade: local TRS of every bone is lerp'd `from`->`to` by `blend`
        // (0 = fully `from`, 1 = fully `to`), then the hierarchy is composed
        // once. Blending local TRS (not final matrices) keeps rotations sane.
        void EvaluateBlended(const import::Skeleton& skeleton,
                             const import::AnimationClip& fromClip, float fromTime, PlayMode fromMode,
                             const import::AnimationClip& toClip, float toTime, PlayMode toMode,
                             float blend,
                             std::vector<math::Mat4>& outSkinMatrices) const;

        // Bind pose, no clip.
        void EvaluateBindPose(const import::Skeleton& skeleton,
                              std::vector<math::Mat4>& outSkinMatrices) const;

    private:
        // Scratch reused across calls to avoid per-frame allocation.
        mutable std::vector<math::Mat4> m_localMatrices;
        mutable std::vector<math::Mat4> m_modelMatrices;
    };
}
