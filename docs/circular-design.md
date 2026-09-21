# 서큘러 (Circular) 설계 — 뱀서라이크 × 덱빌딩 × 오토배틀

**상태: §0(A안 확정 — 별도 씬으로 공존) + 씬/렌더 파이프라인 골격 구현 완료. 카드/성장/미니언/
메타 프로그레션(§2~§6, §8)은 여전히 설계만.**

- `Simulation::DemoScene::Circular` 씬 추가 — `ENGINE_WITH_3D` 유무와 무관하게 항상 선택 가능
  (`EnterScene`/`ActiveScene`가 이제 무조건부 선언, 3D 켜져 있어도 Circular 진입 시 3D 파이프라인
  전체를 건너뜀 — `Simulation::Step`/`SnapshotBuilder::Build`/`Application::Run` 세 곳 모두
  `ActiveScene() != Circular` 런타임 가드 추가, 이전 3D 씬 렌더가 밑에 겹쳐 보이던 문제 방지).
- **몹 스웜 = 진짜 SoA** (`game::MobField`, §1의 "AgentStore" 요청 그대로) — 인덱스별
  `posX/posY/velX/velY/health/radius` 분리 벡터 + free-list 재활용, `JobSystem::ParallelFor`로
  플레이어 추적 스티어링. 현재 프리셋(`game/CircularConfig.h` `kActiveMob`)은 capacity 4096.
  범위 데미지(`DamageInRadius`)는 아직 선형 스캔(문서 §9 "신규 필요" 그대로, 그리드는 필요해지면).
- **2D 이펙트 파이프라인 신설** — `render::EffectInstance`(`render/r2d/Sprite2D.h`) +
  `render::EffectPass2D`(`particle.hlsl`과 동형의 절차적 블롭 셰이더, `assets/shaders/effect2d.hlsl`,
  단 카메라 행렬 없이 순수 화면공간) + `RenderSnapshot::worldEffects`. `main.cpp`에서
  `QuadPass2D`(몹/플레이어) 다음, `SpritePass2D`(UI) 이전에 등록.
- **몹/플레이어 렌더는 신규 파이프라인 없이 기존 `QuadPass2D`(`worldQuads`) 재사용** — 아트가
  없어 흰 사각형/원 근사, §9 표의 "2D 카드/이펙트 이미지"는 여전히 콘텐츠 제작 남음.
- **카드 시스템 자리표시자**: 진짜 덱/슬롯 없음. `Simulation::kCircularAttack*` 상수 + 플레이어
  중심 고정 반경 펄스(`StepCircularScene`)가 `MobField::DamageInRadius` → 힛플래시(`HitFlash`) →
  `EffectPass2D`까지 파이프라인 전체를 검증하는 용도로만 존재 — §2의 `CardDef`/`CardInstance`로
  교체될 자리.
- **세이브/XP/레벨업/미니언 그리드는 미착수** — §3(성장 이원화)·§4(미니언)·§5(메타)는 여전히
  설계 문서 상태 그대로.

원본 GDD(사용자 업로드, v0.1 초안)가 장르·루프·수치의 source of truth다. 이 문서는 그걸 이
엔진의 어떤 기존 시스템에 얹을 수 있고, 뭘 새로 만들어야 하는지만 다룬다 — GDD 내용 자체를
재서술하지 않는다.

관련: [synopsis.md](synopsis.md)(기존 "대규모 디펜스" 장르 확정과의 관계 — §0),
[defense-combat-design.md](defense-combat-design.md)(웨이브 상태기계 §0 패턴,
데미지 계층 §2 패턴 재사용), [entity-lifecycle-design.md](entity-lifecycle-design.md)
§3A/§3B(카드·몹 저장 전략 판단), [scene-flow-design.md](scene-flow-design.md)
(레벨업 모달 = 오버레이 패턴), [time-design.md](time-design.md)(레벨업 시 전역 일시정지),
[texture-atlas-and-sprite-pass.md](texture-atlas-and-sprite-pass.md)(카드/몹 스프라이트),
[collider-design.md](collider-design.md)(2D 브로드페이즈, `CollisionWorld2D`),
[particle-system-research.md](particle-system-research.md)(3D 전용이라 그대로는 못 씀 — §7),
[scrollable-list-and-pool.md](scrollable-list-and-pool.md)(`core::ObjectPool<T>`,
`ui::ScrollList` — 카드/몹 풀링, 인벤토리 화면), [animation-design.md](animation-design.md)
§2(2D 스프라이트 애니메이션, 설계만 — §7에서 처음 실사용 후보), [roadmap.md](roadmap.md).

