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

### 5.5 텍스처 베이킹 애니메이션 — 연구 (미구현)

§5.2~5.3 은 인스턴스마다 CPU `AnimationSampler::Evaluate` + 본 팔레트(cbuffer/StructuredBuffer) 업로드를 전제로 한다. **인스턴스 수가 많아지면**(군중, 배경 캐릭터, 다수 소환수) 이 CPU 평가·업로드 자체가 병목이 된다. 클립을 오프라인/로드 시점에 **텍스처로 구워두면** 런타임은 텍스처 샘플 한 번으로 포즈를 얻는다 — 라이선스·포맷 조사가 필요한 §4(Live2D/Spine)와 달리 순수 엔진 내부 기법이라 조사 없이 바로 설계 가능.

두 갈래가 있고 이 엔진에 걸리는 대상이 다르다.

| | 본 행렬 텍스처 (Bone Matrix Texture) | 버텍스 애니메이션 텍스처 (VAT, Vertex Animation Texture) |
|---|---|---|
| 굽는 것 | 프레임별 **본 팔레트 행렬**(`AnimationSampler::Evaluate` 결과 그대로) | 프레임별 **최종 정점 위치(+법선)** |
| 런타임 계산 | 버텍스 셰이더에서 기존 LBS 그대로 수행, 팔레트만 cbuffer 대신 텍스처 `Load` | 없음 — 텍스처 값이 곧 최종 정점 위치. 스키닝 수식 자체가 셰이더에서 사라짐 |
| 텍스처 크기 | (본 수) × (프레임 수) × 4텍셀(4×4 행렬 = `float4` 4개) — 본 수(수십~수백)에만 비례 | (정점 수) × (프레임 수) × 1~2텍셀 — **정점 수**에 비례, 고밀도 메시엔 큼 |
| 비강체 변형(천/근육/시뮬레이션) | LBS 한계 그대로(본 기반만) | 어떤 변형이든 구울 수 있음(모프 타겟, 오프라인 시뮬레이션 결과도) |
| 적합한 대상 | 본 수 적은 캐릭터가 **인스턴스 많을 때**(군중) | 캐릭터든 식생이든 **정점 수 적당 + 인스턴스 아주 많을 때**, 또는 스키닝으로 못 만드는 변형 |
| 이 엔진에서의 우선순위 | 3e(캐릭터 소수, 수십 본)엔 §5.2 로 충분 — 군중 요구가 생기면 검토 | 배경/군중 전용, 지금 로드맵엔 없음(§4.4 아트 파이프라인 결정과 별개로 "필요해지면") |

**데이터 경로(둘 다 공통 원칙)**: 굽기는 `anim::AnimationSampler::Evaluate` 를 프레임 수만큼 반복 호출하는 CPU 작업 — 매 프레임이 아니라 **모델 로드 시 1회**(또는 오프라인 툴, `tools/fbx_probe.cpp` 처럼)이므로 고정 스텝·렌더 스레드 규칙과 충돌 없다. 텍스처 실제 생성(`ID3D11Device::CreateTexture2D`)은 불변 규칙 1·2에 따라 **렌더 스레드만** 한다 — 구운 픽셀 데이터(값 배열)만 넘기고, 리소스 생성은 그쪽에서.

- **레이아웃(본 행렬 텍스처)**: `DXGI_FORMAT_R32G32B32A32_FLOAT`, 폭 = 본 수 × 4(행 4개를 `float4` 4장으로), 높이 = 프레임 수. 버텍스 셰이더가 `boneIndex`·`frameIndex` 로 4개 텍셀을 `Load` 해 4×4 행렬 재구성 후 기존 LBS(§5.2 HLSL)에 그대로 대입.
- **레이아웃(VAT)**: 폭 = 정점 수, 높이 = 프레임 수, `DXGI_FORMAT_R16G16B16A16_FLOAT`(위치) [+ 법선용 두 번째 텍스처]. 버텍스 셰이더가 `SV_VertexID`·`frameIndex` 로 `Load`, 스키닝 없이 바로 world 변환.
- **프레임 보간**: 포인트 필터로 정수 프레임만 쓰거나(스텝 애니메이션, 저비용), 두 프레임을 `Load` 후 셰이더에서 `lerp`(부드럽지만 텍셀 2배 페치).
- **정밀도**: 로컬(모델) 공간 좌표로 구워야(월드는 인스턴스 트랜스폼으로 별도 적용) 좌표 범위가 작아 16비트 float 오차가 적다 — LBS도 이미 모델 공간 계산이라 같은 가정.
- **인스턴싱과의 결합**: 인스턴스당 스냅샷에 실을 값이 `vector<Mat4> bonePalette`(수백 본이면 KB 단위, §5.2)에서 **`float animTime` 하나**로 줄어든다 — 인스턴스가 많을수록 이 차이가 커진다. `render/r3d/Scene3D.h` 에 값 타입 스케치:
  ```cpp
  struct TexturedAnimDraw {
      MeshHandle mesh;
      TextureHandle animTexture;   // 본 행렬 텍스처 또는 VAT, 모델 로드 시 굽고 등록
      math::Mat4 world;
      float animTime;              // 프레임 인덱스로 변환은 셰이더/등록 시 sampleRate로
      math::Color tint;
  };
  ```
