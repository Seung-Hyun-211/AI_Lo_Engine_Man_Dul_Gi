# 대규모 인스턴스 렌더 — 수천 개체 출력 선행작업

**상태: §8 의 1~2 + 4(전반) 구현됨, 3·4(SoA)·5·6 미구현.**
데모 씬 2([demo-scene.md](demo-scene.md))의 `SimAgent` 군중(현재 600 / capacity 1024)이
배치 하나 = `DrawIndexedInstanced` 1콜로 그려지고, `SnapshotBuilder` 가 프러스텀·최대거리로
컬하며, 군중은 `core::ObjectPool<SimAgent>`(`src/core/ObjectPool.h`) 에서 산다 —
`ParallelFor` 는 그 `ActiveIndices()` 를 쪼개 돌고, 스텝마다 1마리씩 풀을 재활용(churn).
남은 것: LOD 버킷/빌보드(§8-3), SoA 승격(`game/AgentStore`, 측정 게이트), 브로드페이즈(§8-5).
이 문서는 그 벽을 넘기 위한 **엔진 일반 선행작업** 전체를 설계한다 — 정적/강체 인스턴스를 한
번의 `DrawIndexedInstanced` 로 그리는 경로, 스냅샷 값 타입, 컬링·LOD, 심(sim) 쪽 SoA + 풀.

**범위 경계**: 이 문서는 **강체 인스턴스**(트랜스폼만 다른 같은 메시)까지다. 인스턴스마다
**다른 애니메이션**이 필요한 스킨드 군중(좀비 등)은 그 위에 VAT 를 얹는
[horde-design.md](horde-design.md) §5 의 몫이다. 이 문서가 그 아래 레이어 —
`HordePass3D` 도 결국 여기서 정의하는 인스턴스 버퍼·컬링·LOD 골격을 그대로 쓴다.

관련: [demo-scene.md](demo-scene.md)(데모 씬 2 = 이 경로의 첫 소비자),
[horde-design.md](horde-design.md)(애니메이션 확장 + 플로우필드),
[roadmap.md](roadmap.md) D2·D3·D4, [entity-lifecycle-design.md](entity-lifecycle-design.md) §3A,
[scrollable-list-and-pool.md](scrollable-list-and-pool.md) §1.1(오브젝트 풀),
[multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md),
[command-playbook.md](command-playbook.md) 3b(새/확장 패스)·2c(브로드페이즈).

---

## 1. 현재 병목 (측정 없이도 확실한 것)

| 지점 | 현행 | 수천에 필요한 것 |
|---|---|---|
| **드로우 콜** | `MeshPass3D::Execute` 가 `scene.meshDraws` 를 순회하며 개체마다 `UpdateSubresource(Object cbuffer)` + `IASetVertexBuffers` + `DrawIndexed`. N개 = N콜 + N번 cbuffer 갱신 | 메시·LOD 가 같은 인스턴스를 모아 **배치당 `DrawIndexedInstanced` 1콜**. cbuffer 갱신은 프레임당 1회(Frame b0) |
| **스냅샷 빌드** | ~~`for (SimAgent&) meshDraws.push_back(...)` — 컬링 없음~~ ✅ 프러스텀 + 최대거리 컬 후 `MeshInstance` 로 | (남음) LOD 분할, 메시 여러 종류 시 `(mesh,lod)` 버킷 |
| **스냅샷 크기** | `MeshDraw` = `Mat4`(64B) + color(16B) = 80B. 5,000개 = 400KB/프레임 | 컴팩트 인스턴스(pos+yaw+scale+colorRgba = **24B**). 5,000개 = 120KB — 1슬롯 메일박스 허용 |
| **심 업데이트** | ~~`std::vector<SimAgent>` 고정, 풀 없음~~ ✅ `core::ObjectPool<SimAgent>`(capacity 1024) — `ParallelFor` 는 `ActiveIndices()` 슬라이스, 스폰/디스폰은 스텝 밖 | (남음) 규모 커지면 SoA 승격(§6.2) |
| **충돌** | `CollisionWorld3D` N² · 메인 전용 | 유니폼 그리드 브로드페이즈(2c). 개체끼리는 분리력, 콜라이더는 지형/플레이어만 |
| **셰이더** | `mesh.hlsl` VS 가 `world`(Object cbuffer)를 읽음 | 인스턴스 변형: 트랜스폼·색을 **per-instance 정점 스트림**(slot 1)에서 |

핵심: **개체마다 `MeshDraw`/`ModelDraw` 를 push 하는 패턴을 버린다.** 대신 스냅샷에
`{인스턴스 배열 + 배치 목록}` 을 싣고, 패스는 배치마다 한 콜.

---

## 2. 아키텍처 개요 (목표 — `[now]` 표시가 현재 구현)

