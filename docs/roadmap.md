# 로드맵 (Roadmap) — 다음에 뭘

각 시스템 문서의 "지을 것 / 미구현 / 로드맵" 을 한 곳에 모으고 우선순위를 매긴다.
`command-playbook.md` 가 "명령 → 처리" 라면 이 문서는 **"아직 안 된 것과 순서"**.
항목이 착수되면 그 시스템 문서로 상세를 옮기고 여기선 링크만 남긴다.

관련: `docs/engine-conventions.md`(불변값), `docs/synopsis.md`(장르 — 여러 항목의 선행 조건), 각 시스템 문서.

---

## 1. 현재 상태 한 눈에

| 영역 | 됨 | 부분/설계 | 없음 |
|---|---|---|---|
| 렌더 | MSAA 씬타깃, 셀+아웃라인+크리즈, 방향광 1개 + 단일 셰도우맵, MeshPass3D(큐브/평면 + **인스턴스드 드로우** + 프러스텀·거리 컬), QuadPass2D, 디버그 드로우 패스 | SpritePass2D + 아틀라스(무압축), ModelMeshPass3D(정적+CPU스키닝) | 포인트/스팟광, CSM, 투명 정렬, 인스턴스 LOD/빌보드, sRGB 파이프라인, 메시 LOD |
| 애니메이션 | CPU LBS 스키닝, 클립 리타깃, Locomotion→클립 스냅, 재생 모드(Once/PingPong), 크로스페이드(로컬 TRS lerp), 파라메트릭 점프 | 2D 프레임 애니(설계만) | **루트 모션, GPU 스키닝, IK, 블렌드 트리** |
| 물리/충돌 | Box/Sphere 탐지, layer/mask, Contacts, 레이캐스트 3D(Closest/Any/All) | — | **레이캐스트 2D, `Simulation` 연동, 브로드페이즈, 스윕/CCD, 캡슐, 트리거 enter/exit 이벤트, 재사용 캐릭터 컨트롤러** |
| 에셋 | FBX+스키닝, 이미지 디코드 seam, atlas_pack v1(무압축) | — | BC7 압축, AssetRegistry, 비동기 로더, 핫리로드(아틀라스/모델), 글리프 아틀라스 |
| 게임 프레임워크 | 씬 상태(Title/InGame/Settings), 고정 스텝 + time scale, EntityId 뼈대, 오디오 최소 믹서(XAudio2) | ScrollList v1 | **오디오 스트리밍/3D음, 세이브, 이벤트 버스, 프리팹/직렬화, 게임 루프(장르 미정)** |
| 입력 | 키보드/마우스/휠 | — | 게임패드(XInput), 리바인딩, 액션맵 레이어 |
| 툴/디버그 | entity 메모리 벤치, atlas_pack | — | 프레임타임 HUD/프로파일러, 인게임 콘솔, 엔티티 인스펙터, 리플레이 |

---

## 2. 이번에 요청된 것 — 미니 설계

### 2.1 애니메이션 재생 모드 + 자연스러운 점프 — ✅ 구현

`anim::PlayMode{Loop,Once,PingPong}` (`AnimationSampler::Evaluate` 인자, 기본 Loop) + `AnimationSampler::EvaluateBlended`(로컬 TRS lerp 크로스페이드) + `CharacterAnimationState::UpdateParametric`(phase 0..1 → 클립 시간) + 전환 시 0.15s 크로스페이드. 점프는 `Simulation::StepOneActor` 가 `jumpPhase = clamp(0.5 - 0.5*(verticalVel/kCharJumpSpeed))` 로 파라메트릭 구동(이륙 0 · 정점 0.5 · 착지 1). `Scene3D::ModelDraw` 에 `animPlayMode`/`animParametric`/`animFrom*`/`animBlend` 추가, `ModelMeshPass3D::UpdateSkinningForFrame` 가 분기. 상세는 착수 완료 후 `animation-design.md` §5 로 이관 예정.

**(원 설계 — 참고)**

