#include "import/ModelImporter.h"

#include "vendor/ufbx/ufbx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace engine::import
{
    namespace
    {
        [[nodiscard]] std::string ToStd(ufbx_string s)
        {
            return s.data != nullptr ? std::string(s.data, s.length) : std::string{};
        }

        [[nodiscard]] math::Vec3 ToVec3(ufbx_vec3 v)
        {
            return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
        }

        [[nodiscard]] math::Quat ToQuat(ufbx_quat q)
        {
            return { static_cast<float>(q.x), static_cast<float>(q.y),
                     static_cast<float>(q.z), static_cast<float>(q.w) };
        }

        // ufbx_matrix is a 4x3 affine, column-vector convention (p' = M * p).
        // Our math::Mat4 is row-major, row-vector convention (p' = p * M), so
        // this writes the transpose of the 3x3 with the translation as the
        // fourth row.
        [[nodiscard]] math::Mat4 ToMat4(const ufbx_matrix& u)
        {
            math::Mat4 r{};
            r.m[0] = static_cast<float>(u.m00); r.m[1] = static_cast<float>(u.m10); r.m[2] = static_cast<float>(u.m20); r.m[3] = 0.0f;
            r.m[4] = static_cast<float>(u.m01); r.m[5] = static_cast<float>(u.m11); r.m[6] = static_cast<float>(u.m21); r.m[7] = 0.0f;
            r.m[8] = static_cast<float>(u.m02); r.m[9] = static_cast<float>(u.m12); r.m[10] = static_cast<float>(u.m22); r.m[11] = 0.0f;
            r.m[12] = static_cast<float>(u.m03); r.m[13] = static_cast<float>(u.m13); r.m[14] = static_cast<float>(u.m23); r.m[15] = 1.0f;
            return r;
        }

        ImportResult Fail(std::string message)
        {
            ImportResult result;
            result.ok = false;
            result.error = std::move(message);
            return result;
        }

        // ---- skeleton -------------------------------------------------------

        // Collects the skeleton, then orders it so a parent always precedes its
        // children. `boneNodeByIndex` ends up parallel to model.skeleton.bones.
        //
        // Two sources: every bone node referenced by a mesh's skin, PLUS every
        // node the scene marks as a bone (`node->bone`). The second source is
        // what lets a non-skinned mesh that is merely *parented* under a bone
        // (Unity-chan's face / eye / mouth parts hang off the head bone) resolve
        // an attachBone in BuildMesh - the skin clusters alone do not name every
        // bone in the chain up to that mesh's parent.
        void BuildSkeleton(const ufbx_scene* scene, Model& model,
                           std::unordered_map<const ufbx_node*, int>& boneIndexOf,
                           std::vector<const ufbx_node*>& boneNodeByIndex, float scale)
        {
            std::vector<const ufbx_node*> boneNodes;
            auto consider = [&](const ufbx_node* node)
            {
                if (node != nullptr && boneIndexOf.find(node) == boneIndexOf.end())
                {
                    boneIndexOf.emplace(node, -1);   // mark, index assigned below
                    boneNodes.push_back(node);
                }
            };

            for (size_t mi = 0; mi < scene->meshes.count; ++mi)
            {
                const ufbx_mesh* mesh = scene->meshes.data[mi];
                for (size_t di = 0; di < mesh->skin_deformers.count; ++di)
                {
                    const ufbx_skin_deformer* skin = mesh->skin_deformers.data[di];
                    for (size_t ci = 0; ci < skin->clusters.count; ++ci)
                        consider(skin->clusters.data[ci]->bone_node);
                }
            }
            for (size_t ni = 0; ni < scene->nodes.count; ++ni)
                if (scene->nodes.data[ni]->bone != nullptr) consider(scene->nodes.data[ni]);

            if (boneNodes.empty()) return;

            // Sort by node depth so parents come first.
            std::sort(boneNodes.begin(), boneNodes.end(),
                [](const ufbx_node* a, const ufbx_node* b) { return a->node_depth < b->node_depth; });

            model.skeleton.bones.reserve(boneNodes.size());
            boneNodeByIndex.reserve(boneNodes.size());
            for (const ufbx_node* node : boneNodes)
            {
                const int index = static_cast<int>(model.skeleton.bones.size());
                boneIndexOf[node] = index;
                boneNodeByIndex.push_back(node);

                Bone bone;
                bone.name = ToStd(node->name);

                // Nearest ancestor that is itself a bone.
                bone.parent = -1;
                for (const ufbx_node* p = node->parent; p != nullptr; p = p->parent)
                {
                    const auto it = boneIndexOf.find(p);
                    if (it != boneIndexOf.end() && it->second >= 0) { bone.parent = it->second; break; }
                }

                // Same uniform scale the mesh vertices get (BuildMesh): a bone's
                // local offset scales with the world, so the skeleton and the
                // clips below stay in the same units as the skinned vertices.
                // Rotation and (non-uniform) local scale are unaffected.
                bone.bindTranslation = ToVec3(node->local_transform.translation) * scale;
                bone.bindRotation = ToQuat(node->local_transform.rotation);
                bone.bindScale = ToVec3(node->local_transform.scale);
                model.skeleton.bones.push_back(std::move(bone));
            }
        }

        // ---- meshes -------------------------------------------------------

        void BuildMesh(const ufbx_mesh* mesh, const Model& model,
                       const std::unordered_map<const ufbx_node*, int>& boneIndexOf,
                       float scale, Model& out)
        {
            const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;

            // A mesh with no skin may still be rigidly parented under a bone
            // (Unity-chan's face/eye/mouth parts hang off the head bone). Find
            // the nearest ancestor node that is a skeleton bone so the renderer
            // can move the whole mesh with that bone.
            int attachBone = -1;
            if (skin == nullptr && mesh->instances.count > 0)
            {
                for (const ufbx_node* p = mesh->instances.data[0]->parent; p != nullptr; p = p->parent)
                {
                    const auto it = boneIndexOf.find(p);
                    if (it != boneIndexOf.end() && it->second >= 0) { attachBone = it->second; break; }
                }
            }

            std::vector<uint32_t> triIndices(std::max<size_t>(mesh->max_face_triangles, 1) * 3);

            // One ModelMesh per material part (or a single part if unmaterialised).
            const size_t partCount = mesh->material_parts.count > 0 ? mesh->material_parts.count : 1;
            for (size_t pi = 0; pi < partCount; ++pi)
            {
                ModelMesh dst;
                dst.name = ToStd(mesh->name);
                if (dst.name.empty() && mesh->instances.count > 0)
                    dst.name = ToStd(mesh->instances.data[0]->name);   // name lives on the node
                dst.skinned = skin != nullptr;
                dst.attachBone = attachBone;

                const ufbx_mesh_part* part = mesh->material_parts.count > 0 ? &mesh->material_parts.data[pi] : nullptr;
                dst.materialIndex = -1;
                if (part != nullptr && mesh->materials.count > pi && mesh->materials.data[pi] != nullptr)
                {
                    // Map the FBX material to our imported index by name.
                    const std::string wanted = ToStd(mesh->materials.data[pi]->name);
                    for (size_t m = 0; m < out.materials.size(); ++m)
                        if (out.materials[m].name == wanted) { dst.materialIndex = static_cast<int>(m); break; }
                }

                const size_t faceCount = part != nullptr ? part->num_faces : mesh->faces.count;
                for (size_t f = 0; f < faceCount; ++f)
                {
                    const uint32_t faceIndex = part != nullptr ? part->face_indices.data[f] : static_cast<uint32_t>(f);
                    const ufbx_face face = mesh->faces.data[faceIndex];
                    if (face.num_indices < 3) continue;

                    const uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
                    for (uint32_t t = 0; t < numTris * 3; ++t)
                    {
                        const uint32_t corner = triIndices[t];

                        ModelVertex v{};
                        v.position = ToVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, corner)) * scale;
                        if (mesh->vertex_normal.exists)
                            v.normal = ToVec3(ufbx_get_vertex_vec3(&mesh->vertex_normal, corner));
                        if (mesh->vertex_uv.exists)
                        {
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);
                            v.uv = { static_cast<float>(uv.x), static_cast<float>(uv.y) };
                        }

                        if (skin != nullptr)
                        {
                            const uint32_t logical = mesh->vertex_indices.data[corner];
                            const ufbx_skin_vertex sv = skin->vertices.data[logical];
                            float total = 0.0f;
                            const uint32_t take = std::min<uint32_t>(sv.num_weights, kMaxBoneInfluences);
                            for (uint32_t w = 0; w < take; ++w)
                            {
                                const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
                                const ufbx_node* boneNode = skin->clusters.data[sw.cluster_index]->bone_node;
                                const auto it = boneNode != nullptr ? boneIndexOf.find(boneNode) : boneIndexOf.end();
                                v.boneIndices[w] = it != boneIndexOf.end() && it->second >= 0
                                    ? static_cast<uint32_t>(it->second) : 0u;
                                v.boneWeights[w] = static_cast<float>(sw.weight);
                                total += v.boneWeights[w];
                            }
                            if (total > 1e-6f)
                                for (float& weight : v.boneWeights) weight /= total;
                        }

                        dst.indices.push_back(static_cast<uint32_t>(dst.vertices.size()));
                        dst.vertices.push_back(v);
                    }
                }

                if (!dst.vertices.empty()) out.meshes.push_back(std::move(dst));
            }

            (void)model;
        }

        // ---- animation ---------------------------------------------------

        // Bakes one anim stack to fixed-rate keys for every bone that has a
        // matching node in `boneNode` (parallel to the target skeleton's bone
        // list - a null entry means "this file doesn't animate that bone").
        // Shared by the self-contained path (BuildAnimations, skeleton + clip
        // in the same FBX) and the retargeted path (LoadAnimationClipsFromFile,
        // clip in a separate bones-only FBX).
        AnimationClip BakeClip(const ufbx_anim_stack* stack, const std::vector<const ufbx_node*>& boneNode,
                               const Skeleton& skeleton, float sampleRate, float scale)
        {
            AnimationClip clip;
            const double begin = stack->time_begin;
            const double end = stack->time_end;
            clip.duration = static_cast<float>(end - begin);
            if (clip.duration <= 0.0f) return clip;

            clip.name = ToStd(stack->name);
            clip.sampleRate = sampleRate;

            const float dt = sampleRate > 0.0f ? 1.0f / sampleRate : 1.0f / 30.0f;
            const int steps = std::max(1, static_cast<int>(std::ceil(clip.duration / dt)));
            for (size_t b = 0; b < boneNode.size(); ++b)
            {
                if (boneNode[b] == nullptr) continue;
                BoneTrack track;
                track.boneIndex = static_cast<int>(b);
                track.keys.reserve(static_cast<size_t>(steps) + 1);
                for (int k = 0; k <= steps; ++k)
                {
                    const double time = begin + std::min(static_cast<double>(k) * dt, static_cast<double>(clip.duration));
                    const ufbx_transform xf = ufbx_evaluate_transform(stack->anim, boneNode[b], time);
                    BoneKey key;
                    key.time = static_cast<float>(time - begin);
                    key.translation = ToVec3(xf.translation) * scale;   // match the skinned vertices' unit
                    key.rotation = ToQuat(xf.rotation);
                    key.scale = ToVec3(xf.scale);
                    track.keys.push_back(key);
                }
                clip.tracks.push_back(std::move(track));
            }

            // Unity-chan clip takes declare time_begin one frame before real
            // content, so key 0 evaluates to the rest (bind) pose and pops a
            // 1-frame T-pose at every loop and every state change. Drop that
            // leading frame - but only if every animated bone's first key really
            // is its bind pose, so a clip that legitimately starts at rest with
            // its own keyframe there is left alone.
            auto matchesBind = [&](const BoneTrack& tr)
            {
                const Bone& bn = skeleton.bones[static_cast<std::size_t>(tr.boneIndex)];
                const BoneKey& k = tr.keys.front();
                return math::Length(k.translation - bn.bindTranslation) < 1e-4f
                    && std::abs(math::Dot(k.rotation, bn.bindRotation)) > 0.99995f
                    && math::Length(k.scale - bn.bindScale) < 1e-3f;
            };
            if (!clip.tracks.empty() && clip.tracks.front().keys.size() > 2)
            {
                bool allBind = true;
                for (const BoneTrack& tr : clip.tracks)
                    if (tr.keys.empty() || !matchesBind(tr)) { allBind = false; break; }
                if (allBind)
                {
                    const float shift = clip.tracks.front().keys[1].time;
                    for (BoneTrack& tr : clip.tracks)
                    {
                        tr.keys.erase(tr.keys.begin());
                        for (BoneKey& k : tr.keys) k.time -= shift;
                    }
                    clip.duration -= shift;
                }
            }
            return clip;
        }

        void BuildAnimations(const ufbx_scene* scene, Model& model, float sampleRate, float scale)
        {
            if (model.skeleton.Empty()) return;

            // Reverse map: bone index -> its node, for evaluation.
            std::vector<const ufbx_node*> boneNode(model.skeleton.bones.size(), nullptr);
            for (size_t mi = 0; mi < scene->meshes.count; ++mi)
            {
                const ufbx_mesh* mesh = scene->meshes.data[mi];
                for (size_t di = 0; di < mesh->skin_deformers.count; ++di)
                {
                    const ufbx_skin_deformer* skin = mesh->skin_deformers.data[di];
                    for (size_t ci = 0; ci < skin->clusters.count; ++ci)
                    {
                        const ufbx_node* node = skin->clusters.data[ci]->bone_node;
                        // find bone by name (skeleton was built from the same nodes)
                        if (node == nullptr) continue;
                        const std::string name = ToStd(node->name);
                        for (size_t b = 0; b < model.skeleton.bones.size(); ++b)
                            if (model.skeleton.bones[b].name == name) { boneNode[b] = node; break; }
                    }
                }
            }

            for (size_t si = 0; si < scene->anim_stacks.count; ++si)
            {
                AnimationClip clip = BakeClip(scene->anim_stacks.data[si], boneNode, model.skeleton, sampleRate, scale);
                if (!clip.tracks.empty()) model.animations.push_back(std::move(clip));
            }
        }

        // ---- inverse bind ----------------------------------------------

        void FillInverseBind(const ufbx_scene* scene, Model& model,
                             const std::unordered_map<const ufbx_node*, int>& boneIndexOf,
                             const std::vector<const ufbx_node*>& boneNodeByIndex, float scale)
        {
            std::vector<bool> fromCluster(model.skeleton.bones.size(), false);

            for (size_t mi = 0; mi < scene->meshes.count; ++mi)
            {
                const ufbx_mesh* mesh = scene->meshes.data[mi];
                for (size_t di = 0; di < mesh->skin_deformers.count; ++di)
                {
                    const ufbx_skin_deformer* skin = mesh->skin_deformers.data[di];
                    for (size_t ci = 0; ci < skin->clusters.count; ++ci)
                    {
                        const ufbx_skin_cluster* cluster = skin->clusters.data[ci];
                        const auto it = cluster->bone_node != nullptr ? boneIndexOf.find(cluster->bone_node) : boneIndexOf.end();
                        if (it != boneIndexOf.end() && it->second >= 0)
                        {
                            // Rebasing a rigid transform into the uniformly
                            // scaled space only touches its translation (see
                            // BuildSkeleton) - keeps inverseBind * bindGlobal
                            // identity so the bind pose is unchanged.
                            math::Mat4 ib = ToMat4(cluster->geometry_to_bone);
                            ib.m[12] *= scale; ib.m[13] *= scale; ib.m[14] *= scale;
                            model.skeleton.bones[it->second].inverseBind = ib;
                            fromCluster[static_cast<std::size_t>(it->second)] = true;
                        }
                    }
                }
            }

            // Bones that no skin cluster names (the extra parent/attachment bones
            // seeded from node->bone): a mesh rigidly parented under one needs
            // inverseBind = inverse(bone bind world). The character meshes share
            // an identity geometry transform, so this matches what a cluster's
            // geometry_to_bone would give for the same bone.
            for (std::size_t b = 0; b < model.skeleton.bones.size(); ++b)
            {
                if (fromCluster[b] || boneNodeByIndex[b] == nullptr) continue;
                const ufbx_matrix inv = ufbx_matrix_invert(&boneNodeByIndex[b]->node_to_world);
                math::Mat4 ib = ToMat4(inv);
                ib.m[12] *= scale; ib.m[13] *= scale; ib.m[14] *= scale;
                model.skeleton.bones[b].inverseBind = ib;
            }
        }

        ImportResult BuildModel(ufbx_scene* scene, const ImportOptions& options)
        {
            ImportResult result;
            result.ok = true;
            Model& model = result.model;

            // Materials first so meshes can reference them by index.
            model.materials.reserve(scene->materials.count);
            for (size_t m = 0; m < scene->materials.count; ++m)
            {
                const ufbx_material* src = scene->materials.data[m];
                ModelMaterial dst;
                dst.name = ToStd(src->name);

                // Prefer the PBR base color; fall back to the legacy FBX diffuse
                // color (many exporters, e.g. the custom-shader Unity-chan
                // materials, leave the PBR maps at zero).
                const ufbx_material_map* colorMap = src->pbr.base_color.has_value ? &src->pbr.base_color
                                                  : src->fbx.diffuse_color.has_value ? &src->fbx.diffuse_color
                                                  : nullptr;
                if (colorMap != nullptr)
                {
                    dst.baseColor = { static_cast<float>(colorMap->value_vec4.x), static_cast<float>(colorMap->value_vec4.y),
                                      static_cast<float>(colorMap->value_vec4.z),
                                      colorMap->value_components >= 4 ? static_cast<float>(colorMap->value_vec4.w) : 1.0f };
                }
                if (dst.baseColor.a <= 0.0f) dst.baseColor.a = 1.0f;

                const ufbx_texture* tex = src->pbr.base_color.texture != nullptr ? src->pbr.base_color.texture
                                        : src->fbx.diffuse_color.texture;
                if (tex != nullptr)
                {
                    dst.diffuseTexture = ToStd(tex->relative_filename);
                    if (dst.diffuseTexture.empty()) dst.diffuseTexture = ToStd(tex->filename);
                }
                model.materials.push_back(std::move(dst));
            }

            std::unordered_map<const ufbx_node*, int> boneIndexOf;
            std::vector<const ufbx_node*> boneNodeByIndex;
            BuildSkeleton(scene, model, boneIndexOf, boneNodeByIndex, options.scale);
            FillInverseBind(scene, model, boneIndexOf, boneNodeByIndex, options.scale);

            for (size_t mi = 0; mi < scene->meshes.count; ++mi)
                BuildMesh(scene->meshes.data[mi], model, boneIndexOf, options.scale, model);

            if (!options.skipAnimation)
                BuildAnimations(scene, model, options.animationSampleRate, options.scale);

            return result;
        }

        ufbx_load_opts MakeOpts(const ImportOptions&)
        {
            ufbx_load_opts opts{};
            opts.generate_missing_normals = true;
            opts.target_axes = ufbx_axes_left_handed_y_up;   // match the engine's LH world
            opts.target_unit_meters = 1.0f;
            return opts;
        }
    }

    ImportResult LoadModelFromFile(const std::string& path, const ImportOptions& options)
    {
        const ufbx_load_opts opts = MakeOpts(options);
        ufbx_error error{};
        ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
        if (scene == nullptr) return Fail(ToStd(error.description));

        ImportResult result = BuildModel(scene, options);
        ufbx_free_scene(scene);
        return result;
    }

    ImportResult LoadModelFromMemory(const void* data, std::size_t size, const ImportOptions& options)
    {
        const ufbx_load_opts opts = MakeOpts(options);
        ufbx_error error{};
        ufbx_scene* scene = ufbx_load_memory(data, size, &opts, &error);
        if (scene == nullptr) return Fail(ToStd(error.description));

        ImportResult result = BuildModel(scene, options);
        ufbx_free_scene(scene);
        return result;
    }

    AnimationImportResult LoadAnimationClipsFromFile(const std::string& path, const Skeleton& targetSkeleton,
                                                      float sampleRate, float scale)
    {
        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_left_handed_y_up;
        opts.target_unit_meters = 1.0f;

        ufbx_error error{};
        ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
        if (scene == nullptr) return { false, ToStd(error.description), {} };

        // Bone lookup by name only - this file has no skin deformer to walk,
        // just the bone node hierarchy the animation curves are attached to.
        std::vector<const ufbx_node*> boneNode(targetSkeleton.bones.size(), nullptr);
        for (size_t b = 0; b < targetSkeleton.bones.size(); ++b)
            boneNode[b] = ufbx_find_node(scene, targetSkeleton.bones[b].name.c_str());

        AnimationImportResult result;
        result.ok = true;
        for (size_t si = 0; si < scene->anim_stacks.count; ++si)
        {
            AnimationClip clip = BakeClip(scene->anim_stacks.data[si], boneNode, targetSkeleton, sampleRate, scale);
            if (!clip.tracks.empty()) result.clips.push_back(std::move(clip));
        }
        ufbx_free_scene(scene);

        if (result.clips.empty())
        {
            result.ok = false;
            result.error = "no animation stack in '" + path + "' matched any bone of the target skeleton";
        }
        return result;
    }
}