```text
[메인 스레드 / 고정 timestep — Simulation::Step]
  개체 저장소 (game/)
    [now] core::ObjectPool<SimAgent> (AoS) 를 Simulation 이 직접 소유. AgentStore(SoA)는 §6.2.
    ├─ Spawn/Despawn               스텝 사이에만 (불변 규칙 6). [now] 12스텝마다 1마리 churn
    └─ JobSystem::ParallelFor      ActiveIndices() 를 [begin,end) 로 쪼개 이동/거동 (StepSimAgents)

[SnapshotBuilder::Build — 메인 스레드, Step 이후]
  카메라 viewProj → 6 프러스텀 평면 (MakeFrustum, SnapshotBuilder 익명)
  for each agent:  프러스텀·최대거리 컬 → [목표] LOD 버킷(거리) → MeshInstance push
                   [now] LOD 없음 — 컬 통과분을 배치 1개로
  scene.meshInstances + scene.instanceBatches
                                   (배치 = 연속 구간 [first, first+count), meshId, lod)

[스레드 경계]  RenderSnapshot (값) — scene3d.meshInstances[], scene3d.instanceBatches[]

[렌더 스레드 — MeshPass3D::Execute / RenderShadow]
  Frame cbuffer(b0) 1회 갱신
  meshInstances 를 DYNAMIC 인스턴스 VB 에 Map(WRITE_DISCARD) 한 번에 업로드
  for each batch:
     IASetVertexBuffers(slot0 = 메시 VB, slot1 = 인스턴스 VB @ first*stride)
     DrawIndexedInstanced(mesh.indexCount, batch.count, 0, 0, 0)
```

스레드 경계를 넘는 건 여전히 값 스냅샷 하나. 인스턴스는 `std::vector<MeshInstance>`(작은 POD)
+ `std::vector<InstanceBatch>` 로만 건너간다 — 가변 게임 객체 포인터 없음.

---

## 3. 파트 A — 스냅샷 값 타입 (`render/r3d/Scene3D.h`)

```cpp
namespace engine::render
{
    // 강체 인스턴스 하나. per-instance 정점 스트림으로도 그대로 올라간다.
    // 트랜스폼은 압축형(pos + yaw + 균등 scale) — 군중은 회전이 Y축뿐이고
    // 비균등 스케일이 필요 없다. 필요해지면 §10 참고(4x3 행렬로 확장).
    struct MeshInstance
    {
        math::Vec3    pos;           // 월드                (offset 0)
        float         yaw;           // 라디안 (Y축 회전)   (offset 12)
        float         scale;         // 균등                (offset 16)
        std::uint32_t colorRgba;     // 8:8:8:8 packed (VS 에서 언팩)  (offset 20)
    };                               // 24 bytes, 패딩 없음 (입력 레이아웃 오프셋이 이걸 가정)

    // 같은 메시·같은 LOD 인 MeshInstance 들의 연속 구간. 배치 하나 = 드로우 콜 하나.
    struct InstanceBatch
    {
        MeshId        mesh{ MeshId::Cube };
        std::uint32_t first{ 0 };    // scene.meshInstances 안 시작 인덱스
        std::uint32_t count{ 0 };
        std::uint16_t lod{ 0 };      // 0 근 / 1 중 / 2 원(빌보드) — 셰이더/메시 선택에 쓸 수 있음
    };

    struct Scene3D
    {
        // ...기존 (camera, lighting, meshDraws, modelDraws, debugLines)...
        std::vector<MeshInstance> meshInstances;    // 모든 배치의 인스턴스가 여기 연속으로
        std::vector<InstanceBatch> instanceBatches; // 그 구간들
    };
}
```

- **`meshDraws` 는 남긴다.** 지형·프롭처럼 수십 개짜리 유니크 오브젝트는 기존 경로가 더 단순하다
  (OCP — 기존 타입 안 건드림). 인스턴스 경로는 "같은 메시 수백+" 전용.
- **크기 예산**: `MeshInstance` 24B. 8,192개 ≈ 192KB/프레임. `InstanceBatch` 16B × (메시종류 ×
  LOD단계) — 보통 10개 미만. 1슬롯 메일박스가 오래된 프레임을 버려도 무해(규칙 4).
- `colorRgba` 패킹은 선택 — 색이 인스턴스마다 다를 때만 의미. 배치 전체가 같은 색이면
  `InstanceBatch` 에 색 하나 두고 `MeshInstance` 를 20B 로 줄여도 된다(§10).

---

## 4. 파트 B — 렌더: `MeshPass3D` 인스턴스 경로

**결정: 기존 `MeshPass3D` 를 확장한다**(새 `InstancedMeshPass3D` 를 만들지 않는다). 이유:
같은 메시 레지스트리(`m_meshes`)·같은 Frame cbuffer·같은 셰도우 경로를 재사용하고, `Execute`
안에서 "먼저 `meshDraws` 유니크 루프, 그 다음 `instanceBatches` 인스턴스 루프" 두 구간이면
충분하다. 별 패스는 메시 GPU 버퍼를 중복 소유하게 된다. (재검토 지점은 §10.)

### 4.1 인스턴스 입력 레이아웃 (slot 1, PER_INSTANCE_DATA)