---

## 0. `synopsis.md`와의 관계 — 판단 필요

`synopsis.md`는 이미 "대규모 디펜스"를 장르로 확정했고 `DefenseCombat` 씬으로 구현까지
끝났다(웨이브/무기5종/자원경제, `defense-combat-design.md`). 서큘러는 완전히 다른 장르
(2D 탑다운 뱀서라이크 × 덱빌딩)다. 둘 중 하나를 골라야 한다:

- **A. 별도 씬으로 공존 (이 문서가 가정하는 기본값)** — `Simulation::DemoScene`에 신규 항목
  추가(`CharacterDemo`/`DefenseCombat`/`ShadowShowcase`/`EffectsTest`와 같은 급). 기존 3D
  작업·문서 전부 보존, `ENGINE_WITH_2D` 경로를 처음으로 실사용하는 씬이 됨(`engine-overview.md`가
  원래 의도한 "2D는 baseline"과 부합).
- **B. 신규 메인 게임으로 전환** — `synopsis.md`를 이 GDD로 덮어쓰고 `DefenseCombat`은 데모/
  레퍼런스로 격하. 기존 다수 문서가 "구현 완료"로 남지만 로드맵 우선순위에서 빠짐.

**A로 확정됨** — `Simulation::DemoScene::Circular` 추가로 구현 착수(상태 줄 참고).
`synopsis.md`/`defense-combat-design.md`는 그대로 유지, `DefenseCombat`도 계속 정상 동작.

---

## 1. 핵심 루프 → 엔진 매핑

| GDD 요소 | 엔진 매핑 |
|---|---|
| 필드 이동(유일한 조작) | `Application::BuildPlayerIntent`(WASD) → 2D 액터. 카메라는 플레이어 고정 스크롤 — 기존 3D 오빗캠과 다른 신규 2D 정사영/픽셀좌표 카메라 필요 |
| 카드 자동발동 | 신규 `game::Card`/`Deck` — 카드마다 자기 쿨다운(고정 스텝), 만료 시 효과 실행 |
| 몹 스폰/이동/처치 | `physics/p2d::CollisionWorld2D`(원형 콜라이더) + `core::ObjectPool<Mob>`. HP는 `defense-combat-design.md` §2 패턴(이벤트 큐 없이 즉시 `health -=`) 그대로 |
| XP/레벨업 | 신규, 카운터 + 임계값 테이블 |
| 레벨업 3택 모달 | `ui::UIContext::SetOverlay`(Settings 모달과 동일) + 전역 timeScale 0(`time-design.md`) |
| 웨이브 강도 곡선 | `defense-combat-design.md` §0.5 에스컬레이션과 동일 패턴(웨이브 번호 → 스폰레이트 함수) |
| 보스 웨이브 | 몹과 같은 파이프라인, HP/크기만 다른 데이터(테이블 구동) |
| 런 종료 → 메타 재화 | §5 — **세이브 시스템 신규 필요** |

---

## 2. 카드 시스템 → 저장 전략

GDD §4(공격/소환/버프/패시브 분류 + 레벨마다 질적 변화 + 슬롯 6~8 + 합성)는 언뜻
`entity-lifecycle-design.md` §3B(컴포넌트 테이블, sparse set — "있고 없는 조합이 다양한 경우"
용으로 이미 설계돼 있지만 실사용처가 없어 미연결)가 맞아 보이지만, 실제로는 **덱 자체(보유
카드 6~8장)는 §3A(단순 struct 배열)로 충분**하다 — 조합 다양성이 "카드 종류 유무"가 아니라
"레벨별 효과"에 있고, 덱은 동질 슬롯 배열이기 때문. §3B가 필요해지는 시점은 GDD §8 유물
시스템(카드 인스턴스에 임의 부가효과가 컴포넌트처럼 덧붙는 경우)까지 갔을 때.

