# 호드(대규모 좀비 웨이브) 설계 — 플로우필드 + VAT

**상태: 설계만. 미구현.** Back4Blood 처럼 수백~수천 마리가 벽에 뭉치고, 문틈으로 스트림처럼
쏟아지고, 막히면 산처럼 서로를 타 넘는 그림을 이 엔진에 얹기 위한 아키텍처 문서다.
두 축을 쓴다:

- **플로우필드(flow field)** — 전역 경로. 그리드 한 장에서 목표까지의 방향장을 굽고, 모든
  에이전트가 O(1) 로 샘플. 벽·문·좁은 통로가 자연스럽게 병목·군집을 만든다.
- **VAT(Vertex Animation Texture)** — 렌더. 클립을 로드 시 텍스처로 구워, 인스턴스당
  `float animTime` 하나만 넘기고 `DrawIndexedInstanced` **한 콜**로 전체 호드를 그린다.
  런타임 스키닝 계산이 셰이더에서 사라진다.

관련: [model-animation-research.md](model-animation-research.md) §5.5(VAT 원리), §5.2a(현행 CPU 스킨),
[multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md),
[time-design.md](time-design.md), [collider-design.md](collider-design.md),
[command-playbook.md](command-playbook.md) 2c(브로드페이즈)·3b(새 패스), [roadmap.md](roadmap.md).

---

## 1. 왜 기존 경로로는 안 되나

| 병목 | 현행 | 호드에 필요한 것 |
|---|---|---|
| **렌더** | `ModelMeshPass3D` = CPU 스킨 + `scene3d.modelDraws.front()` **1마리만** 스킨 (§5.2a) | 새 `HordePass3D` — VAT GPU 스킨 + 인스턴스드 드로우. 기존 패스는 안 건드림(OCP) |
| **경로** | 없음 (플레이어 캐릭터만 카메라 상대 이동) | 플로우필드 1장 + 로컬 회피. 에이전트별 A* 는 수천이면 불가 |
| **충돌** | `CollisionWorld3D` = N² · 메인스레드 전용 · 탐지만(규칙 8) | 유니폼 그리드 브로드페이즈(playbook 2c "다음"). 좀비끼리는 충돌 대신 스티어링 |
| **틱 비용** | 매 프레임 `ParallelFor(...).Wait()` 완전 블록 | 호드 업데이트가 `ParallelFor` 의 이상적 부하 (평평한 배열, 겹치지 않는 범위) |

핵심 제약: **개별 스킨드 메시(`ModelDraw`) 로 호드를 만들면 안 된다.** CPU 스킨 1마리 상한이
하드 실링이다. 호드는 별도 데이터·별도 패스로 완전히 분리한다.

---

## 2. 아키텍처 개요

```text
[메인 스레드 / 고정 timestep — Simulation::Step]
  FlowField (game/FlowField.*)          목표까지 방향장, N스텝마다 1회 재빌드
     └─ 정적 벽 그리드(BuildBlockers) + BFS/eikonal
  Horde (game/Horde.*)                  SoA 배열: pos/vel/yaw/state/animTime/clipId
     ├─ 스폰/디스폰                     free-list, 스텝 사이에만 (규칙 6)
     ├─ 이웃 그리드 재구축              uniform hash, 이번 스텝 스냅샷
     ├─ JobSystem::ParallelFor          [begin,end) 범위별 스티어링+적분
     │     desired = flow·sample + separation(이웃) + wallAvoid + climb(밀도)
     │     읽기: posRead[]  쓰기: posWrite[]   (더블버퍼, 규칙 3/6)
     ├─ Fence().Wait()                  단계 경계에서만
     ├─ swap(posRead, posWrite)
     ├─ 사망/전이 처리                  contact(플레이어 공격) → state=Die, 타이머
     └─ CollisionWorld3D (broadphase)   좀비-vs-정적지형, 좀비-vs-플레이어만
[SnapshotBuilder]
  Scene3D.hordeInstances  ← Horde SoA 를 20B 레코드로 압축 (값, 규칙 3)
[렌더 스레드 — HordePass3D::Execute]
  immutable VB(바인드포즈) + VAT(로드 시 구움) + per-instance 버퍼
  거리로 LOD 버킷 → DrawIndexedInstanced × (근/중/원)
```