```cpp
// MeshPass3D::Initialize — "mesh_instanced" 셰이더용 레이아웃
const D3D11_INPUT_ELEMENT_DESC instanced[] = {
    // slot 0 — 메시 정점 (기존)
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0 },
    // slot 1 — per-instance (MeshInstance 와 바이트 일치)
    { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // pos
    { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // yaw
    { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // scale
    { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // colorRgba
};
```

- `InstanceDataStepRate = 1`. slot 1 stride = `sizeof(MeshInstance)` = 24. `MeshPass3D` 는
  `static_assert(sizeof(MeshInstance) == 24)` 로 이 오프셋 가정을 고정한다.
- 셰이더는 `assets/shaders/mesh_instanced.hlsl` 로 **별 파일** — 인라인 `D3DCompile` 금지,
  `shaders.Get(device, "mesh_instanced", instanced, ARRAYSIZE(instanced))` ([shader-pipeline.md](shader-pipeline.md)).
  (또는 `ShaderLibrary` 에 매크로 순열이 생기면 `mesh.hlsl` + `#define INSTANCED` 하나로. 지금은
  별 파일이 더 단순.)

### 4.2 DYNAMIC 인스턴스 버퍼

```cpp
// Initialize: 한 번 생성. kMaxInstances 는 스냅샷 캡과 일치 (예: 16384).
D3D11_BUFFER_DESC ib{};
ib.ByteWidth      = sizeof(MeshInstance) * kMaxInstances;
ib.Usage          = D3D11_USAGE_DYNAMIC;
ib.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
ib.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
device->CreateBuffer(&ib, nullptr, &m_instanceBuffer);
```

### 4.3 Execute (인스턴스 구간)

```cpp
// ... 기존 meshDraws 유니크 루프 이후 ...
if (!scene.instanceBatches.empty() && m_instShader != nullptr)
{
    const auto& src = scene.meshInstances;
    const UINT n = (UINT)std::min(src.size(), (size_t)kMaxInstances);

    D3D11_MAPPED_SUBRESOURCE m{};
    device->Map(m_instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    std::memcpy(m.pData, src.data(), n * sizeof(MeshInstance));
    device->Unmap(m_instanceBuffer, 0);

    device->IASetInputLayout(m_instShader->inputLayout);
    device->VSSetShader(m_instShader->vs, nullptr, 0);
    device->PSSetShader(m_instShader->ps, nullptr, 0);
    device->VSSetConstantBuffers(0, 1, &m_frameConstants);   // b0 만. Object b1 안 씀
    device->PSSetConstantBuffers(0, 1, &m_frameConstants);

    for (const InstanceBatch& b : scene.instanceBatches)
    {
        if (b.first >= n) continue;
        const UINT count = std::min(b.count, n - b.first);
        const GpuMesh& mesh = m_meshes[(size_t)b.mesh];
        if (mesh.vertexBuffer == nullptr || count == 0) continue;

        ID3D11Buffer* vbs[2]   = { mesh.vertexBuffer, m_instanceBuffer };
        const UINT     strd[2] = { sizeof(MeshVertex), sizeof(MeshInstance) };
        const UINT     offs[2] = { 0, b.first * sizeof(MeshInstance) };
        device->IASetVertexBuffers(0, 2, vbs, strd, offs);
        device->IASetIndexBuffer(mesh.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        device->DrawIndexedInstanced(mesh.indexCount, count, 0, 0, 0);
    }
}
```

- Map 한 번, 배치 루프는 바인드만. `offs[1] = b.first*stride` 로 배치별 인스턴스 구간을 가리키고
  `StartInstanceLocation = 0` (offset 을 IA 가 처리). 원하면 offset 0 + `StartInstanceLocation = b.first`.

### 4.4 셰이더 (`assets/shaders/mesh_instanced.hlsl`)

```hlsl
#include "common3d.hlsli"     // Frame cbuffer(b0), ApplyLighting, SampleShadow

struct VSIn {
    float3 pos : POSITION;  float3 nrm : NORMAL;
    float3 ipos : TEXCOORD1; float iyaw : TEXCOORD2; float iscale : TEXCOORD3;
    float4 icol : COLOR0;
};
struct VSOut { float4 pos:SV_POSITION; float3 nrm:NORMAL; float4 shadowClip:TEXCOORD1; float4 col:COLOR0; };

VSOut VSMain(VSIn i)
{
    float s = sin(i.iyaw), c = cos(i.iyaw);
    float3 p = float3( c*i.pos.x + s*i.pos.z, i.pos.y, -s*i.pos.x + c*i.pos.z) * i.iscale + i.ipos;
    float3 n = float3( c*i.nrm.x + s*i.nrm.z, i.nrm.y, -s*i.nrm.x + c*i.nrm.z);
    VSOut o;
    o.pos        = mul(float4(p,1), viewProj);
    o.nrm        = n;
    o.shadowClip = mul(float4(p,1), lightViewProj);
    o.col        = i.icol;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET
{
    float sh = SampleShadow(i.shadowClip);
    return float4(ApplyLighting(i.col.rgb, i.nrm, sh), i.col.a);
}
```

