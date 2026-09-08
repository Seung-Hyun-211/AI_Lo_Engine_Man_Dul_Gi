#include "anim/AnimationSampler.h"

#include <algorithm>
#include <cmath>

namespace engine::anim
{
    import::BoneKey SampleTrack(const import::BoneTrack& track, float timeSeconds)
    {
        if (track.keys.empty()) return {};
        if (track.keys.size() == 1 || timeSeconds <= track.keys.front().time) return track.keys.front();
        if (timeSeconds >= track.keys.back().time) return track.keys.back();

        // Linear scan is fine for baked tracks (keys are dense and short-lived);
        // switch to a cursor or binary search if tracks get long.
        std::size_t next = 1;
        while (next < track.keys.size() && track.keys[next].time < timeSeconds) ++next;

        const import::BoneKey& a = track.keys[next - 1];
        const import::BoneKey& b = track.keys[next];
        const float span = b.time - a.time;
        const float t = span > 1e-6f ? (timeSeconds - a.time) / span : 0.0f;

        import::BoneKey out;
        out.time = timeSeconds;
        out.translation = a.translation + (b.translation - a.translation) * t;
        out.scale = a.scale + (b.scale - a.scale) * t;
        out.rotation = math::Slerp(a.rotation, b.rotation, t);
        return out;
    }

    namespace
    {
        math::Mat4 LocalMatrix(const import::Skeleton& skeleton, const import::AnimationClip* clip,
                               int boneIndex, float timeSeconds)
        {
            const import::Bone& bone = skeleton.bones[static_cast<std::size_t>(boneIndex)];

            if (clip != nullptr)
            {
                for (const import::BoneTrack& track : clip->tracks)
                {
                    if (track.boneIndex != boneIndex) continue;
                    const import::BoneKey key = SampleTrack(track, timeSeconds);
                    return math::ComposeTRS(key.translation, key.rotation, key.scale);
                }
            }
            // Bone the clip does not touch: hold its bind pose.
            return math::ComposeTRS(bone.bindTranslation, bone.bindRotation, bone.bindScale);
        }

        void EvaluateInternal(const import::Skeleton& skeleton, const import::AnimationClip* clip,
                              float timeSeconds, std::vector<math::Mat4>& model,
                              std::vector<math::Mat4>& outSkin)
        {
            const std::size_t count = skeleton.bones.size();
            model.assign(count, math::Mat4::Identity());
            outSkin.assign(count, math::Mat4::Identity());

            for (std::size_t b = 0; b < count; ++b)
            {
                const math::Mat4 local = LocalMatrix(skeleton, clip, static_cast<int>(b), timeSeconds);
                const int parent = skeleton.bones[b].parent;
                // Row-vector convention: child-local then parent-model.
                model[b] = parent >= 0 ? local * model[static_cast<std::size_t>(parent)] : local;
                outSkin[b] = skeleton.bones[b].inverseBind * model[b];
            }
        }
    }

    void AnimationSampler::Evaluate(const import::Skeleton& skeleton,
                                    const import::AnimationClip& clip,
                                    float timeSeconds,
                                    std::vector<math::Mat4>& outSkinMatrices) const
    {
        float t = timeSeconds;
        if (clip.duration > 1e-6f)
        {
            t = std::fmod(timeSeconds, clip.duration);
            if (t < 0.0f) t += clip.duration;
        }
        EvaluateInternal(skeleton, &clip, t, m_modelMatrices, outSkinMatrices);
    }

    void AnimationSampler::EvaluateBindPose(const import::Skeleton& skeleton,
                                            std::vector<math::Mat4>& outSkinMatrices) const
    {
        EvaluateInternal(skeleton, nullptr, 0.0f, m_modelMatrices, outSkinMatrices);
    }
}