한 프레임에 스레드 경계를 넘는 것은 여전히 값 기반 `RenderSnapshot` 하나뿐. 호드는
`std::vector<HordeInstance>`(작은 POD) 로만 건너간다 — 본 팔레트도, 가변 게임 객체 포인터도
없다.

---

## 3. 파트 A — 플로우필드 (`game/FlowField.*`)

### 3.1 자료구조

```cpp
namespace engine::game
{
    // 플레이 영역을 덮는 균일 2D 그리드. 셀 하나 = cellSize m 정사각형.
    // y(높이)는 무시 — 지면 위 이동만. 층이 필요하면 층별 필드 1장씩.
    class FlowField final : private core::NonCopyable
    {
    public:
        FlowField(math::Vec2 originXZ, int cols, int rows, float cellSize);

        // 정적 지오메트리에서 막힌 셀 표시. 씬 로드/지오메트리 변경 시 1회.
        void SetBlockers(std::span<const math::Rect> wallsXZ);   // 또는 콜라이더 목록

        // 목표 셀(들)에서 역방향 전파. 매 스텝이 아니라 rebuildEveryNSteps 마다.
        // BFS(4/8-이웃, 균일 비용) 로 시작 — 부족하면 eikonal(대각선 코스트 정확).
        void Rebuild(math::Vec3 targetWorld);

        // 에이전트가 매 스텝 부르는 유일한 함수. 바이리니어로 부드럽게.
        [[nodiscard]] math::Vec2 SampleDirection(math::Vec3 worldXZ) const;
        [[nodiscard]] bool Blocked(math::Vec3 worldXZ) const;

    private:
        math::Vec2 m_originXZ;
        int m_cols, m_rows;
        float m_cellSize;
        std::vector<std::uint16_t> m_cost;   // BFS 거리, 0xFFFF = 막힘/도달불가
        std::vector<math::Vec2>    m_dir;    // 정규화 방향, 재빌드 때 cost 기울기에서
    };
}
```

### 3.2 빌드

1. `m_cost` 를 `0xFFFF` 로 채움. 목표가 있는 셀을 0 으로, 큐에 push.
2. BFS: 이웃 셀 비용 = `min(현재, cur+1)`, 막힌 셀 스킵. 8-이웃이면 대각선 비용 `√2`(정수
   근사 7:5 도 충분).
3. `m_dir[c]` = `normalize( (이웃 중 최소 비용 셀) - c )`. 막힌 셀 인접은 벽에서 밀리는
   성분을 추가로 섞어 벽에 붙어 미끄러지게.

`Rebuild` 는 O(셀 수). 200×200 = 40k 셀 이면 1ms 미만. 목표(플레이어)가 매 프레임 조금씩
움직여도 **`rebuildEveryNSteps`(예: 6~10스텝)** 마다 굽고 그 사이엔 캐시 사용 — 좀비 무리가
1/6초 늦게 방향을 트는 건 안 보인다. 목표가 셀 하나 이상 이동했을 때만 재빌드하는 게이트도 가능.

### 3.3 왜 "벽 군집 + 문틈 스트림 + 산사태" 가 공짜로 나오나

- **문/좁은 통로**: 통로 셀들의 방향이 전부 통로 축을 가리키므로 무리가 자동으로 한 줄로
  수렴 → 입구에서 밀도 폭증 → 분리력(파트 B)이 이들을 옆으로 못 밀어내고(벽) 뒤로도 못
  밀어냄(뒤에서 계속 옴) → **입구 앞에 반원형으로 뭉친다.**
- **막다른 벽**: 벽 바로 앞 셀 방향이 벽을 향하지만 벽 셀은 막힘 → 도착 못 하고 벽면을 따라
  미끄러지는 성분만 남음 → 벽에 붙어 옆으로 흐르며 쌓인다.
- **산사태**: 파트 B 의 `climb` 항(로컬 밀도 > 임계 → desired 에 소량 +Y) 이 뭉친 덩어리를
  봉긋 솟게 하고, 앞줄을 타 넘은 개체가 벽 너머 셀로 떨어지면 그 셀 방향장을 다시 따라간다.
  네비메시·경로탐색 없이 밀도만으로 "쏟아짐" 이 나온다.

---

## 4. 파트 B — 호드 시뮬 (`game/Horde.*`)

### 4.1 데이터 레이아웃 — SoA, 고정 capacity