**문제**: 공중에 있는 동안 `Locomotion::Jump` → `CharacterAnimationState` 가 점프 클립을 **루프**(`AnimationSampler` 가 `fmod(t, duration)`)한다. 실제 체공 시간과 클립 길이가 안 맞아 (a) 짧으면 착지 포즈에 도달 못 하고 (b) 길면 점프 애니가 반복된다. 전환도 스냅(블렌드 없음).

**설계**:

1. **`anim::PlayMode`** — 시간→샘플 시간 매핑 방식.
   ```cpp
   enum class PlayMode : std::uint8_t { Loop, Once, PingPong };
   // Loop     : t' = fmod(t, D)                       (현재 동작)
   // Once     : t' = min(t, D)                        (끝에서 홀드)
   // PingPong : p = fmod(t, 2D); t' = p<D ? p : 2D-p  (0→D→0 왕복)
   ```
   `AnimationSampler::Evaluate(skeleton, clip, timeSeconds, out, PlayMode = Loop)` 에 인자 추가(기본값이라 기존 호출부 불변). 매핑은 `Evaluate` 안 한 줄.

2. **파라메트릭 드라이브** — 클립 시간을 벽시계가 아니라 **게임플레이 파라미터**로. `CharacterAnimationState` 에 모드 추가:
   ```cpp
   void Update(float dt, Locomotion desired);                 // 기존: 자동 진행
   void UpdateParametric(Locomotion desired, float phase01);  // 신규: clipTime = phase01 * clipDuration
   ```
   `phase01` 은 0..1. `RenderSnapshot` 으로는 여전히 `(clipIndex, clipTime)` 만 나간다 — `PlayMode` 는 `clipTime` 을 만들 때만 쓰이므로 스냅샷/렌더 계약 불변.

3. **점프 적용** (`Simulation::StepOneActor`):
   ```cpp
   // vel = +kCharJumpSpeed 이륙 → 0 정점 → -kCharJumpSpeed 착지 직전
   const float jumpPhase = math::Clamp(0.5f - 0.5f * (actor.verticalVel / kCharJumpSpeed), 0.0f, 1.0f);
   if (!actor.grounded) actor.anim.UpdateParametric(Locomotion::Jump, jumpPhase);
   else                 actor.anim.Update(dt, loco);
   ```
   점프 클립의 앞 절반 = 웅크림→도약→상승, 뒤 절반 = 하강→착지. `phase01` 이 아크를 따라가므로 체공 시간과 무관하게 자연스럽다. 대칭(상승 포즈만 있는) 클립이면 `PlayMode::PingPong` 로 0→1→0.

4. **크로스페이드(별도, P0)** — 상태 전환 시 두 클립 팔레트를 `blend` 로 lerp(`animation-design.md` §5). 점프 진입/이탈뿐 아니라 walk↔run↔wait 스냅도 이걸로 해결. `CharacterAnimationState` 가 전환 시 이전 `(clipIndex, clipTime)` + `blendT ∈ [0,1]` 를 스냅샷에 추가로 실어, 렌더가 두 번 평가해 섞는다.

착수 시 → `docs/animation-design.md` §5 로 상세 이동.

### 2.2 레이캐스트 + 디버그 드로우 — ✅ 구현 (3D. 2D 대칭·`Simulation` 연동만 남음)

- **레이캐스트**: `physics/p3d` 에 `Ray3D` + `RayHit3D` + `RaycastCollider`(Ray-Box slab / Ray-Sphere) + `CollisionWorld3D::{RaycastClosest, RaycastAny, RaycastAll}`(선형 스캔, `RaycastAll` 거리순). "일정 거리 안" = `maxDistance`. 상세 `collider-design.md` "레이캐스트".
- **디버그 드로우**: `render/r3d/DebugDrawPass` + `Scene3D::debugLines`(`DebugLine{a,b,color}`) + `render::debug::{Line,Box,Sphere,Ray}` 헬퍼(`Scene3D.h`, 헤더 전용) + `debugline.hlsl`(월드 라인리스트, depth-test/no-write). `SnapshotBuilder` 가 캐릭터 AABB + 전방·지면 레이를 임시 방출.
- **남음**: 2D 대칭(`Ray2D`/`RayHit2D`), `Simulation`↔`CollisionWorld3D` 연동(현재 `EntityRegistry` 처럼 미연결 — 디펜스 타워 타겟팅에서 처음 쓰임).

