// Standalone importer smoke test. Not part of the game build.
// Build: tools/build_fbx_probe.bat  (compiles ModelImporter + AnimationSampler + ufbx).
// Run:   fbx_probe.exe [path/to/model.fbx]

#include "import/ModelImporter.h"
#include "import/CreaseLines.h"
#include "anim/AnimationSampler.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "assets/models/unitychan/unitychan.fbx";
    std::printf("loading: %s\n", path);

    engine::import::ImportOptions opts;
    const engine::import::ImportResult result = engine::import::LoadModelFromFile(path, opts);
    if (!result.ok)
    {
        std::printf("LOAD FAILED: %s\n", result.error.c_str());
        return 1;
    }

    const engine::import::Model& model = result.model;
    std::printf("OK  meshes=%zu  materials=%zu  bones=%zu  clips=%zu\n",
        model.meshes.size(), model.materials.size(), model.skeleton.bones.size(), model.animations.size());

    std::size_t totalVerts = 0, totalIndices = 0, vertsWithUV = 0;
    int skinnedMeshes = 0;
    double minPos[3] = { 1e30, 1e30, 1e30 }, maxPos[3] = { -1e30, -1e30, -1e30 };
    std::size_t badWeightSum = 0, badBoneIndex = 0;
    const int boneCount = static_cast<int>(model.skeleton.bones.size());

    for (const engine::import::ModelMesh& mesh : model.meshes)
    {
        totalVerts += mesh.vertices.size();
        totalIndices += mesh.indices.size();
        if (mesh.skinned) ++skinnedMeshes;
        std::printf("  mesh '%s'  v=%zu  tris=%zu  material=%d  skinned=%d\n",
            mesh.name.c_str(), mesh.vertices.size(), mesh.indices.size() / 3, mesh.materialIndex, mesh.skinned ? 1 : 0);

        for (const engine::import::ModelVertex& v : mesh.vertices)
        {
            const float p[3] = { v.position.x, v.position.y, v.position.z };
            for (int k = 0; k < 3; ++k) { if (p[k] < minPos[k]) minPos[k] = p[k]; if (p[k] > maxPos[k]) maxPos[k] = p[k]; }
            if (v.uv.x != 0.0f || v.uv.y != 0.0f) ++vertsWithUV;
            if (mesh.skinned)
            {
                float sum = 0.0f;
                for (int k = 0; k < engine::import::kMaxBoneInfluences; ++k)
                {
                    sum += v.boneWeights[k];
                    if (v.boneWeights[k] > 0.0f && static_cast<int>(v.boneIndices[k]) >= boneCount) ++badBoneIndex;
                }
                if (std::fabs(sum - 1.0f) > 0.01f) ++badWeightSum;
            }
        }
    }
    std::printf("  totals: verts=%zu  tris=%zu  skinnedMeshes=%d  vertsWithNonZeroUV=%zu\n",
        totalVerts, totalIndices / 3, skinnedMeshes, vertsWithUV);
    std::printf("  bounds: x[%.2f, %.2f]  y[%.2f, %.2f]  z[%.2f, %.2f]\n",
        minPos[0], maxPos[0], minPos[1], maxPos[1], minPos[2], maxPos[2]);
    std::printf("  skin sanity: vertices with weight-sum != 1 (+/-0.01) = %zu, out-of-range bone index = %zu\n",
        badWeightSum, badBoneIndex);

    for (std::size_t i = 0; i < model.materials.size(); ++i)
    {
        const engine::import::ModelMaterial& mat = model.materials[i];
        std::printf("  material[%zu] '%s'  rgba=(%.2f %.2f %.2f %.2f)  tex='%s'\n",
            i, mat.name.c_str(), mat.baseColor.r, mat.baseColor.g, mat.baseColor.b, mat.baseColor.a, mat.diffuseTexture.c_str());
    }

    std::printf("  first bones:\n");
    for (std::size_t i = 0; i < model.skeleton.bones.size() && i < 14; ++i)
    {
        const engine::import::Bone& b = model.skeleton.bones[i];
        std::printf("    [%zu] '%s'  parent=%d\n", i, b.name.c_str(), b.parent);
    }
    int rootBones = 0, badParents = 0;
    for (std::size_t i = 0; i < model.skeleton.bones.size(); ++i)
    {
        const int parent = model.skeleton.bones[i].parent;
        if (parent < 0) ++rootBones;
        else if (parent >= static_cast<int>(i)) ++badParents;   // must be topologically sorted
    }
    std::printf("  skeleton: rootBones=%d  parents-not-before-child=%d\n", rootBones, badParents);

    for (const engine::import::AnimationClip& clip : model.animations)
        std::printf("  clip '%s'  duration=%.3fs  sampleRate=%.0f  tracks=%zu\n",
            clip.name.c_str(), clip.duration, clip.sampleRate, clip.tracks.size());

    if (model.HasSkeleton() && model.HasAnimation())
    {
        engine::anim::AnimationSampler sampler;
        std::vector<engine::math::Mat4> skin;
        const float t = model.animations[0].duration * 0.5f;
        sampler.Evaluate(model.skeleton, model.animations[0], t, skin);
        bool finite = true;
        for (const engine::math::Mat4& m : skin)
            for (float f : m.m) if (!std::isfinite(f)) finite = false;
        std::printf("  AnimationSampler @%.3fs -> %zu skin matrices, all finite=%d, bone[0] translate=(%.3f %.3f %.3f)\n",
            t, skin.size(), finite ? 1 : 0, skin[0].m[12], skin[0].m[13], skin[0].m[14]);
    }

    // Crease-line segment counts at a few normal-angle thresholds (helps tune
    // ModelMeshPass3D / import::CreaseOptions::thresholdDegrees).
    std::printf("  crease segments by threshold (angle between adjacent face normals):\n");
    for (float threshold : { 90.0f, 60.0f, 45.0f, 30.0f, 20.0f })
    {
        std::size_t segs = 0;
        for (const engine::import::ModelMesh& mesh : model.meshes)
        {
            engine::import::CreaseOptions opt;
            opt.thresholdDegrees = threshold;
            segs += engine::import::BuildCreaseLines(mesh, nullptr, nullptr, opt).size() / 6;
        }
        std::printf("    > %4.0f deg : %zu segments\n", threshold, segs);
    }

    std::printf("done.\n");
    return 0;
}