```cpp
// game/Card.h 스케치
enum class CardKind : std::uint8_t { Attack, Summon, Buff, Passive };

struct CardDef   // CrowdConfig.h/GibConfig.h 관행과 동일한 constexpr 데이터 테이블
{
    CardKind kind;
    float    cooldown;
    int      maxLevel{ 5 };
    // 레벨별 효과는 순수 수치 테이블로 부족(질적 변화) — 레벨업 시 CardInstance 플래그를
    // 켜는 방식 추천(예: hasSplash, hasPierce), CardDef 자체엔 분기 안 둠
};

struct CardInstance { const CardDef* def; int level{ 1 }; float cooldownLeft{ 0.0f }; };
// std::vector<CardInstance> m_deck;   // 슬롯 6~8 상한, 슬롯 인덱스가 곧 식별자(CardId 불필요)
```

- **쿨다운 발동**: `Simulation::Step`(고정 스텝)에서 `m_deck` 순회, `cooldownLeft -= dt`, 0
  이하면 효과 실행 + 리셋. 최대 8장이라 `JobSystem::ParallelFor` 오버킬(오버헤드가 더 큼).
- **효과 실행(타겟팅)**: 공격 카드는 범위 질의가 필요한데 **`CollisionWorld2D`엔 지금
  "반경/부채꼴 안 전부"가 없다**(레이캐스트만 있음) — §9에 신규 항목으로 표시.
- **질적 변화**: `CardDef` 레벨→효과 테이블 대신, 레벨업 시 `CardInstance` 플래그를 켜는
  방식(Lv3 "폭발 반경 추가"를 분기 없이 표현).
- **슬롯 교체/합성**: 레벨업 3택 UI 로직, `m_deck` 벡터 조작(swap/erase+push)뿐 — 새 저장
  구조 불필요.

---

## 3. 성장 이원화(스탯 vs 카드) — 신규지만 단순

GDD §5의 능력치 카드(이속/체력/XP획득량/자석범위 등)는 `PlayerStats{ moveSpeedMul, maxHp,
xpGainMul, magnetRadius, regenPerSec, rarityBonus }` 하나로 충분 — 카드 선택 시 필드 갱신뿐,
새 시스템이랄 게 없음.

3택 선택지 풀(최소 1장 무기카드 보장, GDD §5.2)은 **진짜 RNG가 필요**한 몇 안 되는 지점 —
이 엔진의 다른 스폰/시뮬 로직은 대개 결정적(`SeedAgent` 슬롯 해시 등)이라 `<random>` 도입은
여기가 처음이 됨.

---

## 4. 미니언 오토배틀 그리드 — GDD 중 엔진과 가장 안 맞는 부분

3x2 그리드 자동전투(GDD §6, TFT식)는 유사 사례가 없다 — `SimAgent`/크라우드는 "적" 전용,
아군 자동전투 유닛 개념 자체가 없음. 신규 필요:

