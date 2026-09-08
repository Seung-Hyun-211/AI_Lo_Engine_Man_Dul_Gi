# 모델 로딩 · 스켈레탈 애니메이션 연구 (Model & Animation)

FBX 모델을 읽어 스킨드 메시를 애니메이션하는 경로의 조사 + 설계. 라이브러리는 **ufbx**(`src/vendor/ufbx`, MIT/Public Domain, v0.23.0, `ufbx.h` + `ufbx.c` 2파일, 외부 의존 0)를 vendor 했다.

관련 코드: `src/import/`(FBX → 엔진 `Model`), `src/anim/`(포즈 평가), `src/math/Math3D.h`(`Quat`/`Slerp`/`ComposeTRS`).

---

## 1. FBX 로딩 라이브러리 비교 (조사 결과)

| | ufbx (**채택**) | Assimp | OpenFBX | FBX SDK |
|---|---|---|---|---|
| 라이선스 | MIT / Public Domain | BSD-3 | MIT | 독점(무료, 재배포 제약) |
| 포함 방식 | 소스 2파일 vendor | vcpkg / CMake 빌드 | 소스 2파일 | ~100MB SDK, DLL |
| 포맷 | FBX 전용 | FBX + 40여 종 | FBX 전용 | FBX 전용, 레퍼런스 |
| 스키닝/애니메이션 | ✅ 전부 | ✅ | 얕음 | ✅ |
| 빌드 영향 | 없음(단일 vcxproj 유지) | 외부 빌드 추가 | 없음 | 큼 |
| 안정성 | 높음, 활발 | 높음, FBX는 리버스 엔지니어링 | 중간 | 공식 |

단일 vcxproj·외부 의존 0 정책([engine-overview](engine-overview.md))에 ufbx가 가장 맞다. glTF 로도 갈 거면 `cgltf`(역시 단일 헤더)를 나중에 나란히 두면 된다 — 런타임 포맷은 업계가 glTF 로 이동 중, FBX 는 저작 포맷.

## 2. 스켈레탈 애니메이션 기초

용어:
- **스켈레톤**: 계층 구조의 본(joint) 배열. 각 본은 부모 기준 로컬 변환(TRS)을 가진다.
- **바인드 포즈**: 메시가 스킨될 때의 기준 포즈.
- **inverse bind matrix** `B_i⁻¹`: 바인드 포즈에서 본 i 로컬 공간 → 모델 공간의 역행렬. ufbx 의 `skin_cluster.geometry_to_bone`.
- **스킨 매트릭스** `S_i = B_i⁻¹ · M_i(pose)` — `M_i` 는 현재 포즈에서 본 i 의 모델 공간 변환.
- **스키닝(LBS, linear blend skinning)**: 정점 `v` 를 최대 4개 본이 가중치로 끌어당김.
  `v' = Σ_i w_i · (v · S_i)`  (행벡터 규약, `Σ w_i = 1`)

행벡터 규약(`p' = p·M`, 좌수좌표 LH)은 이 엔진의 `math::Mat4` 규약과 같다.

## 3. 데이터 경로: FBX → 엔진 `Model`

`import::LoadModelFromFile(path)` → `import::Model`:

| 엔진 타입 | 내용 | ufbx 소스 |
|---|---|---|
| `ModelMesh` | position/normal/uv + `boneIndices[4]`/`boneWeights[4]`, 삼각형 인덱스, 머티리얼 인덱스 | `ufbx_mesh` 삼각형화, `vertex_position/normal/uv`, `skin_deformers[0].vertices/weights` |
| `ModelMaterial` | name, baseColor, diffuseTexture(파일명) | `ufbx_material.pbr.base_color` |
| `Skeleton` (`Bone[]`) | name, parent index, `inverseBind`, 바인드 로컬 TRS. 부모 index < 자식 index 로 정렬 | skin cluster 의 `bone_node` 집합, `geometry_to_bone`, `node.local_transform` |
| `AnimationClip` (`BoneTrack[]`) | 본별 키프레임(time, T/R/S) | `anim_stacks[i]` 를 `sampleRate`(기본 30fps)로 `ufbx_evaluate_transform` 샘플링(bake) |

**bake(샘플링) 방식을 택한 이유**: FBX 애니메이션 커브는 pre/post-rotation, 회전 순서, 커브 타입이 복잡하다. ufbx 의 `ufbx_evaluate_transform` 이 그걸 다 풀어 최종 TRS 를 준다. 고정 레이트로 구워두면 런타임 보간이 단순(lerp + slerp)해지고 결정적이다. 대가: 파일 크기 ↑, 원본 커브의 곡률 손실(30fps 면 대부분 무시 가능).

## 4. 포즈 평가 — 구현됨 (`anim::AnimationSampler`)