Y축 회전만이라 행렬 없이 sin/cos 2개. 툰 셰이딩(`cel.hlsl`)이 필요하면 `ApplyLighting` 대신
`ApplyCelLighting` 로 바꾸면 그대로 붙는다.

### 4.5 셰도우

`RenderShadow` 에도 같은 인스턴스 구간을 추가한다 — `shadow_instanced.hlsl`(pos-only + slot1
pos/yaw/scale, depth-only, PS 없음). 원거리 LOD(2)는 셰도우에서 생략(`b.lod < 2` 만). 군중
그림자는 개별 정확도가 안 보이므로 LOD0 만 캐스트해도 된다.

---

## 5. 파트 C — 컬링 & LOD (`SnapshotBuilder`)

`BuildScene3D` 가 `Step` 이후 메인 스레드에서 돈다 — 여기서 잘라내는 게 가장 싸다(GPU 로 안 보냄).

### 5.1 프러스텀 추출 — **구현됨** (`SnapshotBuilder.cpp` 익명 네임스페이스)

```cpp
struct Plane { float a, b, c, d; };          // a*x + b*y + c*z + d >= 0 이면 안쪽
struct Frustum { Plane p[6]; };
Frustum   MakeFrustum(const math::Mat4& vp);            // Gribb-Hartmann, 이 엔진 행렬 규약
bool      SphereInFrustum(const Frustum&, math::Vec3 c, float r);
math::Vec3 EyeFromView(const math::Mat4& view);         // LookAtLH view → 카메라 위치
```

- `viewProj = camera.view * camera.projection` (row-major, row-vector). 열 k = `(m[k], m[4+k],
  m[8+k], m[12+k])`; left=col3+col0, right=col3-col0, bottom=col3+col1, top=col3-col1,
  **near=col2**(이 엔진 투영은 clip z ∈ [0,w]), far=col3-col2. 각 평면은 `(a,b,c)` 길이로 정규화.
- 지금은 `math` 가 아니라 `SnapshotBuilder` 익명에 둔다(유일 사용처). **두 번째 사용처가
  생기면 `math/Math3D.h` 로 승격**(순수 값 함수, `ENGINE_WITH_3D`).

### 5.2 컬 + LOD 버킷 — **컬만 구현, LOD 버킷은 미구현 (§8-3)**

현재 `BuildCliffScene` 은 LOD 없이 컬만 한다:

```cpp
const Frustum frustum = MakeFrustum(scene.camera.view * scene.camera.projection);
const math::Vec3 eye = EyeFromView(scene.camera.view);
const auto& pool = simulation.SimAgents();               // core::ObjectPool<SimAgent>
for (std::uint32_t i : pool.ActiveIndices())
{
    const SimAgent& a = pool.Slots()[i];
    const math::Vec3 center = a.pos + math::Vec3{ 0, kAgentScale * 0.5f, 0 };
    if (!SphereInFrustum(frustum, center, kAgentCullRadius)) continue;   // kAgentCullRadius = 0.5
    const math::Vec3 d = center - eye;
    if (math::Dot(d, d) > kAgentCullDist * kAgentCullDist) continue;     // kAgentCullDist   = 90
    scene.meshInstances.push_back({ center, a.heading, kAgentScale, PackRgba(...) });
}
```

**LOD 목표** (§8-3): `d2` 로 `bucket[0..2]` 를 나누고, `bucket` 을 `(meshId, lod)` 순으로
`meshInstances` 에 이어붙여 배치를 여러 개 만든다. `InstanceBatch.lod` 필드와
`MeshPass3D::DrawInstanced` 의 `lod >= 2` 셰도우 컷은 이미 배선돼 있다.

### 5.3 배치 조립 (LOD 시)

```cpp
// 지금은 메시가 Cube 하나뿐 → LOD 별로 배치 3개. 메시 종류가 늘면 (meshId,lod) 로 중첩.
for (int lod = 0; lod < 3; ++lod)
{
    if (bucket[lod].empty()) continue;
    render::InstanceBatch b{};
    b.mesh  = lod < 2 ? render::MeshId::Cube : render::MeshId::Plane;   // 원거리는 빌보드 쿼드
    b.lod   = (std::uint16_t)lod;
    b.first = (std::uint32_t)scene.meshInstances.size();
    b.count = (std::uint32_t)bucket[lod].size();
    scene.meshInstances.insert(scene.meshInstances.end(), bucket[lod].begin(), bucket[lod].end());
    scene.instanceBatches.push_back(b);
}
// 전체 캡: scene.meshInstances.size() 를 kMaxInstances 로 잘라 배치 count 도 보정.
```