- **그리드 배치 UI(드래그)** — `ui::Slider`가 드래그 캡처 불완전(CLAUDE.md "알려진 소소한
  이슈")하다고 이미 기록돼 있음, 그리드 드래그도 같은 함정 주의.
- **미니언 AI** — 카드와 비슷한 쿨다운 구조 + `CollisionWorld2D`에서 "가장 가까운 적" 탐색.
  단, 위치가 그리드에 고정된다는 차이.
- **진형 시너지**(앞줄 탱커/뒷줄 원거리 버프) — 그리드 좌표 읽어 스탯 배율 적용, 신규 렌더·
  충돌 불필요.

**권장**: GDD §11 로드맵의 5단계 그대로 — 1~4단계(카드+성장+웨이브)만으로 완결된 루프가
나오고, 미니언이 이 엔진 기준 리스크가 제일 큰 신규 시스템이니 뒤로 미루는 순서가 맞다.

---

## 5. 메타 프로그레션 — **엔진에 없는 선행 인프라가 진짜 블로커**

GDD §8(런 종료 시 재화 획득 → 영구 해금 → 재도전)은 세이브 파일이 있어야 성립하는데, 지금
엔진엔 `core::Settings`(그래픽/오디오 설정 로컬 저장) 밖에 어떤 영속 저장도 없다(확인됨,
`src/core/`에 세이브/로드 계층 없음).

- 최소로 필요한 것: `core::SaveData`류 — 메타 재화, 해금된 카드/시작세트/유물 id 목록을 파일
  (JSON/바이너리)로 직렬화. `Settings.cpp`가 "값을 파일로"의 유일한 선례라 포맷/경로 규칙은
  그 패턴 재사용이 자연스러움.
- **1~4단계 프로토타입은 세이브 없이도 검증 가능** — 메타 프로그레션은 GDD 로드맵 5~6단계라
  이 인프라도 그때 만들어도 순서상 안 늦음. 지금 당장 블로커는 아님.

---

## 6. UI/UX

GDD §9(카드 순환 표시, 미니언 그리드 하단, 레벨업 모달)는 `ui::Widget` 합성으로 가능.
`ui::ScrollList`(가상화)는 카드 인벤토리 화면(`InventoryScreen` 선례)에 재사용. 새로 필요한
건 원형/막대 쿨다운 게이지 위젯(값 0..1 하나만 받는 단순 신규 `ui::Widget` 파생) 정도.

---

## 7. 2D 파티클/이펙트 — 현재 3D 전용이라 그대로 못 씀

`particle-system-research.md`의 파티클 시스템은 **`render/r3d::ParticlePass3D` 전용**(3D
빌보드 인스턴싱, `Frame` cbuffer의 카메라 축 등 3D 파이프라인에 결합). 카드 이펙트(파이어볼
폭발 등)엔 그대로 재사용 불가. 옵션:

- **A (추천, 초반)**: `render::SpriteDraw` 프레임 애니메이션(스프라이트시트 순차 전환)만으로
  단순 이펙트 — 새 렌더 경로 불필요, `animation-design.md` §2(2D 스프라이트 애니메이션, 현재
  설계만)를 여기서 처음 실사용. 원형 글로우류는 `Quad`를 짧은 수명으로 스폰하는 절차적 2D
  파티클로도 근사 가능(3D `ParticlePass3D`의 개념만 이식, 코드는 새로 씀).
- **B**: 진짜 `render/r2d::ParticlePass2D` 신규(3D 패턴 이식) — 카드 종류가 늘고 동시 발동
  이펙트가 많아지면 필요. 지금은 YAGNI.

---

## 8. 아키텍처 규칙 준수 확인

- **2D baseline만 사용, 3D `#include` 금지** — 규칙 7. `ENGINE_WITH_3D`를 꺼도 이 게임은
  빌드/동작해야 한다(이 규칙의 첫 실질 검증 사례).
- **충돌은 탐지만** — 몹 피격 반응(넉백 등)은 `CollisionWorld2D` 밖(게임 로직)에서 처리(규칙 8).
- **엔티티 저장**: 몹 = §3A(AoS + `ObjectPool`, `SimAgent`와 동일 패턴), 카드 = §3A(§2 참고).
  이번 게임도 §3B 컴포넌트 테이블을 실제로 요구하는 대상은 아직 없음(유물 시스템까지 가면
  재검토).
- **렌더**: `SpriteDraw`/`TextureAtlas`(부분 구현이지만 몹/카드아이콘 정도엔 충분).

---

## 9. 신규로 필요한 것 (요약)

| 항목 | 상태 |
|---|---|
| 2D 이동/카메라 스크롤 | 부분(3D 카메라 패턴 이식 필요) |
| 2D 몹 스폰/충돌/HP | 있음(`CollisionWorld2D`) + 신규 데이터(`Mob`) |
| 카드 시스템(정의/쿨다운/업그레이드/합성) | **신규** |
| 반경/부채꼴 범위 질의(카드 타겟팅) | **신규** — `CollisionWorld2D`에 없음(레이캐스트만 있음) |
| 레벨업 3택 모달 | 있음(오버레이 패턴) + 신규 UI 내용 |
| 미니언 그리드 오토배틀 | **신규**(가장 큰 신규 시스템) |
| 2D 카드/이펙트 이미지 | 있음(아틀라스) + 콘텐츠 제작 |
| 2D 파티클/이펙트 | **신규**(§7 옵션 A 권장) |
| 세이브/메타 프로그레션 | **신규**(§5, 후순위 — 블로커 아님) |
| RNG(레벨업 선택지) | **신규**(`<random>` 도입, 결정적 관행의 예외) |

---

## 10. 개발 우선순위 — GDD §11 채택, 엔진 작업 순서만 주석

1. 이동 + 쿨다운 카드 자동발동 + XP/레벨업 — 2D 카메라·`CollisionWorld2D`·`Mob` `ObjectPool`부터
2. 스탯 vs 무기 카드 이원화 선택지 — `PlayerStats` + 3택 모달
3. 카드 업그레이드(질적 변화) + 슬롯 제한/교체 — §2 플래그 방식
4. 덱 순환 보너스(후반 심화) — §1 루프 위에 버프 레이어만 추가
5. 미니언 진형(오토배틀) — §4, 리스크 제일 큼, 제일 나중
6. 메타 프로그레션(영구 해금, 유물) — §5 세이브 인프라 선행

---

## 사용 방법 (How to use)

- **씬 진입/전환**: `Simulation::DemoScene::Circular` + `EnterScene(DemoScene::Circular)` —
  이미 배선됨. 타이틀 → "SELECT SCENE" → "CIRCULAR" 버튼(`TitleScreen::BuildSceneSelectScreen`).
  `ENGINE_WITH_3D` 유무와 무관하게 항상 뜬다(3D 스위치가 꺼져 있으면 나머지 4개 버튼만 빠짐 —
  `BuildSceneSelectScreen`이 빈 `std::function` 콜백을 받으면 그 행을 건너뛰는 방식, OCP).
- **몹 SoA에 새 필드 추가**: `game::MobField`의 병렬 벡터(`m_posX`/`m_health`/... )에 새
  `std::vector<T>` 하나 추가 + `Spawn`/`Kill`에서 같이 채움/비움. **AoS로 되돌리지 말 것** —
  이 파일 헤더 코멘트가 SoA를 고른 이유(캐시 지역성, `ParallelFor` 범위별 필드 접근)를 설명한다.
- **새 몹 타입 추가(여러 타입 지원 시)**: 지금은 `game/CircularConfig.h`의 `kActiveMob` 하나뿐
  (§1 "카드 1종" 원칙과 동일한 YAGNI). 두 번째 타입이 필요해지면 `MobConfig` 배열 + `MobField`에
  타입 인덱스 필드 하나 추가.
- **새 카드 추가**: 아직 `CardDef` 테이블 자체가 없다(§2는 여전히 설계만) — 지금 있는 건
  `Simulation::kCircularAttack*` 상수 하나뿐인 자리표시자 "카드". 진짜 카드 시스템을 만들 때
  이 상수들과 `StepCircularScene`의 펄스 블록을 `CardDef`/`CardInstance`로 교체(§2 스케치 참고).
- **새 2D 이펙트 추가**: `render::EffectInstance`를 만들어 `RenderSnapshot::worldEffects`에
  push — 새 렌더 패스 불필요, `EffectPass2D`가 이미 매 프레임 소비한다. 카드 시스템이 생기면
  `Simulation::HitFlash` 패턴(값 타입 + 수명 필드 + swap-remove)을 그대로 복제해서 카드별
  이펙트 벡터를 늘리면 됨(`TracerLine`이 3D 쪽에서 이미 같은 패턴).
- **하지 말 것**:
  - `MobField`를 `core::ObjectPool<Mob>`(AoS) 스타일로 되돌리지 말 것 — SoA가 이 문서의
    "대규모 몹 사냥 전제" 요구사항 자체다.
  - 카드 효과를 `Simulation::Step`/`StepCircularScene` 안에 하드코딩 if/else 사슬로 늘리지
    말 것 — `CardDef` + 효과 태그 테이블을 거친다(OCP, `defense-combat-design.md`가 무기
    5종에서 이미 겪은 함정).
  - 몹 AI를 `SimAgent`(3D 크라우드)와 공유하지 말 것 — 2D/3D 분리 규칙(CLAUDE.md 불변 규칙 7)
    위반. `MobField`/`Simulation`의 Circular 관련 코드는 전부 `ENGINE_WITH_3D` 가드 밖에 있다.
  - `ParticlePass3D`/`render/r3d/*`를 2D 씬에서 직접 호출하지 말 것 — `EffectPass2D`(`r2d`)가
    그 자리를 대신한다.
  - `Simulation::Step`/`SnapshotBuilder::Build`/`Application::Run`의 3D 블록에 새 3D 전용
    로직을 추가할 때 `ActiveScene() != DemoScene::Circular` 가드를 빠뜨리지 말 것 — 빠뜨리면
    Circular 진입 시 그 로직이 되살아나 이전 3D 씬 잔여 상태를 다시 밟거나(낭비) 화면에
    겹쳐 그려짐(정확성 버그).