```cpp
anim::AnimationSampler sampler;
std::vector<math::Mat4> skin;      // 본 개수만큼
sampler.Evaluate(model.skeleton, model.animations[0], timeSeconds, skin);
// skin[b] = inverseBind[b] · modelMatrix[b]  (업로드 준비 완료)
```

- 트랙에서 `timeSeconds` 전후 키 2개를 찾아 T/S 는 lerp, R 은 `math::Slerp`.
- 클립이 건드리지 않는 본은 바인드 포즈 유지.
- `modelMatrix[b] = localMatrix[b] · modelMatrix[parent]` (본이 부모 우선 정렬돼 한 패스).
- `timeSeconds` 는 `clip.duration` 으로 loop.
- CPU. 정점 스키닝은 GPU 로(§5).

## 5. 런타임 스키닝 파이프라인 — 설계 (미구현)

### 5.1 CPU vs GPU 스키닝

| | CPU 스키닝 | GPU 스키닝 (**권장**) |
|---|---|---|
| 정점 변환 | 매 프레임 CPU 에서 `v'` 계산 후 동적 VB 업로드 | 정적 VB(바인드 포즈) + 본 매트릭스만 업로드, 셰이더에서 |
| 비용 | 정점 수에 비례, 대역폭 큼 | 본 개수(수십~수백)만 업로드 |
| 인스턴싱 | 어려움 | 본 팔레트만 바꿔 여러 인스턴스 |
| 결론 | 프로토타입/소량에만 | 기본으로 채택 |

### 5.2 새 렌더 패스: `SkinnedMeshPass3D` (`render/r3d/`)

`IRenderPass` 구현 하나 추가. 기존 `MeshPass3D` 는 손 안 댐(OCP). 등록: `main.cpp` 의 `renderer.AddRenderPass(std::make_unique<SkinnedMeshPass3D>())`.

- **정점 포맷**: `float3 pos; float3 normal; float2 uv; uint4 boneIndices; float4 boneWeights;` (`ModelVertex` 그대로).
- **본 팔레트 업로드**: 본 ≤ 64 면 `cbuffer { row_major float4x4 bones[64]; }`; 그 이상이면 `StructuredBuffer<float4x4>` + SRV(t0). 스냅샷이 인스턴스별 `std::vector<Mat4>` 를 실어 보냄.
- **HLSL(개념)**:
  ```hlsl
  float4x4 skin =
      bones[i.boneIndices.x] * i.boneWeights.x +
      bones[i.boneIndices.y] * i.boneWeights.y +
      bones[i.boneIndices.z] * i.boneWeights.z +
      bones[i.boneIndices.w] * i.boneWeights.w;
  float4 worldPos = mul(mul(float4(i.pos,1), skin), world);
  o.pos = mul(worldPos, viewProj);
  o.nrm = mul(float4(i.normal,0), skin).xyz;   // 비균등 스케일 없으면 근사 OK
  ```
- **스냅샷 확장**: `render/r3d/Scene3D.h` 에 값 타입 추가.
  ```cpp
  struct SkinnedDraw {
      MeshHandle mesh;            // GPU 에 올라간 스킨드 메시
      math::Mat4 world;
      std::vector<math::Mat4> bonePalette;   // AnimationSampler 결과
      math::Color tint;
  };
  std::vector<SkinnedDraw> skinnedDraws;   // Scene3D 멤버
  ```
  (본 팔레트를 매 프레임 스냅샷에 복사 = 스레드 경계 값 규약 유지. 본 수백 개 × 인스턴스 수십이면 KB~수십 KB, 허용.)
- **메시 등록**: `MeshPass3D` 의 내장 큐브/평면처럼, `SkinnedMeshPass3D` 가 `import::Model` 을 받아 immutable VB/IB 생성하고 `MeshHandle` 발급하는 `RegisterModel()` 을 갖는다. 로드맵의 "메시 레지스트리"와 합류.

### 5.3 애니메이션 상태 — 설계 (미구현)

- **크로스페이드 블렌딩**: 두 클립을 각각 평가해 본별 `math::Slerp`(회전) + lerp(T/S), 가중치 `blend∈[0,1]`. `AnimationSampler` 에 `EvaluateBlend(skelA, clipA, tA, clipB, tB, blend, out)` 추가.
- **애디티브**: `delta = clip_additive(t) * bind⁻¹` 를 베이스 포즈에 곱. 상체 조준 위 걷기 등.
- **본 마스크**: 본별 가중치 배열로 하반신=locomotion, 상반신=action.
- **상태 머신**: `AnimatorController { state, transitions, params }` — `Idle→Run` 등, 전이마다 크로스페이드 시간. `game/` 레이어에 두고 매 고정 스텝 tick, 결과 `bonePalette` 를 스냅샷에.
- **루트 모션**: 루트 본의 XZ 변위를 뽑아 캐릭터 트랜스폼에 적용, 포즈에서는 제거.

