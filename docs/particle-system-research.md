# 파티클 시스템 연구 — 총구 이펙트 + 폭발 버섯구름

**상태: 연구 + 설계만. 미구현.** 코드 변경 없음(요청: "문서로만 남겨"). 목표 두 가지를 기준으로
이 엔진 구조(스레드 경계·2D/3D 분리·스냅샷·셰이더 파이프라인)에 맞는 파티클 아키텍처를 조사하고
설계한다:

1. **총기 격발** — 총구 화염(머즐 플래시) + 화약연기
2. **폭탄 폭발** — 섬광 + 화구 + 버섯구름(줄기+갓) + 잔해/불티

관련: [instanced-rendering.md](instanced-rendering.md)(이 문서가 가장 많이 재사용하는 패턴 —
인스턴스 배열+배치+DrawIndexedInstanced), [horde-design.md](horde-design.md)(SoA+풀+새 렌더 패스
선례), [texture-atlas-and-sprite-pass.md](texture-atlas-and-sprite-pass.md)(아틀라스 재사용),
[image-assets.md](image-assets.md)(텍스처 디코드 진입점), [shader-pipeline.md](shader-pipeline.md),
[msaa.md](msaa.md)(씬 타깃), [time-design.md](time-design.md)(고정 스텝 — "애니메이션·파티클도
고정 스텝"), [multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md),
[command-playbook.md](command-playbook.md) 2(게임 시스템)·2f(인스턴싱)·3b(새 패스).

기존에 있는 것과 헷갈리지 말 것: `Simulation::m_particles`(20,000개, `SnapshotBuilder`가 앞
2,048개만 `worldQuads`로 그리는 **JobSystem 처리량 스텁**, CLAUDE.md 이력 4번)은 게임플레이
VFX가 아니라 순수 벤치마크용 더미 데이터다. 이 문서가 설계하는 파티클 시스템과는 무관 —
스텁을 걷어낼 때 이 문서의 구조로 대체하는 게 자연스러운 다음 단계이긴 하다(§9 참고).

---

## 1. 업계 리서치 — 게임 파티클 시스템이 공통으로 쓰는 것

VFX 파티클(머즐 플래시·연기·폭발·불티)은 물리 시뮬레이션이 아니라 **속임수의 조합**이다. 핵심
개념 정리(Unity/Unreal/자체 엔진 공통):

| 개념 | 뭐하는 것 | 이 엔진에 있는 대응물 |
|---|---|---|
| **이미터(Emitter)** | 위치/방향에서 파티클을 생성. 버스트(한번에 N개) vs 레이트(초당 N개) | 없음 — §4 |
| **빌보드(Billboard)** | 항상 카메라를 향하는 사각형에 텍스처. 파티클의 기본 도형 | 없음(3D 메시는 있음) — §5 |
| **오버 라이프타임 커브** | 크기/색/알파가 나이(0..1)에 따라 변함(주로 시작값→끝값 lerp로 근사) | 없음 — §4.1 |
| **플립북/텍스처 애니메이션** | 텍스처 아틀라스의 프레임을 순서대로 재생(불꽃이 타는 것처럼) | `TextureAtlas`/`AtlasIndex` 있음 — §5.4 |
| **블렌드 모드** | 가산(Additive, 밝은 빛·불꽃) vs 알파(스모크·먼지, 정렬 필요) | 알파블렌드만 있음(`Dx11Renderer`) — §6.2 |
| **소프트 파티클** | 씬 depth 와 비교해 파티클이 지오메트리와 만나는 경계를 부드럽게 | 없음 — §9 열린 질문 |
| **GPU 인스턴싱 드로우** | 파티클 수천 개를 드로우 콜 하나로 | 이미 있음(크라우드) — §5 그대로 재사용 |
| **컴퓨트 시뮬레이션** | 이동/수명을 GPU에서 계산(수만~수십만 개) | 없음, D3D11 컴퓨트 자체도 아직(§3f) |
| **서브이미터/이벤트** | 파티클이 죽을 때 다른 이미터를 트리거(불티가 튀며 스파크 발생) | 없음 — v1은 이펙트 조합 함수로 대체(§7) |

**CPU vs GPU 시뮬레이션**: 이 엔진은 컴퓨트 셰이더 경로가 없다(`command-playbook.md` 3f "다음").
머즐 플래시(수십 개)·폭발(수백 개) 규모는 CPU + `JobSystem::ParallelFor`로 충분 — 크라우드가
이미 수천 개 스텝을 이 방식으로 처리한다(§9.5~9.6 참고 사례). 컴퓨트는 나중에 폭발 규모가
수만 개로 커지면(예: 파괴 파편) 재검토.

**드로우 방식**: 파티클은 "카메라를 향하는 텍스처 쿼드 수백 개"라서, 정점 포맷·블렌드·셰이딩이
크라우드 인스턴싱(불투명, Y축 회전만, 조명 받음)과 근본적으로 다르다 → `MeshPass3D` 확장이 아니라
**새 패스**가 맞다(호드 설계 §2의 "완전히 분리" 원칙과 동일한 판단, 아래 §5).

---

## 2. 요구사항 분석

### 2.1 총구 이펙트 (격발)

실제 총기 발사 VFX는 보통 이 레이어로 분해된다:

1. **머즐 플래시** — 총구에서 0.02~0.05초, 밝은 가산 블렌드 쿼드 1~3장(큰 별 모양 플래시 +
   작은 코어), 카메라/총구 방향 랜덤 회전. 조명 영향 없음(자체발광).
2. **연기(스모크 퍼프)** — 플래시와 동시에 생성, 알파블렌드 회색 스프라이트 2~4장, 위로 약간
   부양 + 서서히 확산, 수명 0.4~1초. 플래시가 꺼진 뒤에도 잠깐 남아 "방금 쐈다"는 잔상.
3. **불티(옵션)** — 소수(3~6개) 가산 블렌드 점, 짧은 수명, 약한 중력, 빠른 속도.
4. **탄피 배출** — 파티클이 아니라 강체(인스턴스 메시 낙하 + 회전) 영역, 이 문서 범위 밖
   ([instanced-rendering.md](instanced-rendering.md) 몫).

핵심 특성: **버스트 하나**(격발 이벤트당 1회, 위치/방향 = 총구 소켓 트랜스폼)이고 수명이 아주
짧다. 초당 발사 속도가 높은 무기는 버스트가 빈번히 반복될 뿐 "연속 이미터"는 아니다.

### 2.2 폭발 버섯구름

버섯구름은 사실 **단일 이펙트가 아니라 시간차를 둔 여러 이펙트의 합성**이다(실사 폭발 레퍼런스
공통):

1. **t=0, 섬광** — 초대형 가산 블렌드 구/쿼드, 1~2프레임, 매우 빠른 페이드. 화면 전체 밝기에
   영향(블룸이 있으면 여기서 크게 번짐 — 이 엔진엔 블룸 없음, §9).
2. **t=0~0.3s, 화구(fireball)** — 폭심에서 바깥으로 빠르게 팽창하는 가산 블렌드 불꽃 텍스처
   클러스터(8~16장), 플립북 애니메이션 또는 주황→빨강→어두운 색 램프, 팽창하며 감속·페이드.
3. **t=0~2s, 상승 연기 기둥(줄기, stem)** — 폭심 지면에서 위로 쏘아 올리는 알파블렌드 연기
   스프라이트들. 초기 상승 속도가 크고 항력(drag)으로 감속.
4. **t=0.3~수 초, 갓(cap)** — 줄기 파티클이 특정 고도(정점)에 도달하면 **수직 속도를 거의 죽이고
   수평(반경 방향) 속도를 얻어** 옆으로 퍼진다 → 버섯 갓 모양이 저절로 나온다. 크기도 나이에
   따라 커짐(연기가 냉각·확산되며 부피가 느니까). 색은 밝은 회백색(열기)→짙은 회색/검정(그을음)
   으로 나이에 따라 어두워짐.
5. **불티/파편** — 소수의 밝은 점(가산) + (옵션) 강체 파편 인스턴스(돌·잔해, 탄도 곡선 + 지면
   충돌로 정지) — 파편은 이 문서 범위 밖([instanced-rendering.md](instanced-rendering.md) 몫,
   §7에서 연동 지점만 표시).
6. **충격파 링(옵션)** — 지면에 붙는 확장하는 얇은 알파 링 텍스처. 엄밀히는 파티클이 아니라
   디컬/오버레이 쿼드 하나에 가깝다 — §9 열린 질문.

핵심 특성: **여러 이펙트 정의(버스트+연속 혼합)를 하나의 "폭발" 함수가 순차/동시 트리거**하고,
줄기→갓 전환처럼 **파티클마다 나이에 따른 상태 전이**가 필요하다 — 총구 이펙트보다 한 단계
복잡하다.

---

## 3. 이 엔진 구조와의 접점

| 필요한 것 | 기존 재사용처 |
|---|---|
| 값 스냅샷 인스턴스 배열 + 배치 그룹핑 + `DrawIndexedInstanced` | `MeshInstance`/`InstanceBatch` 패턴 그대로(`instanced-rendering.md` §3~4) — 정점 포맷·셰이더만 빌보드용으로 교체 |
| 새 렌더 패스(기존 패스 안 건드림, OCP) | `HordePass3D` 선례(`horde-design.md` §5) — 완전 분리 이유도 동일(정점 포맷·블렌드가 다름) |
| 고정 용량 풀 + `ActiveIndices()` + `ParallelFor` 순회 | `core::ObjectPool<T>`(크라우드가 쓰는 것, §6.1 그대로) |
| 스폰/디스폰은 스텝 밖, 워커는 겹치지 않는 범위만 | 규칙 6, 크라우드 `StepSimAgents`와 동일 계약 |
| 텍스처 로드 | `import::LoadImageFromFile`(`image-assets.md`) — dev 편의 경로. 배포는 `.dds` |
| 여러 프레임(스프라이트)을 이름/그리드로 찾기 | `render::TextureAtlas`/`AtlasIndex`(`texture-atlas-and-sprite-pass.md`) — **그룹 `vfx/` 하나 추가**로 충분, 새 시스템 불필요 |
| 셰이더 로딩 | `ShaderLibrary::Get` + `assets/shaders/particle.hlsl`(신규), 인라인 컴파일 금지 |
| 블렌드 스테이트를 패스가 소유 | `QuadPass2D`/`SpritePass2D` 선례(SrcAlpha/InvSrcAlpha) — 파티클 패스는 **가산 블렌드 스테이트를 새로 추가**해야 함(엔진 최초) |
| 고정 스텝에서 전진 | `time-design.md` 불변 규칙 4 — "애니메이션·파티클도 고정 스텝" |
| 씬 타깃(멀티샘플)에 그리기 | 다른 3D 패스와 동일하게 `PassContext::renderTarget`/`depthStencil` 그대로 사용(`msaa.md`) |

**없어서 새로 필요한 것 한 가지**: 카메라의 월드 공간 right/up 벡터. 빌보드는 화면을 향해야
하므로 VS가 카메라 축을 알아야 한다. 지금 `Frame` cbuffer(b0, `common3d.hlsli`)는 `viewProj` +
조명만 있고 `view` 자체나 카메라 축은 없다. `Dx11Renderer`가 매 프레임 `Scene3D::camera`로
`FrameConstantsGpu`를 채우는 지점(`render/r3d/FrameConstants.h`)에서 `camera.view`의 첫 두
행(월드 공간 right, up — 뷰 행렬이 정규직교라 행이 곧 카메라 축)을 뽑아 cbuffer에 얹으면 된다.
**CLAUDE.md 불변 규칙**: `FrameConstantsGpu`(C++)와 `common3d.hlsli`의 `cbuffer Frame` 레이아웃은
항상 같이 고친다 — 이 필드 추가도 예외 없음.

---

## 4. 데이터 설계

### 4.1 `Particle` (AoS, `core::ObjectPool<Particle>`에 상주)

```cpp
namespace engine::game::vfx
{
    struct Particle
    {
        math::Vec3 pos{};
        math::Vec3 vel{};
        float      age{ 0.0f };      // 초, Reset 후 0부터
        float      life{ 1.0f };     // 총 수명(초). age >= life 면 디스폰 대상
        float      sizeStart{ 0.1f }, sizeEnd{ 0.1f };     // 빌보드 반경(월드 단위), age/life 로 lerp
        float      rotation{ 0.0f }, angularVel{ 0.0f };   // 화면 평면 롤(라디안)
        std::uint32_t colorStart{ 0xffffffffu };           // 8:8:8:8 RGBA, age/life 로 lerp
        std::uint32_t colorEnd{ 0x00000000u };
        std::uint16_t effectId{ 0 };   // ParticleEffectDef 인덱스 — 블렌드/아틀라스/드래그/중력 참조
        std::uint8_t  phase{ 0 };      // 이펙트별 상태(예: 버섯구름 Rising/Capping). §7.2
        void Reset() { *this = Particle{}; }   // core::ObjectPool 계약
    };
}
```

- **색을 시작/끝 두 값만**(중간 커브 없음) — Unity/Unreal의 풀 커브 대신 lerp 하나로 시작.
  머즐 플래시·화구·연기 전부 "밝다가 어두워짐/불투명하다가 투명해짐" 단조 변화라 충분하다.
  다단 그라디언트가 필요해지면 `colorStart/Mid/End` 3점으로 늘리거나(값 추가, OCP), 텍스처
  자체에 색을 넣는(플립북) 방식으로 회피 가능.
- **`effectId`가 이펙트별 튜닝 전부를 참조**(§4.2) — `Particle` 자체는 순수 상태(위치/속도/나이)
  만 들고, "얼마나 빨리 페이드하는지" 같은 정책은 안 든다(SRP).

### 4.2 `ParticleEffectDef` — 정책 테이블 (constexpr, `game/CrowdConfig.h`·`ZombieTypes.h`와 같은 관행)

```cpp
namespace engine::game::vfx
{
    enum class BlendMode : std::uint8_t { Additive, AlphaBlend };

    struct ParticleEffectDef
    {
        BlendMode     blend{ BlendMode::AlphaBlend };
        std::uint32_t atlasId{ 0 };          // vfx 아틀라스 페이지/그룹
        const char*   spriteName{ "" };      // AtlasIndex::Find(name) — 플립북이면 이름 접미사 프레임
        int           frameCount{ 1 };       // 1 = 정지 스프라이트. >1 = spriteName0..N 순차 재생
        float         framesPerSecond{ 24.0f };
        float         gravityScale{ 0.0f };  // 0 = 중력 무시(연기), >0 = 불티/파편
        float         dragPerSec{ 0.0f };    // 속도 감쇠(연기 상승 감속에 사용)
        float         lifeMin{ 0.3f }, lifeMax{ 0.6f };
        float         speedMin{ 1.0f }, speedMax{ 2.0f };
        float         spreadDeg{ 15.0f };    // 방향 콘 각도(0 = 완전 직선)
        int           burstMin{ 4 }, burstMax{ 8 };
    };

    inline constexpr ParticleEffectDef kMuzzleFlash{
        .blend = BlendMode::Additive, .spriteName = "vfx_flash", .frameCount = 1,
        .gravityScale = 0, .dragPerSec = 0, .lifeMin = 0.03f, .lifeMax = 0.05f,
        .speedMin = 0, .speedMax = 0, .spreadDeg = 0, .burstMin = 1, .burstMax = 2 };

    inline constexpr ParticleEffectDef kMuzzleSmoke{
        .blend = BlendMode::AlphaBlend, .spriteName = "vfx_smoke", .frameCount = 1,
        .gravityScale = -0.15f /* 부양 */, .dragPerSec = 0.5f,
        .lifeMin = 0.4f, .lifeMax = 1.0f, .speedMin = 0.3f, .speedMax = 0.8f,
        .spreadDeg = 25.0f, .burstMin = 2, .burstMax = 4 };

    inline constexpr ParticleEffectDef kExplosionFlash{ /* Additive, 큼, life 0.1s, burst 1 */ };
    inline constexpr ParticleEffectDef kExplosionFireball{ /* Additive, frameCount 8 flipbook, life 0.3~0.5s, burst 10~16 */ };
    inline constexpr ParticleEffectDef kExplosionSmokeStem{ /* AlphaBlend, life 2~3s, speed 큼, drag 큼(감속→갓 전환 재료) */ };
    inline constexpr ParticleEffectDef kExplosionEmbers{ /* Additive, gravityScale 1, life 0.5~1s */ };
}
```

- **데이터 파일이 아니라 C++ constexpr 테이블**로 시작 — 이 엔진 관행(애니메이션 클립 매니페스트,
  크라우드 설정 전부 C++ 테이블, 별도 애셋 파이프라인 없음)과 일치. 이펙트 종류가 많아지고
  기획이 직접 튜닝해야 하면 그때 파일화(YAGNI).
- `spriteName`은 §3의 `vfx/` 아틀라스 그룹에서 찾는다. 플립북은 `"vfx_fire_00".."vfx_fire_07"`
  처럼 이름 접미사 규약(간단), 또는 아틀라스 매니페스트에 그리드 메타를 추가하는 대안도 가능
  (§9 판단 필요).

### 4.3 스냅샷 값 타입 (`render/r3d/Scene3D.h` 확장)

```cpp
namespace engine::render
{
    // 빌보드 파티클 하나. VS 가 SV_VertexID 로 코너를 만들고 camRight/camUp*size 로 오프셋.
    struct ParticleInstance
    {
        math::Vec3    pos;         // 월드                         (offset 0)
        float         size;        // 반경(빌보드 half-extent)     (offset 12)
        float         rotation;    // 화면 평면 롤, 라디안          (offset 16)
        std::uint32_t colorRgba;   // 8:8:8:8, PS 에서 언팩         (offset 20)
        std::uint32_t uvRect;      // u0,v0,u1,v1 각 8bit 정규화    (offset 24)
    };                             // 28 bytes — MeshInstance 와 같은 예산 규약(§3 표)

    enum class ParticleBlend : std::uint8_t { Additive, AlphaBlend };

    // 같은 텍스처·같은 블렌드 모드인 ParticleInstance 들의 연속 구간.
    struct ParticleBatch
    {
        std::uint32_t atlasId{ 0 };
        ParticleBlend blend{ ParticleBlend::AlphaBlend };
        std::uint32_t first{ 0 };
        std::uint32_t count{ 0 };
    };

    struct Scene3D
    {
        // ...기존...
        std::vector<ParticleInstance> particleInstances;
        std::vector<ParticleBatch>    particleBatches;
    };
}
```

- `uvRect`를 8bit×4로 패킹해 28B를 유지(`MeshInstance`와 동일 크기 — 일관성, 캐시 친화적).
  아틀라스 좌표는 대부분 저정밀도로 충분(스프라이트 경계가 128~512px 격자).
- **정렬은 `SnapshotBuilder`가 배열에 채우기 전에 한다**(§6.1) — 렌더 스레드는 순서 그대로
  그린다. `AlphaBlend` 배치만 카메라 거리 내림차순, `Additive`는 순서 무관(교환법칙).

---

## 5. 렌더 — 새 패스 `render/r3d/ParticlePass3D`

**결정: `MeshPass3D` 확장이 아니라 새 패스.** 이유(호드 설계 §2와 동일 논리): 정점 포맷이
근본적으로 다르고(빌보드는 정점 데이터가 사실상 없음 — `SV_VertexID`로 코너 생성), 조명을 안
받고(자체발광), 블렌드 모드가 2종(가산/알파) 왔다갔다 하며, 깊이 **쓰기는 끄고 테스트는 켠 채**
그린다. `MeshPass3D`에 이 전부를 끼워 넣으면 그 패스의 "불투명·조명 받음" 전제가 깨진다(OCP).

### 5.1 초기화

```cpp
// ParticlePass3D::Initialize(device, shaders)
// 1. 빌보드는 정점 버퍼가 없다 — VS 가 SV_VertexID(0..3) 로 로컬 코너 (-1,-1)(1,-1)(-1,1)(1,1) 생성.
//    IB 도 필요 없음 (DrawIndexedInstanced 대신 DrawInstanced, 4 verts/quad, 삼각형 스트립 또는
//    6-vert 트라이앵글리스트로 디제너레이트 없이 SV_VertexID 0..5 매핑).
// 2. per-instance DYNAMIC 버퍼 (kMaxParticles, 예: 4096) — stride 28.
// 3. 블렌드 스테이트 2개: 가산(SrcBlend=SRC_ALPHA 또는 ONE, DestBlend=ONE, Op=ADD),
//    알파(기존 SrcAlpha/InvSrcAlpha 재사용 가능한 값으로 별도 생성 — 패스가 자기 걸 소유).
// 4. 깊이 스텐실 스테이트: DepthEnable=TRUE, DepthWriteMask=ZERO (씬 지오메트리에 가려지되
//    서로/자신은 안 씀).
// 5. 래스터라이저: CullMode=NONE(빌보드는 항상 카메라를 보므로 백페이스 없음), MSAA on.
// 6. shaders.Get(device, "particle", instancedLayout, ...) — assets/shaders/particle.hlsl
```

### 5.2 인스턴스 입력 레이아웃 (slot 0, 정점 버퍼 없이 인스턴스 스트림만)

```cpp
const D3D11_INPUT_ELEMENT_DESC particleLayout[] = {
    // 정점 버퍼 없음 — SV_VertexID 로 코너 생성. per-instance 만 slot 0.
    { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // pos
    { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // size
    { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // rotation
    { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // colorRgba
    { "COLOR",    1, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // uvRect(8bit x4)
};
```

### 5.3 `Execute` — 배치 루프

```text
for each ParticleBatch b (particleBatches 순서 = SnapshotBuilder 가 이미 정렬/그룹핑):
    OMSetBlendState(b.blend == Additive ? m_additiveBlend : m_alphaBlend)
    PSSetShaderResources(0, 1, &m_atlasSrv[b.atlasId])
    IASetVertexBuffers(0, 1, &m_instanceBuffer, stride=28, offset=b.first*28)
    DrawInstanced(6 /* 2 tri */, b.count, 0, 0)
```

블렌드 스테이트 전환이 배치 경계마다 있을 수 있으나(가산↔알파), 이펙트 수가 적어(엔진당 수~수십
배치) 비용은 무시할 만하다. 크게 늘면 배치를 블렌드별로 먼저 정렬(§4.3에서 이미 그룹핑).

### 5.4 셰이더 (`assets/shaders/particle.hlsl`)

```hlsl
#include "common3d.hlsli"   // Frame cbuffer(b0) — camRight/camUp 필드 추가 필요 (§3)

Texture2D atlasTex : register(t0);
SamplerState samp : register(s0);

struct VSIn {
    uint vid : SV_VertexID;
    float3 ipos : TEXCOORD1; float isize : TEXCOORD2; float irot : TEXCOORD3;
    float4 icol : COLOR0; float4 iuv : COLOR1;   // iuv = u0,v0,u1,v1 (0..1 로 언팩됨, UNORM 포맷)
};
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };

static const float2 kCorner[6] = {
    float2(-1,-1), float2(1,-1), float2(-1,1),
    float2(-1,1),  float2(1,-1), float2(1,1)
};

VSOut VSMain(VSIn i)
{
    float2 c = kCorner[i.vid];
    float s = sin(i.irot), co = cos(i.irot);
    float2 rc = float2(c.x*co - c.y*s, c.x*s + c.y*co);      // 화면 평면 롤
    float3 worldOffset = (camRight * rc.x + camUp * rc.y) * i.isize;
    VSOut o;
    o.pos = mul(float4(i.ipos + worldOffset, 1), viewProj);
    o.uv  = lerp(i.iuv.xy, i.iuv.zw, (c + 1) * 0.5);
    o.col = i.icol;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET
{
    float4 tex = atlasTex.Sample(samp, i.uv);
    return tex * i.col;   // 가산/알파 구분은 블렌드 스테이트가 처리, 셰이더는 동일
}
```

- **조명 없음** — 파티클은 자체발광(플래시·불) 또는 주변광에 무관하게 튀는 연기라 `ApplyLighting`
  호출 안 함. 필요해지면(연기가 주변 조명 색을 받게) `common3d.hlsli`의 ambient 항만 곱하는
  절충안 가능 — v1은 생략.
- **셰도우 없음** — 파티클은 그림자를 드리우지도 받지도 않는다(관행, 비용 대비 이득 낮음).

### 5.5 패스 등록 순서

`main.cpp`에서 `MeshPass3D`/`ModelMeshPass3D`(불투명, 셰도우 포함) **다음**, `QuadPass2D`/
`SpritePass2D`(2D 오버레이/UI) **이전**에 추가— 3D 월드 알파 이펙트는 불투명 뒤·UI 앞이 관례.

---

## 6. 시뮬레이션 (`game/vfx/ParticleSystem`)

### 6.1 구조

```cpp
namespace engine::game::vfx
{
    class ParticleSystem final : private core::NonCopyable
    {
    public:
        static constexpr std::size_t kCapacity = 4096;   // 측정 게이트로 조정 (§8)
        explicit ParticleSystem(core::JobSystem& jobs);

        // --- 스텝 사이(메인 스레드)에만 (규칙 6) ---
        void SpawnBurst(const ParticleEffectDef& def, math::Vec3 pos, math::Vec3 dir);

        // --- 고정 스텝 ---
        void Step(float dt);

        // --- 읽기 (SnapshotBuilder) ---
        const core::ObjectPool<Particle>& Pool() const { return m_pool; }

    private:
        core::JobSystem& m_jobs;
        core::ObjectPool<Particle> m_pool{ kCapacity };
        std::vector<ParticleEffectDef> m_effects;   // kMuzzleFlash 등을 인덱스로 (effectId)
    };
}
```

- **이펙트 종류를 몰라야 하는 건 렌더러 쪽**(OCP/DIP — `ParticlePass3D`는 `ParticleInstance`만
  안다). **`ParticleSystem` 자체도 "머즐"이나 "폭발"을 모른다** — `SpawnBurst(def, pos, dir)`
  하나만 알고, "무엇을 스폰할지"는 호출자가 `ParticleEffectDef`로 결정한다(SRP: 정책은 §4.2
  테이블에, 메커니즘은 여기).
- **연속 이미터는 v1 범위 밖**(총구/폭발 둘 다 버스트로 충분 — §2 참고). 필요해지면
  `SpawnBurst`를 매 스텝 소량씩 호출하는 헬퍼(`EmitterHandle` + 누적 레이트)를 얹는다.

### 6.2 `Step` — `ParallelFor`

```text
active = pool.ActiveIndices()
jobs.ParallelFor(0, active.size(), 64, [&](begin, end) {
    for k in [begin, end):
        Particle& p = pool.Slots()[active[k]]
        const ParticleEffectDef& def = m_effects[p.effectId]
        p.age += dt
        p.vel += Vec3{0, -kGravity * def.gravityScale, 0} * dt
        p.vel *= max(0, 1 - def.dragPerSec * dt)
        p.pos += p.vel * dt
        p.rotation += p.angularVel * dt
        // 버섯구름 전용 상태 전이 (§7.2) — effectId 로 분기, 다른 이펙트는 phase 안 씀
}).Wait()

// 스텝 밖(메인): 죽은 파티클 Release
for i in active: if (pool.Slots()[i].age >= pool.Slots()[i].life) pool.Release(handle-of(i))
```

- 워커는 자기 파티클 칸만 쓴다(규칙 6). `Release`는 `Wait()` 뒤 메인에서만 — 크라우드의
  churn 패턴과 동일.
- **`m_effects` 는 읽기 전용 상수 테이블**이라 워커에서 읽어도 안전(공유 가변 상태 아님).

---

## 7. 총구/폭발 — 조합 계층 (`game/vfx/WeaponVfx.h`, `game/vfx/ExplosionVfx.h` 등)

`ParticleSystem`은 "머즐"이나 "폭발"을 모르므로, 그 의미는 얇은 조합 함수가 담당한다
(SRP — 게임 개념 ↔ 파티클 프리미티브 번역 계층, `SnapshotBuilder`가 게임 개념을 렌더 프리미티브로
번역하는 것과 같은 패턴).

### 7.1 총구 이펙트

```cpp
// 격발 이벤트가 오면 (Simulation 또는 Application, 무기 시스템이 아직 없으므로 호출 지점은 추후)
void SpawnMuzzleFlash(ParticleSystem& vfx, math::Vec3 muzzlePos, math::Vec3 muzzleDir)
{
    vfx.SpawnBurst(kMuzzleFlash, muzzlePos, muzzleDir);
    vfx.SpawnBurst(kMuzzleSmoke, muzzlePos, muzzleDir);
}
```

`muzzlePos`/`muzzleDir`은 **총구 소켓의 월드 트랜스폼**이 필요하다 — 지금 엔진엔 무기 부착
소켓/본 트랜스폼을 게임 코드에 노출하는 API가 없다(`ModelMeshPass3D`가 스키닝 팔레트를 내부에
숨김). §9에 열린 질문으로 남긴다.

### 7.2 폭발 — 버섯구름 (상태 전이가 필요한 유일한 지점)

```cpp
enum class MushroomPhase : std::uint8_t { Rising, Capping };   // Particle::phase 에 저장

void SpawnExplosion(ParticleSystem& vfx, math::Vec3 groundPos)
{
    vfx.SpawnBurst(kExplosionFlash, groundPos, {0,1,0});
    vfx.SpawnBurst(kExplosionFireball, groundPos, {0,1,0});
    vfx.SpawnBurst(kExplosionSmokeStem, groundPos, {0,1,0});   // phase = Rising 로 스폰
    vfx.SpawnBurst(kExplosionEmbers, groundPos, {0,1,0});
    // 강체 파편은 여기서 안 함 — instanced-rendering.md 의 MeshInstance 경로로 별도 호출
}
```

`ParticleSystem::Step`의 `kExplosionSmokeStem` 분기(전체 파티클을 순회하되 `effectId`로 걸러진
소수에만 적용, 다른 이펙트엔 비용 0):

```text
if def == kExplosionSmokeStem and phase == Rising:
    if p.pos.y - spawnY > kCapHeight:
        phase = Capping
        p.vel.xz = normalize(p.vel.xz 또는 랜덤 방위) * kCapOutSpeed   // 반경 방향으로 전환
        p.vel.y *= 0.2                                                 // 수직 속도 급감
    // Rising 이든 Capping 이든 age/life 는 공통으로 진행(§6.2), size 는 age 에 따라 계속 성장
```

**왜 이게 버섯 모양을 만드나**(§2.2에서 예고한 것의 구현): 초반에 스폰된 파티클일수록 먼저
`kCapHeight`에 도달해 먼저 갓으로 전환 → 시간이 지날수록 갓이 아래서부터 채워지며 자란다. 늦게
스폰된 파티클(버스트 마지막 쪽)은 아직 줄기를 오르는 중이라 기둥이 유지된다. 하나의 버스트 +
하나의 `if` 분기로 "줄기가 서고 위에서 갓이 퍼지는" 그림이 나온다 — 별도 상태 머신 프레임워크
불필요.

- `spawnY`는 `Particle`에 없다(공간 절약) — `pos.y - groundPos.y`로 근사하거나, 정밀함이
  필요하면 `Particle`에 4바이트 추가(28→32B, 예산 여유 있음, §4.1 재검토 지점).
- 파편(돌·잔해)은 **파티클이 아니라 강체 인스턴스**로 별도 스폰 —
  [instanced-rendering.md](instanced-rendering.md)의 `MeshInstance` 경로를 그대로 쓰고, 탄도는
  중력 적분 + (옵션) `CollisionWorld3D` 레이캐스트로 착지 판정(탐지만, 규칙 8 — 응답은 이
  파편 스텝 코드 안에서).

---

## 8. 스레드 / 불변 규칙 매핑

| 규칙 | 이 설계에서 |
|---|---|
| 1 (D3D11 은 렌더 스레드만) | 인스턴스 버퍼 `CreateBuffer`/`Map`, 블렌드/뎁스/래스터 스테이트, 아틀라스 SRV 전부 `ParticlePass3D` 안. `ParticleSystem`/`SnapshotBuilder`는 값만 |
| 2 (렌더러 코어는 clear/bind/pass 순회만) | 새 그리기는 `ParticlePass3D::Execute` 안. 코어 불변 |
| 3 (경계는 값 스냅샷만) | `ParticleInstance`(28B POD) + `ParticleBatch`. `Particle`(시뮬 상태)·`ParticleSystem` 포인터는 안 넘어감 |
| 4 (1슬롯 메일박스) | `kCapacity=4096` 이면 최대 ~112KB/프레임 — 무해 |
| 6 (`ParallelFor` 범위 독립) | 워커는 `ActiveIndices()`의 자기 `[begin,end)`만 쓰기. `Acquire`/`Release`(스폰/디스폰)는 `Wait()` 뒤 메인만 |
| 7 (2D/3D 분리) | 전부 `render/r3d`·`math/Math3D` 만 사용, `ENGINE_WITH_3D`로 감쌈. 2D 전용 파티클(2D 게임 UI 이펙트 등)이 필요해지면 `render/r2d`에 별도 경량 패스 — 이 문서 범위 밖, `math/Math2D`만 참조하는 완전히 다른 타입이어야 함(공유 금지) |
| 8 (충돌은 탐지만) | 파편(강체)이 지면 충돌을 쓴다면 `CollisionWorld3D`는 탐지만, 정지/바운스는 파편 스텝 코드 안(physics 밖) |

---

## 9. 구현 순서 제안 (측정 게이트마다 멈춤 — 실제 구현 시)

1. **스냅샷 타입 + `ParticlePass3D` 최소본 + `particle.hlsl`.** 카메라 축을 `Frame` cbuffer에
   추가(§3). 하드코딩된 흰 사각형 텍스처로 가산 블렌드 파티클 몇 개 고정 스폰 → 빌보드가
   카메라를 향하는지, 블렌드가 맞는지만 검증.
2. **`core::ObjectPool<Particle>` + `ParticleSystem::Step`(중력/드래그) + `SpawnBurst`.**
   `SnapshotBuilder`가 `ActiveIndices()` → `ParticleInstance` 변환 + 알파 배치만 거리 정렬.
3. **`vfx/` 아틀라스 그룹**(`tools/atlas_pack --group vfx`) — 플래시/스모크/파이어 스프라이트.
   `ParticleEffectDef::spriteName` → `AtlasIndex::Find` 실제 연결.
4. **`kMuzzleFlash`/`kMuzzleSmoke`** — 격발 이벤트 연동. 이 시점에 총구 소켓 트랜스폼 문제(§7.1)
   를 풀어야 함 — 없으면 임시로 캐릭터 전방 고정 오프셋으로 근사.
5. **`kExplosion*` 4종 + 버섯구름 phase 전이(§7.2).** 파편은 별도(4c 이후, instanced-rendering
   경로 재사용).
6. (선택) 플립북(`frameCount>1`) 재생, 소프트 파티클(§9 아래), 서브이미터.

각 단계 독립 커밋 가능, 언제든 "지금 규모로 충분"하면 멈춰도 된다(다른 설계 문서와 동일 원칙).

---

## 10. 사용 방법 (How to use) — *구현 후 기준*

### 10.1 새 이펙트 추가

```cpp
// game/vfx/ParticleEffects.h 에 한 줄
inline constexpr ParticleEffectDef kRocketTrail{
    .blend = BlendMode::AlphaBlend, .spriteName = "vfx_smoke", .frameCount = 1,
    .gravityScale = -0.05f, .dragPerSec = 0.3f, .lifeMin = 0.5f, .lifeMax = 0.8f,
    .speedMin = 0.1f, .speedMax = 0.3f, .spreadDeg = 10.0f, .burstMin = 1, .burstMax = 1 };
```

정의만 추가하면 끝 — `ParticleSystem`/`ParticlePass3D`는 안 건드림(OCP). 스프라이트가 아틀라스에
없으면 `AtlasIndex::Find`가 `nullptr` → 폴백 흰 텍스처(경고 로그, 크래시 아님).

### 10.2 게임 코드에서 발사

```cpp
// 격발
vfx::SpawnMuzzleFlash(m_particles, muzzleWorldPos, muzzleForward);

// 폭발
vfx::SpawnExplosion(m_particles, impactPos);
```

`Simulation::Step` 안, 이벤트가 발생한 그 스텝에서 호출(다른 스텝 부수효과와 같은 타이밍 규칙,
`time-design.md`).

### 10.3 튜닝 위치

| 무엇 | 어디 |
|---|---|
| 파티클 풀 용량 | `ParticleSystem::kCapacity` |
| 이펙트별 색/수명/속도/블렌드 | `game/vfx/ParticleEffects.h` 각 `ParticleEffectDef` |
| 버섯구름 갓 전환 높이/반경 속도 | `ParticleSystem::Step`의 `kCapHeight`/`kCapOutSpeed` (또는 `ParticleEffectDef`에 필드로 승격) |
| 알파 배치 정렬 기준 | `SnapshotBuilder`의 파티클 빌드 함수 |
| 카메라 축 계산 | `Dx11Renderer`의 `FillFrameConstants`(또는 그 헬퍼) |

### 10.4 하지 말 것

```cpp
// ✗ 파티클을 MeshDraw/MeshInstance 로 — 조명 계산·불투명 셰이더가 붙어 비용 낭비, 빌보드도 안 됨
scene.meshDraws.push_back({...});                      // 금지

// ✗ ParallelFor 워커 안에서 Acquire/Release/vector 재할당 (규칙 6)
jobs.ParallelFor(0, n, c, [&](b,e){ for(...) pool.Release(h); });   // 금지 — 스텝 밖에서

// ✗ 알파 배치를 정렬 안 하고 그리기 — 겹치는 연기가 뒤집혀 보임(가산은 무관, 알파만 필수)

// ✗ ParticlePass3D 가 다른 배치의 uvRect 조회를 이름으로 렌더 스레드에서 하기
//    — UV resolve 는 반드시 SnapshotBuilder(메인)에서 (texture-atlas 문서 §5.5 원칙과 동일)

// ✗ particle.hlsl 을 인라인 D3DCompile — assets/shaders/ + shaders.Get(...)

// ✗ Particle::phase 를 이펙트 무관하게 범용 상태머신으로 일반화하려 하지 않기
//    — 지금은 버섯구름 하나뿐. 두 번째 사례가 생기면 그때 일반화(YAGNI)
```

---

## 11. 판단 필요 / 열린 질문

- **소프트 파티클(깊이 기반 페이드)**: 스모크가 지면/벽과 만나는 딱딱한 경계를 없애려면 PS 가
  씬 depth 를 읽어야 하는데, 지금 depth buffer 는 그 프레임에 아직 DSV 로 바인드 중(같은 depth
  를 SRV 로 동시에 읽을 수 없음). 방법: (a) depth prepass 후 별도 SRV 로 복사, (b) 파티클 패스
  전용 depth-copy 1회. 비용 대비 필요성 낮으면(카메라가 파티클에 안 붙는 게임이면) v1은 생략.
- **가산 블렌드가 HDR 없이 밝게 번지나**: 이 엔진은 백버퍼가 `R8G8B8A8_UNORM`(LDR) —
  가산이 1.0에서 클램프된다. 폭발 화구가 밋밋해 보이면 톤매핑/블룸(`msaa.md` "다음"의 포스트
  패스 인프라와 함께)이 필요 — 이 문서 범위 밖, 열어둠.
- **아틀라스 플립북 메타데이터**: `spriteName0..N` 접미사 규약 vs `.atlas`에 그리드 정보
  (`frameGrid: cols rows`) 추가. 후자가 더 정직하지만 `atlas_pack`/`AtlasIndex` 확장이 필요
  (`texture-atlas-and-sprite-pass.md` 변경). 이펙트 수가 적을 땐 전자로 충분.
- **총구 소켓 트랜스폼**: 무기 부착점의 월드 변환을 게임 코드가 얻을 방법이 지금 없다
  (`ModelMeshPass3D`가 본 팔레트를 내부에 숨김). 무기 시스템이 생길 때 "본 이름 → 월드 행렬
  질의" API를 `ModelMeshPass3D`나 `game::CharacterAnimationState`에 추가해야 함 — 별도 설계
  필요, 이 문서는 존재만 표시.
- **컴퓨트 셰이더 시뮬레이션 전환 시점**: 파티클이 수만 개 규모(대규모 파괴, 화면 가득 찬 연기)
  로 커지면 CPU `ParallelFor`가 병목 — 그때 `command-playbook.md` 3f(컴퓨트 지원)와 합류해
  GPU 시뮬레이션 재검토. 지금 규모(총구 수십, 폭발 수백)는 CPU로 충분.
- **파티클 수 상한과 우선순위**: 풀이 꽉 찼을 때 새 버스트가 밀려나야 하는지(오래된 것 강제
  회수) 아니면 스폰 실패를 무시할지 — 크라우드의 `Acquire` 실패 시 무효 핸들 반환 관행과
  동일하게 "무시"로 시작, 눈에 띄게 파티클이 씹히면 강제 회수(가장 나이 많은 것부터) 추가.

---

## 12. 관련 문서

- [instanced-rendering.md](instanced-rendering.md) — 인스턴스 배열+배치+`DrawIndexedInstanced` 패턴의 원본, `MeshInstance` 예산 규약(28B)을 그대로 따름
- [horde-design.md](horde-design.md) — "정점 포맷이 다르면 새 패스" 판단 선례, SoA+풀+`ParallelFor` 계약
- [texture-atlas-and-sprite-pass.md](texture-atlas-and-sprite-pass.md) — `vfx/` 아틀라스 그룹으로 재사용, UV resolve는 메인 스레드 원칙
- [atlas-build-pipeline.md](atlas-build-pipeline.md) — `tools/atlas_pack --group vfx` 빌드 경로
- [image-assets.md](image-assets.md) — 텍스처 디코드 진입점, dev/ship 경계
- [shader-pipeline.md](shader-pipeline.md) — `particle.hlsl` 로딩 규약
- [msaa.md](msaa.md) — 씬 타깃에 그리기, 리사이즈 불변
- [time-design.md](time-design.md) — 고정 스텝에서 파티클 전진(불변 규칙 4)
- [multithreaded_game_engine_architecture.md](multithreaded_game_engine_architecture.md) — `JobSystem`, 스냅샷 경계
- [collider-design.md](collider-design.md) — 파편 지면 충돌 시 탐지-only 경계
- [command-playbook.md](command-playbook.md) — 2(게임 시스템), 2f(인스턴싱), 3b(새 패스), 3f(컴퓨트, 향후)