**(원 설계 — 참고)**

**용도**: 지면 검사(현재 `y<=0` 하드코딩 대체), 시야/LOS, 투사체, "바라보는 대상" 상호작용 타겟팅, 카메라 벽 충돌.

**설계** (`physics/p3d` + `physics/p2d` 대칭, `docs/collider-design.md` 확장):

```cpp
// physics/Collision.h (공유 타입)
struct RayHit
{
    ColliderId    id{};
    std::uint64_t user{};        // 콜라이더의 user (보통 엔티티 id)
    float         distance{};    // origin 에서 hit 까지
    math::Vec3    point{};       // 3D. 2D 는 별도 RayHit2D { math::Vec2 point; ... }
    math::Vec3    normal{};      // 맞은 면의 바깥 방향
};

// physics/p3d/CollisionWorld3D.h
struct Ray3D
{
    math::Vec3 origin{};
    math::Vec3 dir{};                          // 정규화되어 들어온다(호출부 책임)
    float      maxDistance{ 3.4e38f };         // 이 거리까지만
    CollisionLayer mask{ kAllLayers };
    ColliderId ignoreId{ kInvalidCollider };   // 자기 자신 제외
};

// ── 3개 질의 ──
[[nodiscard]] std::optional<RayHit> RaycastClosest(const Ray3D&) const;          // ① 가장 가까운 1개
[[nodiscard]] bool RaycastAny(const Ray3D&) const;                              // ①' 맞았나만 (LOS, 빠름)
void RaycastAll(const Ray3D&, std::vector<RayHit>& out) const;                  // ② 전부 (distance 오름차순)
```

- **"일정 거리 안" (③)** = `Ray3D::maxDistance` 로 `RaycastClosest`/`RaycastAll` 을 제한한 것. 별도 API 아님.
- 교차: **Ray vs Box** = slab 법(`(min-o)/d`, `(max-o)/d` 의 `tmin/tmax`), **Ray vs Sphere** = 2차방정식. `dir` 성분 0 은 분모 가드. `t < 0` 또는 `t > maxDistance` 는 miss.
- 필터: `LayersInteract` 대신 `ray.mask & collider.layer` (레이는 레이어가 없으므로 단방향). `ignoreId` 스킵.
- **탐지만·메인 스레드만** — `CollisionWorld` 규칙 그대로. 콜라이더를 안 움직인다.
- 브로드페이즈 없으니 v1 은 **선형 스캔**(전 콜라이더 순회). 브로드페이즈(P1) 붙으면 레이 AABB 로 후보만.
- 2D: `Ray2D { origin: Vec2, dir, maxDistance, mask, ignoreId }` + `RayHit2D` + 같은 3 API. Box/Circle 교차.

**사용 예** (지면 검사):
```cpp
Ray3D down{ .origin = actor.pos + Vec3{0, 0.1f, 0}, .dir = {0,-1,0},
            .maxDistance = 0.2f, .mask = kLayerScenery, .ignoreId = actor.colliderId };
if (auto hit = world.RaycastClosest(down)) { actor.pos.y = hit->point.y; actor.grounded = true; }
```

착수 시 → `docs/collider-design.md` 에 "레이캐스트" 절 + "사용 방법".

---

## 3. 우선순위 로드맵

### P0 — 곧 필요 / 지금 막힘