### 5.4 더 뒤 (측정 후)

- 본 매트릭스 팔레트를 `JobSystem::ParallelFor` 로 캐릭터 단위 병렬 평가.
- IK(발 정합, 조준) — 2본 아날리틱 IK부터.
- 애니메이션 LOD: 먼 캐릭터는 sampleRate/본 수 축소, 아주 멀면 스키닝 생략.
- 듀얼 쿼터니언 스키닝(팔꿈치 캔디랩 방지) — ufbx 가 `dq_weight` 제공.

## 6. 현재 상태 / 검증 필요

| | 상태 |
|---|---|
| ufbx vendor + 빌드 | ✅ `ufbx.c` 를 C++ 로 컴파일, 경고 0 |
| `import::Model` 타입 | ✅ |
| `ModelImporter` (메시/머티리얼/스켈레톤/애니메이션 bake) | ✅ 컴파일. **실제 FBX 로 런타임 미검증** (샘플 에셋 없음) |
| `AnimationSampler` (CPU 포즈 평가, loop) | ✅ 컴파일. 블렌딩 미구현 |
| `SkinnedMeshPass3D` (GPU 스키닝) | ❌ 설계만 (§5.2) |
| 상태 머신 / 블렌딩 / 루트 모션 | ❌ 설계만 (§5.3) |

**검증 시 확인할 것** (첫 실제 FBX 로드 때):
- 삼각형 winding — `MeshPass3D` 는 현재 `CULL_NONE`, 스킨드 패스도 처음엔 그렇게.
- `target_axes = left_handed_y_up` 로 좌표계 통일했으나 모델 스케일/업축은 파일마다 다름 → `ImportOptions::scale`.
- `geometry_to_bone` 가 메시 노드 트랜스폼을 이미 포함하는지 (다중 메시 스킨이면 mesh 별 `geometry_to_world` 보정 필요할 수 있음).
- 본 4개 초과 정점의 가중치 절단 후 재정규화 확인.
- `ufbx_real` 은 double — importer 에서 float 캐스트 완료.

## 7. 사용 방법 (How to use)

### 모델 로드

```cpp
#include "import/ModelImporter.h"

engine::import::ImportOptions opts;
opts.animationSampleRate = 30.0f;
opts.scale = 1.0f;                      // 파일 단위가 cm 면 0.01 등

auto result = engine::import::LoadModelFromFile("assets/character.fbx", opts);
if (!result.ok) { /* result.error 로그 */ return; }
engine::import::Model& model = result.model;
// model.meshes / model.materials / model.skeleton / model.animations
```

메모리에서: `LoadModelFromMemory(data, size, opts)` (IO 스레드가 파일을 읽어와 넘길 때).

### 포즈 평가 (현재 가능한 "사용")

```cpp
#include "anim/AnimationSampler.h"

engine::anim::AnimationSampler sampler;                 // 캐릭터당 하나, 스크래치 재사용
std::vector<engine::math::Mat4> skinMatrices;

// 매 고정 스텝 (Simulation::Step 안, time-design.md 규칙)
m_animTime += fixedDelta;
sampler.Evaluate(model.skeleton, model.animations[m_clip], m_animTime, skinMatrices);
// skinMatrices 를 SnapshotBuilder 가 SkinnedDraw.bonePalette 로 복사 (구현되면)

// 애니메이션 없이 바인드 포즈만:
sampler.EvaluateBindPose(model.skeleton, skinMatrices);
```

### 그리기 (SkinnedMeshPass3D 구현 후)

1. `render/r3d/Scene3D.h` 에 `SkinnedDraw` + `Scene3D::skinnedDraws` 추가.
2. `SkinnedMeshPass3D` (`IRenderPass`) 작성 → `main.cpp` 에서 `renderer.AddRenderPass(...)`.
3. 시작 시 `pass->RegisterModel(model)` → `MeshHandle`.
4. `SnapshotBuilder` 가 `Scene3D::skinnedDraws.push_back({ handle, worldMatrix, skinMatrices, tint })`.

### 하지 말 것

- 렌더/잡 스레드에서 `ModelImporter`·`AnimationSampler` 호출 (메인/시뮬 스레드 전용, 값만 스냅샷으로).
- ufbx 타입(`ufbx_*`)을 `import` 밖으로 노출 — `ModelImporter.cpp` 안에 가둔다.
- `Step()` 밖에서 애니메이션 시간 전진 (고정 timestep 규칙).
- 매 프레임 `LoadModelFromFile` (로드는 1회, 결과 `Model` 보관).
- 검증 전 스킨드 패스에 back-face culling 켜기 (winding 미확인).