- **LOD2 = 빌보드**: `MeshId::Plane` 인스턴스를 카메라 향하게(셰이더에서 view-aligned) + 원거리
  단색/스프라이트. 1차 구현은 그냥 "작은 큐브 그대로, 그림자 생략"으로 시작하고 빌보드는 나중.
- **정렬**: 불투명이라 앞뒤 정렬 불필요. `(mesh,lod)` 로만 그룹. 반투명 인스턴스가 생기면 거리
  내림차순 정렬 배치 추가.
- 튜닝 상수: `kAgentCullDist`, `kAgentCullRadius`, `kAgentScale` 는 `BuildCliffScene` 안
  `constexpr`(구현됨). LOD 경계 `kLodNear`/`kLodMid` 는 §8-3 에서 추가.

---

## 6. 파트 D — 심 쪽 선행 (`game/AgentStore` + `core::ObjectPool<T>`)

렌더가 준비돼도, 수천 개체를 **매 프레임 `new`/`vector` 재할당 없이** 스폰·재사용·순회할
구조가 필요하다. 로드맵 D2.

### 6.1 `core::ObjectPool<T>` — **구현됨** (`src/core/ObjectPool.h`)

```cpp
namespace engine::core
{
    // 슬롯 1회 할당. Acquire 는 T::Reset() 호출(생성자 재실행 X), Release 는 파괴 안 함
    // (버퍼 재사용). 슬롯은 이동하지 않음 → Handle::index 는 객체 수명 내내 유효.
    // generation 으로 stale 핸들 거부. 메인/심 스레드 전용 (규칙 6).
    template <class T>   // T: 기본 생성 가능 + void Reset()
    class ObjectPool : private core::NonCopyable
    {
    public:
        struct Handle { std::uint32_t index, generation; bool Valid() const; };
        ObjectPool() = default;  explicit ObjectPool(std::size_t cap);
        void Init(std::size_t cap);                 // 나중 (재)초기화

        Handle Acquire();                           // free 스택에서, 꽉 차면 무효 핸들
        void   Release(Handle);                     // free 스택으로. stale/무효는 no-op
        bool   IsLive(Handle) const;   T* Get(Handle);

        T* Slots();                                             // cap 개, ActiveIndices() 만 live
        const std::vector<std::uint32_t>& ActiveIndices() const; // 조밀 live 슬롯 인덱스
        std::size_t Size() const;   bool Full() const;
    };
}
```

원안(`std::span<T> Active()` 압축형) 대신 **슬롯 고정 + `ActiveIndices()`** 를 택한 이유·
`ParallelFor` 매핑은 `scrollable-list-and-pool.md` §1.1 "설계 노트".

**사용 (`ParallelFor` + 스냅샷)**:

```cpp
// 스텝 (심 스레드). 스폰/디스폰은 이 밖에서만.
const auto& active = pool.ActiveIndices();
T* slots = pool.Slots();
jobs.ParallelFor(0, active.size(), 32, [slots, &active](size_t b, size_t e){
    for (size_t k = b; k < e; ++k) { T& x = slots[active[k]]; /* x 만 쓰기 */ }
}).Wait();

// 스냅샷 빌드 (심 스레드, 스텝 뒤). 순서 의존 금지.
for (uint32_t i : pool.ActiveIndices()) { const T& x = pool.Slots()[i]; emit(x); }

// 스폰/디스폰: 스텝 사이 메인만. 꽉 찰 수 있으면 Release 먼저.
pool.Release(h);  auto h2 = pool.Acquire();  if (T* x = pool.Get(h2)) reseed(*x);
```

### 6.2 `game/AgentStore` — SoA, 고정 capacity — **아직 안 함 (측정 게이트)**

현재 데모 씬 2 크라우드는 **AoS** 다: `core::ObjectPool<SimAgent>`(§6.1) 에 `SimAgent`
구조체가 그대로 산다. 600마리에선 이게 맞다(§6.2 마지막 불릿). 수천에서 `ParallelFor` 가
pos/vel 만 스트리밍하는 게 병목으로 측정되면 그때 아래 SoA 로 승격한다 — `ObjectPool` 은
그대로 두고 `AgentStore` 가 병렬 벡터 + 자체 free-list 를 갖는다.

```cpp
namespace engine::game
{
    class AgentStore final : private core::NonCopyable
    {
    public:
        static constexpr std::size_t kCapacity = 8192;   // 상한, 재할당 없음
        explicit AgentStore(core::JobSystem& jobs);

        // --- 스텝 사이(메인 스레드)에만 (불변 규칙 6) ---
        int  Spawn(math::Vec3 pos, std::uint16_t kind);   // free-list, 실패 시 -1
        void Despawn(int index);

        // --- 고정 스텝 ---
        void Step(float dt /*, FlowField / target / hits ... */);

        // --- 읽기 (SnapshotBuilder) ---
        [[nodiscard]] int Count() const { return m_count; }
        [[nodiscard]] const std::vector<math::Vec3>& Pos()   const { return m_pos; }
        [[nodiscard]] const std::vector<float>&      Yaw()   const { return m_yaw; }
        [[nodiscard]] const std::vector<float>&      Scale() const { return m_scale; }
        [[nodiscard]] const std::vector<std::uint16_t>& Kind() const { return m_kind; }

    private:
        core::JobSystem& m_jobs;
        int m_count{ 0 };
        std::vector<math::Vec3>       m_pos;     // resize(kCapacity) 생성자에서 1회
        std::vector<math::Vec3>       m_vel;
        std::vector<float>            m_yaw;
        std::vector<float>            m_scale;
        std::vector<std::uint16_t>    m_kind;
        std::vector<int>             m_freeList;
    };
}
```