| 항목 | 왜 | 문서 | 규모 |
|---|---|---|---|
| ~~애니 재생 모드(Once/PingPong) + 파라메트릭 점프 + 크로스페이드~~ ✅ | 요청. 점프 부자연·전환 스냅 해결 | §2.1 (구현) | — |
| ~~레이캐스트 (Closest/Any/All + maxDistance)~~ ✅ 3D | 요청. 지면·LOS·투사체·타겟팅 | §2.2 (구현). 2D 대칭·`Simulation` 연동 남음 | — |
| ~~디버그 드로우 패스 (라인/AABB/스피어)~~ ✅ | 레이·콜라이더 시각화 | §2.2 (구현). 본/스켈레톤 라인은 추가 가능 | — |

### P1 — 확장성/공백

| 항목 | 왜 | 문서 |
|---|---|---|
| 브로드페이즈(균일 그리드 or BVH) | N² 충돌은 엔티티 수십 개에서 한계. 레이캐스트도 이걸로 가속 | `collider-design.md` |
| 재사용 캐릭터 컨트롤러 컴포넌트(move-and-slide) | 지금 `Simulation::StepOneActor` 에 하드코딩 — 재사용/조립 불가 | 새 `game/CharacterController` |
| ~~오디오 서브시스템(XAudio2 최소 믹서)~~ ✅ → 스트리밍/3D음/OGG 후속 | 볼륨 슬라이더 살아남 | `docs/audio-design.md` §6 |
| winding 검증 + back-face cull | 지금 전 3D 패스가 `CULL_NONE` (`engine-conventions.md` §10) | `engine-conventions.md` |
| AssetRegistry + 비동기 로더 | 시작 검은 창, 런타임 로드 불가 | `loading-and-streaming.md`(설계 완료) |
| BC7/BC4 압축 (`atlas_pack --format bc7`) | 현재 무압축 `.dds` — 메모리 4배 | `atlas-build-pipeline.md` §5 |
| 게임패드(XInput) + 리바인딩 액션맵 | 입력이 키보드/마우스 고정 | `input` 새 소스 |

### P2 — 이후

| 항목 | 왜 |
|---|---|
| GPU 스키닝(`SkinnedMeshPass3D`) | 여러 캐릭터를 각자 다른 애니로 세우려면 필수 (`model-animation-research.md` §5.2) |
| sRGB 렌더 파이프라인 | 감마 정확 (`lighting.md`) |
| 시뮬/렌더 파이프라이닝 | 매 프레임 `ParallelFor().Wait()` 완전 블록 |
| 프레임타임 HUD / 프로파일러 | 성능 회귀 감지 |
| 라이트 배열(포인트/스팟) + CSM | 단일 방향광·단일 캐스케이드 (`lighting.md`) |
| 메시 LOD / 임포스터 | 디스턴스 스왑 없음 (인스턴스 크라우드 LOD 는 `instanced-rendering.md` §5, 메시 자체 LOD 는 별개) |
| 스왑체인 `FLIP_DISCARD` | 레거시 `DISCARD` (`CLAUDE.md` 알려진 이슈) |
| 결정성 리플레이(입력 로그 + 프레임 해시) | 회귀 테스트 (`time-design.md` 로드맵) |
| 프리팹 / 직렬화 / 세이브 | 콘텐츠 저작·저장 |

### 조사 필요 (설계 전에 결정부터)

| 항목 | 결정할 것 |
|---|---|
| ~~장르 / 시놉시스~~ | **확정: 대규모 디펜스** (`synopsis.md`). 남은 결정: 시점(탑다운 vs 3인칭), 저지 수단(타워/유닛/혼합), 적 표현(스켈레탈/인스턴스 메시/스프라이트) |
| Live2D/Spine 2D 리깅 | 상용 SDK 라이선스, vendor 가능 여부 (`animation-design.md` §4) |
| 스크립팅 vs 데이터 주도 튜닝 | Lua/wren vs JSON/토큰 테이블 |

### 디펜스 게임 우선순위 (장르 확정 반영, `synopsis.md`)

"대규모 오브젝트" 가 위 P0~P2 를 재정렬한다. 디펜스 게임 관점의 착수 순서:

