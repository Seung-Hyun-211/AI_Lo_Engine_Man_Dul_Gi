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
        // Maps an ever-increasing clock onto [0, duration] per the play mode.
        float MapTime(float timeSeconds, float duration, PlayMode mode)
        {
            if (duration <= 1e-6f) return 0.0f;
            switch (mode)
            {
            case PlayMode::Once:
                return std::clamp(timeSeconds, 0.0f, duration);
            case PlayMode::PingPong:
            {
                float p = std::fmod(timeSeconds, 2.0f * duration);
                if (p < 0.0f) p += 2.0f * duration;
                return p <= duration ? p : 2.0f * duration - p;
            }
            case PlayMode::Loop:
            default:
            {
                float t = std::fmod(timeSeconds, duration);
                if (t < 0.0f) t += duration;
                return t;
            }
            }
        }

        // Local TRS of one bone at `timeSeconds` (already mapped). Falls back to
        // the bind pose for a bone the clip does not animate.
        import::BoneKey LocalTrs(const import::Skeleton& skeleton, const import::AnimationClip* clip,
                                 int boneIndex, float timeSeconds)
        {
            if (clip != nullptr)
            {
                for (const import::BoneTrack& track : clip->tracks)
                {
                    if (track.boneIndex != boneIndex) continue;
                    return SampleTrack(track, timeSeconds);
                }
            }
            const import::Bone& bone = skeleton.bones[static_cast<std::size_t>(boneIndex)];
            import::BoneKey key;
            key.translation = bone.bindTranslation;
            key.rotation = bone.bindRotation;
            key.scale = bone.bindScale;
            return key;
        }

        void ComposeChain(const import::Skeleton& skeleton, const std::vector<math::Mat4>& localMats,
                          std::vector<math::Mat4>& model, std::vector<math::Mat4>& outSkin)
        {
            const std::size_t count = skeleton.bones.size();
            model.assign(count, math::Mat4::Identity());
            outSkin.assign(count, math::Mat4::Identity());
            for (std::size_t b = 0; b < count; ++b)
            {
                const int parent = skeleton.bones[b].parent;
                // Row-vector convention: child-local then parent-model.
                model[b] = parent >= 0 ? localMats[b] * model[static_cast<std::size_t>(parent)] : localMats[b];
                outSkin[b] = skeleton.bones[b].inverseBind * model[b];
            }
        }
    }

    void AnimationSampler::Evaluate(const import::Skeleton& skeleton,
                                    const import::AnimationClip& clip,
                                    float timeSeconds,
                                    std::vector<math::Mat4>& outSkinMatrices,
                                    PlayMode mode) const
    {
        const float t = MapTime(timeSeconds, clip.duration, mode);

        const std::size_t count = skeleton.bones.size();
        std::vector<math::Mat4>& local = m_localMatrices;
        local.assign(count, math::Mat4::Identity());
        for (std::size_t b = 0; b < count; ++b)
        {
            const import::BoneKey k = LocalTrs(skeleton, &clip, static_cast<int>(b), t);
            local[b] = math::ComposeTRS(k.translation, k.rotation, k.scale);
        }
        ComposeChain(skeleton, local, m_modelMatrices, outSkinMatrices);
    }

    void AnimationSampler::EvaluateBlended(const import::Skeleton& skeleton,
                                           const import::AnimationClip& fromClip, float fromTime, PlayMode fromMode,
                                           const import::AnimationClip& toClip, float toTime, PlayMode toMode,
                                           float blend,
                                           std::vector<math::Mat4>& outSkinMatrices) const
    {
        const float w = std::clamp(blend, 0.0f, 1.0f);
        const float tf = MapTime(fromTime, fromClip.duration, fromMode);
        const float tt = MapTime(toTime, toClip.duration, toMode);

        const std::size_t count = skeleton.bones.size();
        std::vector<math::Mat4>& local = m_localMatrices;
        local.assign(count, math::Mat4::Identity());
        for (std::size_t b = 0; b < count; ++b)
        {
            const import::BoneKey a = LocalTrs(skeleton, &fromClip, static_cast<int>(b), tf);
            const import::BoneKey c = LocalTrs(skeleton, &toClip, static_cast<int>(b), tt);
            const math::Vec3 translation = a.translation + (c.translation - a.translation) * w;
            const math::Vec3 scale = a.scale + (c.scale - a.scale) * w;
            const math::Quat rotation = math::Slerp(a.rotation, c.rotation, w);
            local[b] = math::ComposeTRS(translation, rotation, scale);
        }
        ComposeChain(skeleton, local, m_modelMatrices, outSkinMatrices);
    }

    void AnimationSampler::EvaluateBindPose(const import::Skeleton& skeleton,
                                            std::vector<math::Mat4>& outSkinMatrices) const
    {
        const std::size_t count = skeleton.bones.size();
        std::vector<math::Mat4>& local = m_localMatrices;
        local.assign(count, math::Mat4::Identity());
        for (std::size_t b = 0; b < count; ++b)
        {
            const import::Bone& bone = skeleton.bones[b];
            local[b] = math::ComposeTRS(bone.bindTranslation, bone.bindRotation, bone.bindScale);
        }
        ComposeChain(skeleton, local, m_modelMatrices, outSkinMatrices);
    }
}