- **판단할 것(실사용 시점)**: 본 행렬 텍스처 vs VAT는 양자택일이 아니라 대상이 다름(캐릭터 군중 vs 정점 단위 변형) — 실제로 군중이 필요해질 때 결정. 로드 시점 굽기(단순, 시작 시간 ↑) vs 오프라인 툴 산출물 커밋(복잡, 시작 시간 절약) 도 그때 판단.

## 6. 현재 상태 / 검증 필요

| | 상태 |
|---|---|
| ufbx vendor + 빌드 | ✅ `ufbx.c` 를 C++ 로 컴파일, 경고 0 |
| `import::Model` 타입 | ✅ |
| `ModelImporter` — 메시/스킨/스켈레톤 | ✅ **실제 FBX 검증됨** (Unity-chan: 23 mesh / 48k vert / 140 bone / 15 skinned. 가중치 합=1 위반 0, 본 index 범위 초과 0, 스켈레톤 위상정렬 OK). `tools/fbx_probe.cpp` 로 확인 |
| `ModelImporter` — 머티리얼 | ✅ 이름·컬러 추출. `pbr.base_color` 없으면 `fbx.diffuse_color` 폴백 |
| `ModelImporter` — 애니메이션 bake | ✅ 컴파일. **애니메이션 있는 FBX 로 미검증** (Unity-chan 모델 FBX 엔 클립 없음 — 별도 파일) |
| `AnimationSampler` (CPU 포즈 평가, loop) | ✅ 컴파일. 실데이터 미검증, 블렌딩 미구현 |
| `ModelMeshPass3D` — **정적** 렌더 (바인드 포즈, 스키닝 없음) + **텍스처** | ✅ FBX 를 시작 시 로드해 immutable VB/IB 생성. `import::LoadTga`(uncompressed TGA 24/32bpp) 로 디퓨즈 텍스처 로드 → SRV + linear-wrap 샘플러, 셰이더에서 샘플(V flip + alpha cutout). 머티리얼→파일은 FBX ref basename `.tga` 우선, 없으면 이름 테이블(Unity-chan: body→body_01.tga 등). `main.cpp` 에서 `AddRenderPass`. 스키닝은 다음 |
| `SkinnedMeshPass3D` (GPU 스키닝) | ❌ 설계만 (§5.2) — `ModelMeshPass3D` 에 본 팔레트 + 스킨 셰이더 추가하는 형태 |
| 상태 머신 / 블렌딩 / 루트 모션 | ❌ 설계만 (§5.3) |
| 텍스처 베이킹 애니메이션 (본 행렬 텍스처 / VAT) | ❌ 연구·설계만 (§5.5) — 군중/다수 인스턴스용, 캐릭터 소수면 §5.2 로 충분 |

관찰: Unity-chan FBX 는 단위가 **cm** (키 ≈156 유닛). `ImportOptions::scale = 0.01` 로 미터화. 텍스처 경로는 원본 `.psd` 참조 (stale) — 실제 `.tga` 는 FBX 옆에. 에셋 경로 해석기는 별도 과제.

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

### 텍스처 베이킹 애니메이션 사용 (구현 후, 인스턴스가 많을 때만)

```cpp
// 모델 로드 직후, 1회 (매 프레임 아님)
std::vector<float> bakedPixels;
for (int frame = 0; frame < frameCount; ++frame) {
    sampler.Evaluate(model.skeleton, clip, frame / sampleRate, skinMatrices);
    AppendToBoneMatrixTexturePixels(skinMatrices, bakedPixels);  // 본 행렬 텍스처 레이아웃(§5.5)
}
// bakedPixels(값 배열)를 렌더 스레드로 넘겨 CreateTexture2D — 렌더 스레드만 D3D11 호출

// 매 프레임 스냅샷 (인스턴스당 float 하나만)
scene3d.texturedAnimDraws.push_back({ meshHandle, animTextureHandle, worldMatrix, m_animTime, tint });
```

### 하지 말 것

- 렌더/잡 스레드에서 `ModelImporter`·`AnimationSampler` 호출 (메인/시뮬 스레드 전용, 값만 스냅샷으로).
- ufbx 타입(`ufbx_*`)을 `import` 밖으로 노출 — `ModelImporter.cpp` 안에 가둔다.
- `Step()` 밖에서 애니메이션 시간 전진 (고정 timestep 규칙).
- 매 프레임 `LoadModelFromFile` (로드는 1회, 결과 `Model` 보관).
- 검증 전 스킨드 패스에 back-face culling 켜기 (winding 미확인).
- 인스턴스가 적을 때(수십 이하) 텍스처 베이킹부터 만들지 않는다 — §5.2 cbuffer/StructuredBuffer 팔레트로 충분하고, 텍스처 베이킹은 군중 규모에서만 이득이 실측된다.
- 렌더 스레드가 아닌 곳에서 베이킹 텍스처 `CreateTexture2D` 호출(불변 규칙 1·2 — 굽는 CPU 계산과 텍스처 리소스 생성을 분리해서 지킨다).