```cpp
namespace engine::game
{
    enum class ZombieState : std::uint8_t { Idle, Chase, Climb, Attack, Die, Dead };

    class Horde final : private core::NonCopyable
    {
    public:
        static constexpr std::size_t kCapacity = 4096;   // 상한, 재할당 안 함

        explicit Horde(core::JobSystem& jobs);

        // --- 스텝 사이(메인 스레드)에만: 절대 ParallelFor 워커 안에서 금지 (규칙 6) ---
        int  Spawn(math::Vec3 pos, std::uint16_t typeId);   // free-list 에서, 실패 시 -1
        void Despawn(int index);                            // Dead 처리 후

        // --- 고정 스텝 ---
        void Step(float dt, const FlowField& flow, math::Vec3 playerPos,
                  std::span<const AttackHit> hitsThisStep);

        // --- 읽기 (SnapshotBuilder 용) ---
        [[nodiscard]] int AliveCount() const { return m_alive; }
        [[nodiscard]] const std::vector<math::Vec3>& Positions() const { return m_pos[m_read]; }
        [[nodiscard]] const std::vector<float>& Yaw() const { return m_yaw; }
        [[nodiscard]] const std::vector<float>& AnimTime() const { return m_animTime; }
        [[nodiscard]] const std::vector<std::uint16_t>& ClipId() const { return m_clipId; }
        [[nodiscard]] const std::vector<std::uint8_t>& Alive() const { return m_slotAlive; }

    private:
        core::JobSystem& m_jobs;
        int m_alive{ 0 };
        int m_read{ 0 };                       // 더블버퍼 인덱스
        std::vector<math::Vec3> m_pos[2];      // read/write 스왑 (규칙 3: 이웃은 이전 프레임 읽기)
        std::vector<math::Vec3> m_vel;
        std::vector<float>         m_yaw;
        std::vector<float>         m_animTime; // 현재 클립 재생 시간 (초)
        std::vector<std::uint16_t> m_clipId;   // VAT 클립 행 그룹
        std::vector<std::uint16_t> m_typeId;   // 좀비 종류 (클립 세트·속도·체력)
        std::vector<std::uint8_t>  m_state;
        std::vector<std::uint8_t>  m_slotAlive;
        std::vector<float>         m_health;
        std::vector<int>           m_freeList;
        SpatialHash                m_neighbors; // 이번 스텝 이웃 그리드
    };
}
```

`EntityRegistry` 안 씀 — 좀비는 조합이 균일한 단일 종류라 SoA struct 배열이 맞다
([entity-lifecycle-design.md](entity-lifecycle-design.md) §3). 종류(typeId)는 정수 한 칸이면 충분.

### 4.2 스텝 (한 고정 스텝)

```text
1. 이웃 그리드 재구축                 m_pos[m_read] 를 셀 버킷에 (메인, O(n) 또는 counting-sort 2패스)
2. 피격 반영                          hitsThisStep → m_health 감소, 0 이하면 state=Die, animTime=0
3. ParallelFor(0, m_alive, chunk):    각 잡이 겹치지 않는 [begin,end)
     for i in [begin,end):
        if state==Die/Dead: 사망 클립만 진행, 이동 0
        dir  = flow.SampleDirection(posR[i])
        sep  = Σ over 이웃 j: (posR[i]-posR[j]) / dist²         (반경 내, 이웃 그리드로)
        wall = flow.Blocked(posR[i]+dir*probe) ? 벽법선 밀기 : 0
        dens = 이웃 수 / 기대치
        climb = dens > kClimbDensity ? Vec3{0, kClimbRise, 0} : 0
        desired = norm(dir)*speed(typeId,state) + sep*kSep + wall*kWall + climb
        m_vel[i] = lerp(m_vel[i], desired, kAccel*dt)          (관성)
        posW[i]  = posR[i] + m_vel[i]*dt
        posW[i].y = max(groundHeight(posW[i]), posW[i].y - kFall*dt)   (climb 후 낙하)
        m_yaw[i] = turnToward(m_yaw[i], atan2(vel.x,vel.z), kTurn*dt)
        m_animTime[i] += dt
        state/clipId 전이: 밀도·속도·플레이어 거리로 Chase/Climb/Attack 선택
   Fence().Wait()                     (규칙 6: 프레임 단계 경계에서만)
4. swap(m_read)                       posW 가 다음 스텝의 posR
5. 사망 완료(animTime > dieDur) → state=Dead, 큐에 Despawn 예약
6. CollisionWorld3D: 좀비 AABB(브로드페이즈) vs 정적지형·플레이어만 Add/Step, contact 로
   플레이어 피해 판정 (응답=넉백은 호드가, physics 밖 — 규칙 8)
```