| # | 항목 | 왜 (디펜스) | 기존 표 |
|---|---|---|---|
| ~~D1~~ ✅ | **레이캐스트 + 범위 질의 + 디버그 드로우** | 타워가 사거리 안 적을 고르고, 투사체가 다수를 맞춘다 | §2.2 (구현). `Simulation` 연동만 남음 |
| D2 | **다수 엔티티 SoA + `core::ObjectPool<T>`** (`game/AgentStore`) | 수백~수천 적/투사체를 개별 `new` 없이 스폰·재사용 | **`instanced-rendering.md` §6** (+ `entity-lifecycle-design.md` §3A, `scrollable-list-and-pool.md` §1.1) |
| D3 | **브로드페이즈(균일 그리드)** | 다수 대 다수 충돌·타겟 질의 — 선형 스캔 N² 불가 | P1, `instanced-rendering.md` §6.4 |
| D4 | **인스턴싱 렌더** — ✅ 인스턴스드 드로우 + 프러스텀·거리 컬(§8-1·2), LOD/빌보드·`AgentStore` 남음 | 같은 메시 수천 개를 draw call 소수로 | **`instanced-rendering.md`** (§3~§5, 구현순서 §8) |
| D5 | **웨이브/스폰 + HP/데미지 + 목표 지점·패배 판정** | 게임 루프 자체 | 새 `game/` 시스템 |
| ~~D6~~ ✅ | 오디오 최소 믹서 | 타격·스폰·경보음 + 설정 슬라이더 살리기 | `audio-design.md` (최소 구현) |
| — | 애니 재생 모드/크로스페이드(§2.1) | 플레이어 유닛/보스엔 필요하나 **적 다수엔 저비용 표현이 맞음** → 시점·적 표현 확정 후로 미룸 | P0 → 낮춤 |

---

## 4. 추천 (엔진이 지금 가장 아쉬운 것)

1. ~~**애니 재생 모드 + 크로스페이드**~~ ✅ (§2.1) — Once/PingPong + 로컬 TRS lerp 크로스페이드 + 파라메트릭 점프 구현. 블렌드 트리·루트 모션은 후속.
2. ~~**레이캐스트 + 디버그 드로우 패스**~~ ✅ (§2.2) — 3D 레이 3질의 + `DebugDrawPass` 구현. 2D 대칭·`Simulation` 연동만 남음.
3. ~~**오디오 최소 믹서**~~ ✅ — XAudio2 마스터 + music/sfx 서브믹스 + PCM 원샷/루핑, 설정 볼륨 3버스 연결(`docs/audio-design.md`). 스트리밍·OGG·3D음은 후속.
4. **장르/시놉시스 확정** — 부분 완료: 장르 **대규모 디펜스** 확정(`synopsis.md`). 남은 결정: 시점, 저지 수단, 적 표현.
5. **AssetRegistry + 비동기 로더** (`loading-and-streaming.md` 설계 완료 → 구현) — 시작 시 검은 창을 없애고, 씬 전환·아틀라스 그룹 로드를 가능하게 한다.

착수 순서 제안: **1·2·3 완료 → (4 결정) → 5**. 다음: 디펜스 게임 우선순위 D2(SoA + `ObjectPool`) → D3(브로드페이즈) → D5(웨이브/HP/목표).

---

## 5. 사용 방법 (How to use)

- **다음 작업 고르기**: P0 위에서부터. 조사 항목(§3 마지막)에 막혀 있으면 그걸 먼저 사용자에게 물어 결정.
- **항목 착수**: 미니 설계(있으면 §2)를 그 시스템 문서로 옮겨 "사용 방법" 까지 채우고, 여기 표의 행은 "→ `xxx.md`" 링크로 축약. 구현되면 `command-playbook.md` 에 "명령 → 처리" 행 추가.
- **새 항목 추가**: 어느 시스템 문서의 "지을 것" 에 먼저 적고, 그게 우선순위를 다투면 §3 표에 올린다.
- **하지 말 것**: 이 문서에 시스템 상세 계약을 쓰지 않는다(그건 각 문서). 여긴 "무엇을·왜·순서" 만.