- **AoS(`std::vector<SimAgent>`) vs SoA(위)**: 수백까지는 AoS 로 충분(현행). 수천에서
  `ParallelFor` 가 pos/vel 만 스트리밍하면 SoA 가 캐시에 유리. **전환 시점은 측정 게이트**
  (§7-4). 그 전까지 데모 씬 2 는 `std::vector<SimAgent>` 유지.
- `EntityRegistry` 안 씀 — 개체가 균일 단일 종류라 SoA 배열이 맞다
  ([entity-lifecycle-design.md](entity-lifecycle-design.md) §3A). 종류는 `m_kind` 정수 한 칸.

### 6.3 스폰/디스폰 + 순회

- **스폰/디스폰은 `Step()` 밖**(메인 스레드). `Spawn` = free-list pop, `Despawn` = free-list
  push. `ParallelFor` 워커 안에서 절대 금지(규칙 6).
- **순회 조밀도**: free-list 방식은 구멍이 생겨 `ParallelFor(0, kCapacity)` 가 죽은 슬롯도
  돈다. 대안 = **swap-remove 압축**(죽은 슬롯을 마지막 활성 슬롯과 교환, `m_count--`) →
  `ParallelFor(0, m_count)` 가 조밀. 대가: 인덱스가 매 스텝 바뀌어 외부 참조 금지(핸들 필요).
  군중을 외부에서 개별 참조 안 하면 압축이 낫다.
- `StepSimAgents`(데모 씬 2)가 이미 `ParallelFor(청크 32)` + 겹치지 않는 `[begin,end)` + 공유
  쓰기 없음으로 이 계약의 프로토타입이다 — `AgentStore::Step` 은 그걸 SoA 로 옮긴 것.

### 6.4 브로드페이즈 (로드맵 D3, 별도)

`physics/p3d` 에 유니폼 그리드 — 개체 대 개체·타겟 질의(타워 사거리)를 O(n)으로. 개체끼리
`CollisionWorld3D` 에 안 넣고 분리력이 겹침을 처리, 콜라이더는 지형·플레이어만
([command-playbook.md](command-playbook.md) 2c, [horde-design.md](horde-design.md) §4.4).

---

## 7. 스레드 / 불변 규칙 매핑

| 규칙 | 이 설계에서 |
|---|---|
| 1 (D3D11 은 렌더 스레드만) | 인스턴스 VB `CreateBuffer`/`Map` 전부 `MeshPass3D` 안. `SnapshotBuilder` 는 값 배열만 |
| 2 (렌더러 코어 = clear/bind/pass 순회) | 새 드로우는 `MeshPass3D::Execute` 안. 코어 불변 |
| 3 (경계는 값 스냅샷만) | `MeshInstance`(24B POD) + `InstanceBatch`. Mat4 팔레트·게임 객체 포인터 없음 |
| 4 (1슬롯 메일박스) | ~192KB/프레임(8k), 오래된 프레임 버려도 무해 |
| 6 (ParallelFor 범위 독립) | `StepSimAgents` 워커는 `ActiveIndices()` 의 자기 `[begin,end)` → `Slots()[active[k]]` 만 쓰기(슬롯 인덱스 유일 → 충돌 없음). `ObjectPool::Acquire/Release`(churn) 는 `Wait()` 뒤 메인에서만 |
| 7 (2D/3D 분리) | `MeshInstance`/`InstanceBatch` 는 `render/r3d`; `SimAgent`/`ObjectPool` 사용은 전부 `#if ENGINE_WITH_3D`. `MakeFrustum` 는 `SnapshotBuilder` 익명(값). `core::ObjectPool` 자체는 모듈 중립 |
| 8 (충돌은 탐지만) | 브로드페이즈는 후보 목록만. 분리력·넉백은 크라우드 스텝(`StepSimAgents` / 장차 `AgentStore`) 안, physics 밖 |

---

## 8. 구현 순서 (측정 게이트마다 멈춤)

1. ~~**스냅샷 인스턴스 타입 + `MeshPass3D` 인스턴스 경로 + `mesh_instanced.hlsl`.**~~ ✅
   `render::MeshInstance`/`InstanceBatch` + `Scene3D::{meshInstances,instanceBatches}`,
   `MeshPass3D` 확장(slot1 `PER_INSTANCE_DATA`, DYNAMIC VB `Map(WRITE_DISCARD)` 1회, 배치당
   `DrawIndexedInstanced`), `mesh_instanced.hlsl` + `shadow_instanced.hlsl`(셰도우 인스턴스 경로).
   `BuildCliffScene` 크라우드가 배치 1개로. `kMaxInstances = 16384`.