**규칙 6 준수**: 워커는 `posW[i]` 자기 칸만 쓰고, 읽기는 전부 `posR`(이전 스텝, 불변). 공유
카운터 증가·`vector` 재할당·스폰/디스폰 전부 워커 밖. 이웃 그리드는 스텝 시작 시 만들어 스텝
동안 read-only.

### 4.3 스폰/디스폰 — 메인 스레드, free-list

- **웨이브 스포너**(`game/WaveDirector.*`, 별도): 스폰 볼륨 목록 + 시간축. 화면 밖/차폐된
  볼륨에서 초당 N마리, free-list 에서 슬롯 회수. capacity 꽉 차면 가장 먼 Dead 슬롯부터 재활용.
- 디스폰: 사망 애니메이션 끝 + 일정 시간, 또는 플레이어 뒤로 멀어져 컬링. `Despawn` 이
  `m_freeList` 에 push, `m_slotAlive[i]=0`. `m_alive` 는 "마지막 활성 슬롯+1" 로 관리하거나
  압축(swap-remove) — 압축하면 `ParallelFor` 범위가 조밀해져 유리.

### 4.4 충돌 (playbook 2c "다음" 과 합류)

- `physics/p3d` 에 **유니폼 그리드 브로드페이즈** 추가 (현행 N² → O(n)). 이건 호드와 무관하게
  로드맵에 이미 있는 항목.
- 호드는 좀비끼리 `CollisionWorld3D` 에 안 넣는다 — 분리력이 그 역할. 넣는 건 좀비-vs-정적
  지형(벽 관통 방지)과 좀비-vs-플레이어(피해). 벽 관통은 플로우필드 `Blocked` + wallAvoid 로
  대부분 처리되고, 콜라이더는 보정용.

---

## 5. 파트 C — VAT 렌더 (`render/r3d/HordePass3D`)

`IRenderPass` 구현 하나 추가. `main.cpp` 에서 `renderer.AddRenderPass(std::make_unique<HordePass3D>(...))`
(Start 전). `ModelMeshPass3D`·`MeshPass3D` 안 건드림(OCP). 셰이더는 `assets/shaders/horde.hlsl`
(+ 필요 시 `horde_shadow.hlsl`), 인라인 `D3DCompile` 금지 — `shaders.Get(...)` ([shader-pipeline.md](shader-pipeline.md)).

### 5.1 VAT 굽기 (로드 시 1회)

`model-animation-research.md` §5.5 의 "버텍스 애니메이션 텍스처" 갈래를 쓴다.

- 좀비 모델 + 클립 세트(walk / run / lunge / climb / attack / die 등)를 `import::LoadModelFromFile`
  + `LoadAnimationClipsFromFile`(scale 일관성 — §5.2a) 로 로드.
- 각 클립을 `sampleRate`(예: 24fps) 로, `anim::AnimationSampler::Evaluate` + LBS 를 **CPU 에서
  프레임 수만큼** 돌려 최종 정점 위치(모델 로컬 공간)를 배열로. 매 프레임이 아니라 로드 시 1회라
  고정 스텝·스레드 규칙과 무관.
- 픽셀 데이터(값 배열)만 렌더 스레드에 넘기고, **`ID3D11Device::CreateTexture2D` 는 렌더 스레드가**
  (규칙 1·2). 레이아웃:
  - `DXGI_FORMAT_R16G16B16A16_FLOAT`, **폭 = 정점 수, 높이 = Σ(클립별 프레임 수)**.
  - 두 번째 텍스처에 법선(또는 rgb10a2). 접선까지 필요하면 세 번째.
  - 클립별 시작 행(`clipRowOffset[clipId]`) 과 프레임 수를 상수 버퍼/작은 lookup 으로.
- 좌표는 **모델 로컬 공간**으로 구움(월드는 인스턴스 트랜스폼) → 값 범위가 작아 fp16 오차 적음.
- 정점 수가 큰 메시는 VAT 가 커진다 — 좀비 메시는 LOD0 을 3~6k 정점으로 잡고, 24fps·(walk 32f
  + run 20f + …) ≈ 150행 × 5k열 × 8B ≈ 6MB/텍스처. 허용.

### 5.2 인스턴스 데이터 + 스냅샷 값 타입

```cpp
// render/r3d/Scene3D.h 에 추가 (값 타입, 규칙 3)
struct HordeInstance
{
    math::Vec3 pos;      // 월드, 지면
    float      yaw;      // 라디안
    float      animTime; // 현재 클립 재생 시간 (초) — 셰이더가 sampleRate 로 프레임행 변환
    std::uint16_t clipId;// VAT 클립 그룹
    std::uint16_t lod;   // 0 근 / 1 중 / 2 임포스터 — SnapshotBuilder 가 카메라 거리로 채움
};                       // 20 bytes. 4096마리 = 80KB/프레임 — 1슬롯 메일박스에 허용

struct Scene3D
{
    // ...기존...
    std::vector<HordeInstance> hordeInstances;
};
```

`SnapshotBuilder` 가 `Horde` SoA + `CameraView` 로 이 배열을 채운다(거리로 LOD 버킷, 원거리·
프러스텀 밖은 스킵). 게임 개념(`Horde`) 은 여기서 렌더 프리미티브로 번역되고 렌더러는
`HordeInstance` 만 안다(OCP/DIP — [engine-overview.md](engine-overview.md)).

### 5.3 드로우

```text
HordePass3D::Initialize:  horde.hlsl 로드, 바인드포즈 VB/IB(immutable) 생성, VAT 2장 업로드,
                          per-instance DYNAMIC 버퍼(kCapacity) 생성, 인스턴스 입력 레이아웃
                          (slot 1, D3D11_INPUT_PER_INSTANCE_DATA).
HordePass3D::Execute:
  1. hordeInstances 를 lod 로 안정 분할 (근/중/원). 각 그룹을 DYNAMIC 버퍼에 Map/WRITE_DISCARD.
  2. LOD0/1: DrawIndexedInstanced(lodIndexCount, groupCount, ...)  — VAT 스킨
  3. LOD2 : 임포스터(빌보드 쿼드 아틀라스) 인스턴스드 드로우 — 8방향 프리렌더 스프라이트
  4. 그림자: LOD0 만, horde_shadow.hlsl 로 depth-only (또는 원거리 생략)
```

VS 개념(스키닝 수식이 사라짐):

```hlsl
// per-vertex: SV_VertexID ;  per-instance: pos, yaw, animTime, clipId
uint frame   = clipRowOffset[clipId] + (uint)(animTime * kSampleRate) % clipFrameCount[clipId];
float3 lp    = vatPos.Load(int3(vid, frame, 0)).xyz;      // 이미 스킨된 로컬 위치
float3 ln    = vatNrm.Load(int3(vid, frame, 0)).xyz;
float3 wp    = RotateY(lp, yaw) + pos;                     // 인스턴스 트랜스폼
o.pos = mul(float4(wp,1), viewProj);
o.nrm = RotateY(ln, yaw);
// 이후 cel.hlsl 과 같은 툰 셰이딩 재사용 (common3d.hlsli)
```

프레임 보간은 처음엔 생략(포인트, 24fps 면 충분). 부드럽게 하려면 두 행 `Load` 후 `lerp`.

### 5.4 클립 전이

VAT 는 클립 경계에서 튀지 않게 하려면 크로스페이드가 필요하지만(두 clipId·두 animTime lerp),
1차 구현은 **스냅 전환**(호드 규모에선 잘 안 보인다). `Horde` 가 state 바뀔 때 `clipId` 갈고
`animTime=0`. die 는 `PlayMode::Once` 취급(마지막 프레임 홀드).

---

## 6. 스레드 / 불변 규칙 매핑

| 규칙 | 이 설계에서 |
|---|---|
| 1 (D3D11 은 렌더 스레드만) | VAT `CreateTexture2D`·인스턴스 버퍼 `Map` 전부 `HordePass3D` 안. 굽기(CPU)는 값 배열만 산출 |
| 3 (경계는 값 스냅샷만) | `std::vector<HordeInstance>` (POD 20B). 본 팔레트·`Horde` 포인터 안 넘김 |
| 4 (1슬롯 메일박스) | 80KB/프레임, 오래된 프레임 버려도 무해 |
| 6 (ParallelFor 범위 독립) | 워커는 `posW[i]` 자기 칸만 쓰고 `posR`(불변) 만 읽음. 스폰/디스폰·grid rebuild·vector 재할당은 스텝 밖 메인 |
| 7 (2D/3D 분리) | `game/Horde`·`game/FlowField` 는 `math` 공통만. 렌더는 `render/r3d/HordePass3D`. `ENGINE_WITH_3D` 로 감쌈 |
| 8 (충돌은 탐지만) | `CollisionWorld3D` 는 contact 만. 넉백·밀어내기는 `Horde` 안 |