2. ~~**프러스텀 + 최대거리 컬링**~~ ✅ `SnapshotBuilder` 익명 헬퍼 `MakeFrustum`(Gribb-Hartmann,
   이 엔진 행렬 규약에 맞춤) + `SphereInFrustum` + `EyeFromView`. 데모 씬 2 크라우드 600마리를
   컬 후 `MeshInstance` 로. (`math::MakeFrustum` 승격은 두 번째 사용처가 생기면.)
3. **LOD 버킷**(거리 3단계). LOD2 는 우선 "작은 큐브 + 셰도우 생략"으로. 빌보드/임포스터는 뒤로.
   → 4k+ 목표. `InstanceBatch.lod` 필드·셰도우 컷은 이미 배선됨 → `SnapshotBuilder` 쪽만.
4. **~~`core::ObjectPool<T>`~~ ✅ + `game/AgentStore`(SoA) + 스폰/디스폰.**
   `core::ObjectPool<T>`(`src/core/ObjectPool.h`) 구현 완료 — 데모 씬 2 크라우드가
   `ObjectPool<SimAgent>`(capacity 1024) 에서 살고, `StepSimAgents` 가 `ActiveIndices()` 를
   `ParallelFor` 로 돌고, 스텝마다 1마리 churn(Acquire/Release 상시 검증). **남음**: SoA 승격
   (`game/AgentStore`, 측정 게이트 — §6.2), 웨이브형 스폰(게임 루프).
5. **브로드페이즈**(로드맵 D3) — 개체끼리 분리력 + 타겟 질의.
6. **(→ [horde-design.md](horde-design.md) §5)** 애니메이션이 필요하면 VAT 를 이 골격 위에.
   `HordePass3D` 는 여기 인스턴스 버퍼·컬링·LOD 를 재사용하고 per-instance 에 `animTime`,
   `clipId` 만 더한다.

각 단계 독립 커밋 가능. 1 이후 언제든 "현재 규모로 충분"이면 멈춰도 된다.

---

## 9. 사용 방법 (How to use) — *구현 후 기준*

### 9.1 군중/다수 오브젝트를 인스턴스로 방출

```cpp
// SnapshotBuilder 안 (BuildCliffScene 이 이 형태). 개체마다 push 하는 대신:
auto& inst = scene.meshInstances;
const std::uint32_t first = (std::uint32_t)inst.size();
for (std::uint32_t i : pool.ActiveIndices())            // core::ObjectPool 순회
{
    const Agent& a = pool.Slots()[i];
    if (!SphereInFrustum(frustum, a.pos, kR)) continue;  // 프러스텀 컬
    if (math::Dot(a.pos - eye, a.pos - eye) > kD * kD) continue;   // 거리 컬
    inst.push_back({ a.pos, a.yaw, a.scale, PackRgba(...) });
}
if (inst.size() > first)
    scene.instanceBatches.push_back({ render::MeshId::Cube, first,
                                      (std::uint32_t)inst.size() - first, /*lod=*/0 });
```

`meshDraws` 는 지형·유니크 프롭 용으로 그대로. 인스턴스 경로는 "같은 메시 수백+" 일 때만.

### 9.2 새 인스턴스 메시 종류 추가

1. `render/r3d/Scene3D.h` `enum class MeshId` 에 값 추가 + `MeshPass3D::Initialize` 의
   `CreateMesh(device, MeshId::New, MakeX())` (또는 파일 로더가 생기면 거기서).
2. `SnapshotBuilder` 에서 그 메시를 쓰는 `InstanceBatch{ .mesh = MeshId::New, ... }` 를 만든다.
   셰이더/레이아웃은 공용(`mesh_instanced`) — 정점 포맷이 같으면 추가 작업 없음.
3. 메시 종류가 여럿이면 배치 조립을 `(meshId, lod)` 중첩 버킷으로.

### 9.3 LOD·컬 튜닝

| 무엇 | 어디 |
|---|---|
| 컬링 최대 거리 | `BuildCliffScene` 의 `kAgentCullDist` (현재 90) |
| 개체 반지름(프러스텀 구 테스트) | `BuildCliffScene` 의 `kAgentCullRadius` (현재 0.5) |
| LOD 경계 | (§8-3 에서 추가) `kLodNear` / `kLodMid` |
| 인스턴스 상한(프레임당) | `MeshPass3D::kMaxInstances` (16384). GPU 측에서 안전하게 자름. **SnapshotBuilder 쪽 캡은 아직 없음** — 규모 커지면 같은 값으로 추가 |
| 셰도우 캐스트 LOD 컷 | `MeshPass3D::DrawInstanced` 의 `batch.lod >= 2` continue |

### 9.4 하지 말 것