---

## 7. 구현 순서 (측정 게이트마다 멈춤)

1. **`FlowField` + `Horde`(SoA, 스티어링) + 무텍스처 인스턴스드 큐브.**
   `HordePass3D` 가 내장 큐브를 `DrawIndexedInstanced`. 애니메이션·LOD·VAT 없음.
   → 1~2k 마리 스텝+드로우가 프레임 예산 안인지 측정. **이 단계만으로 "벽 군집 + 문틈 스트림 +
   밀도 climb" 가 눈에 보인다.** (핵심 게임필을 여기서 확정)
2. **`physics/p3d` 유니폼 그리드 브로드페이즈** (좀비-지형/플레이어). 로드맵 2c.
3. **VAT 굽기 + `horde.hlsl`** — LOD0 만. 이제 애니메이션 좀비.
4. **LOD1 + 임포스터(LOD2)** → 4k+ 목표. 원거리 그림자 컬.
5. **웨이브 디렉터**(스폰 볼륨·시간축·강도 곡선) + 사망 처리 다듬기.
6. (선택) 클립 크로스페이드, 프레임 보간, 래그돌-라이트.

각 단계는 독립적으로 커밋 가능하고, 1 이후 언제든 "현재 규모로 충분" 하면 멈춰도 된다.

---

## 8. 사용 방법 (How to use) — *구현 후 기준*

### 8.1 웨이브 스크립팅

`game/WaveDirector` 가 시간·강도만 안다. 스폰 지점은 씬 데이터.

```cpp
// InGame 진입 시 (game/Application 또는 씬 로더)
WaveDirector director;
director.AddSpawnVolume({ .boxXZ = {-40,-40, 8, 8}, .facing = +X });   // 맵 가장자리
director.AddSpawnVolume({ .boxXZ = { 32, 10, 6, 6}, .facing = -Z });
director.SetSchedule({
    { .atSeconds =  0, .ratePerSec =  4, .typeId = kWalker },
    { .atSeconds = 20, .ratePerSec = 12, .typeId = kWalker },
    { .atSeconds = 45, .ratePerSec =  2, .typeId = kBruiser },   // 특수
    { .atSeconds = 60, .rest = 30 },                             // 소강
});

// 고정 스텝 (Simulation::Step 안, 플레이어 스텝 뒤)
director.Step(dt, m_horde, camera-visible-set);   // 안 보이는 볼륨에서만 Spawn 호출
flow.Rebuild(playerPos);                          // 내부에서 N스텝 게이트
m_horde.Step(dt, flow, playerPos, attackHits);
```

목표를 바꾸려면(방어 지점, 탈출구) `flow.Rebuild(objectivePos)` 의 인자만 교체. 다중 목표는
`Rebuild(span<Vec3>)` 로 여러 셀을 0 으로 시드.

### 8.2 새 좀비 종류 추가

1. 모델 + 클립 세트를 `assets/models/<name>/` 에. 클립 파일명 규약은
   `CharacterAnimationClips.h` 스타일로 `<name>ClipManifest` 배열 하나.
2. `HordePass3D` 로드 목록에 그 매니페스트 추가 → VAT 에 클립 행 그룹이 붙는다
   (`clipRowOffset` 자동 확장).
3. `game/ZombieTypes.h` 에 `{ typeId, speed, health, clipset, scale }` 한 줄. `Horde::Step` 의
   속도·전이는 `typeId` 로 테이블 조회하므로 분기 추가 불필요.
4. `WaveDirector` 스케줄에서 `typeId` 로 참조.

### 8.3 파라미터 튜닝 위치

| 무엇 | 어디 |
|---|---|
| 그리드 해상도·재빌드 주기 | `FlowField` 생성자 인자, `kRebuildEveryNSteps` |
| 분리/벽/climb 가중치, 밀도 임계, 관성 | `Horde` 의 `static constexpr k*` |
| 좀비별 속도·체력·클립 | `game/ZombieTypes.h` 테이블 |
| LOD 거리, 임포스터 전환 | `SnapshotBuilder` 의 `kLodNear/kLodMid` |
| VAT sampleRate, fp 포맷 | `HordePass3D` VAT 굽기 상수 |
| 웨이브 강도 곡선 | `WaveDirector::SetSchedule` (데이터) |