```cpp
// ✗ 군중을 개체마다 MeshDraw/ModelDraw 로 — draw call N개, 스냅샷 80B×N
for (auto& a : agents) scene.meshDraws.push_back({...});          // 금지 (인스턴스로)

// ✗ ModelDraw(스킨드)로 군중 — ModelMeshPass3D 는 인스턴스 1개만 CPU 스킨(문서화된 제약)
//    애니메이션 군중은 horde-design.md §5 (VAT). 이 문서는 강체 인스턴스.

// ✗ ParallelFor 워커 안에서 스폰/디스폰/free-list/vector 재할당 (규칙 6)
m_jobs.ParallelFor(0, n, c, [&](size_t b, size_t e){ for(...) store.Despawn(i); });  // 금지

// ✗ 워커가 다른 개체 칸에 쓰기 (겹치는 범위 = 레이스). 힘은 자기 칸에만 누적

// ✗ SnapshotBuilder 에 인스턴스 캡을 두게 되면 MeshPass3D::kMaxInstances 와 다른 값 금지
// ✗ 인스턴스 VB 를 UpdateSubresource 로 — DYNAMIC + Map(WRITE_DISCARD) 로 프레임당 1회
// ✗ mesh_instanced.hlsl 을 인라인 D3DCompile — assets/shaders/ + shaders.Get(...)
```

- 컬링·LOD 를 렌더 스레드에서 하지 말 것 — `SnapshotBuilder`(메인)에서. GPU 로 안 보이는 걸
  안 보낸다. (GPU 컬은 §10, 훨씬 뒤.)
- 불투명 인스턴스를 거리 정렬하지 말 것 — 낭비. 반투명 배치만 정렬.
- `meshInstances` 를 배치 경계와 어긋나게 채우지 말 것 — 배치는 **연속 구간**이어야 한 콜.

---

## 10. 판단 필요 / 열린 질문

- **인스턴스 트랜스폼 포맷: 압축(pos+yaw+scale, 24B) vs 4x3 행렬(48B+).** 군중은 Y회전·균등
  스케일뿐이라 압축이 맞다. 기울기·비균등 스케일(래그돌, 파편)이 필요해지면 배치별로 포맷을
  나누거나 4x3 로. 1차는 압축.
- **색: per-instance(24B) vs per-batch(20B + 배치 색 1개).** 개체마다 색이 다르면 전자, 종류로만
  갈리면 후자가 스냅샷·대역폭에 유리. 데모 씬 2 는 속도로 색 램프 → per-instance.
- **`MeshPass3D` 확장 vs 새 `InstancedMeshPass3D`.** 지금은 확장(메시/셰도우/Frame 재사용).
  인스턴스 경로가 커지고 정점 포맷이 갈라지면 분리.
- **메시 레지스트리.** `enum MeshId` 는 파일 로드 메시가 생기면 한계 — 핸들 기반 레지스트리로
  (로드맵 "메시 레지스트리"와 합류). 인스턴스 배치의 `mesh` 필드가 그 핸들이 된다.
- **GPU 컬링(compute).** CPU 프러스텀 컬로 수천은 충분. 수만+ 이거나 CPU 가 병목이면
  `DrawIndexedInstancedIndirect` + compute 프러스텀/오클루전 컬. 훨씬 뒤.
- **규모 목표 확정.** 화면 내 동시 몇 개를 60fps 로? 이 숫자가 `kCapacity`·`kMaxInstances`·LOD
  거리·빌보드 전환을 전부 결정한다. (WARP 개발 환경에선 실 GPU 수치와 다름 — 상대 비교로.)
- **빌보드/임포스터 방식.** 8방향 프리렌더 스프라이트 아틀라스 vs 단색 쿼드 vs 옥타헤드럴
  임포스터. 강체면 단색/스프라이트로 충분. 애니 군중은 horde-design.md §5.3.

---

## 11. 관련 문서

- [demo-scene.md](demo-scene.md) — 데모 씬 2(`SimAgent` 군중) = 이 경로의 첫 소비자·측정 대상
- [horde-design.md](horde-design.md) §5 — 애니메이션 군중(VAT). 이 문서의 인스턴스 골격 위에 얹힘
- [roadmap.md](roadmap.md) — D2(SoA+풀), D3(브로드페이즈), D4(인스턴싱 렌더) 합류점
- [entity-lifecycle-design.md](entity-lifecycle-design.md) §3A — SoA 배열 vs 컴포넌트 테이블
- [scrollable-list-and-pool.md](scrollable-list-and-pool.md) §1.1 — `core::ObjectPool<T>` 설계
- [multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md) — `JobSystem`, Fence, 스냅샷 경계
- [command-playbook.md](command-playbook.md) — 2c(브로드페이즈), 3b(패스 추가/확장)
- [shader-pipeline.md](shader-pipeline.md) — `assets/shaders/*.hlsl` + `ShaderLibrary::Get`
- [engine-conventions.md](engine-conventions.md) — 좌표계·단위·LOD 방침