### 8.4 하지 말 것

```cpp
// ✗ 호드를 ModelDraw 로 — CPU 스킨 1마리 상한에 걸린다
for (auto& z : zombies) scene.modelDraws.push_back({...});   // 금지

// ✗ ParallelFor 워커 안에서 스폰/디스폰/재할당 (규칙 6)
m_jobs.ParallelFor(0, n, chunk, [&](size_t b, size_t e){
    for (...) if (dead) m_horde.Despawn(i);                  // 금지 — 스텝 밖에서
});

// ✗ 워커가 다른 에이전트 칸을 쓰기 (겹치는 범위)
posW[j] += push;   // j 가 내 [begin,end) 밖이면 데이터 레이스. 힘은 내 칸에만 누적

// ✗ 본 팔레트를 스냅샷에 싣기 — VAT 는 animTime float 하나면 된다
struct HordeInstance { std::vector<Mat4> palette; };         // 금지

// ✗ 패스에서 ClipCursor/셰이더 인라인 컴파일 / MeshPass3D 수정
// horde.hlsl 은 assets/shaders/, shaders.Get(...). 기존 패스는 OCP.
```

- 플로우필드 재빌드를 매 스텝 하지 말 것 — N스텝 게이트. 목표가 안 움직이면 스킵.
- 좀비끼리 `CollisionWorld3D` 에 넣지 말 것 — 분리력이 그 일. 콜라이더는 지형·플레이어만.
- `Horde::Step` 밖에서 `m_animTime` 를 전진시키지 말 것 (고정 스텝 규칙, [time-design.md](time-design.md)).

---

## 9. 판단 필요 / 열린 질문

- **VAT 굽기: 로드 시 vs 오프라인 툴 산출물 커밋.** 로드 시는 단순하지만 시작 시간이 는다
  (좀비 5종 × 클립 6개 × 프레임 = CPU 스킨 수만 번). 먼저 로드 시로, 시작이 느리면
  `tools/` 에 베이커 추가하고 `.vat` 를 커밋.
- **프레임 보간 필요 여부.** 24fps 스냅으로 시작, 근접 LOD0 에서 계단이 보이면 2행 lerp.
- **climb 를 진짜 +Y 로 vs 가짜(앞줄 위로 스프라이트만).** 진짜 +Y 는 낙하·착지 처리가 붙고
  플레이어가 타 넘긴 좀비에 맞을 수 있음(게임필 ↑). 가짜는 싸다. 1차는 진짜 +Y, 소량.
- **네비: 플로우필드로 충분한가.** 단순 아레나·복도면 충분. 다층·점프 구간·동적 장애물(문
  부수기)이 생기면 필드에 동적 blocker 갱신 + 부분 재빌드, 그래도 안 되면 셀별 여러 방향
  (flow field + portal) 로 확장.
- **`Horde` 압축(swap-remove) vs 슬롯 유지.** 압축하면 `ParallelFor` 가 조밀해 빠르지만 인덱스가
  매 스텝 바뀜(외부 참조 금지). 좀비를 외부에서 개별 참조할 일이 없으면 압축이 낫다.
- 규모 목표 확정: 화면 내 동시 몇 마리를 60fps 로? 이 숫자가 LOD 거리·capacity·VAT 해상도를
  전부 결정한다.

---

## 10. 관련 문서

- [model-animation-research.md](model-animation-research.md) §5.5 — VAT/본행렬텍스처 원리, §5.2a — 현행 CPU 스킨(호드가 대체하는 것)
- [collider-design.md](collider-design.md) — 충돌 탐지 계약, 브로드페이즈 확장 지점
- [multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md) — `JobSystem`, Fence, 스냅샷 경계
- [time-design.md](time-design.md) — 고정 timestep, 애니메이션 시간 전진 규칙
- [command-playbook.md](command-playbook.md) — 2c(브로드페이즈), 3b(새 렌더 패스), 2(게임 시스템)
- [entity-lifecycle-design.md](entity-lifecycle-design.md) §3 — SoA struct 배열 vs 컴포넌트 테이블
- [demo-scene.md](demo-scene.md) — 단일 캐릭터 컨트롤러(호드와 분리된 플레이어 경로)
- [roadmap.md](roadmap.md) — 인스턴싱·메시 레지스트리 항목과 합류점
