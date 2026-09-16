# 디펜스 전투 설계 — 무한 웨이브(전투 60초 + 정비 60초) + 무기 5종

**상태: §0(핵심 상태기계 + 패배 처리)/§0.1(자원)/§1/§2(공통 데미지 계층)/§3~§7(무기 5종) 구현 완료.**
**§0.3(정산 화면)/§0.5(에스컬레이션)/§0.6(미니맵)와 모든 UI는 설계만.**
**패배는 결과 화면 없이 즉시 타이틀로** — §0.3이 없어서 "패배" 표시 없이 바로 `EnterTitle()`만
호출(`Application::Run()`, `m_simulation.IsMatchLost()`). 최고 웨이브 기록도 아직 없음(§0.3에서
같이 할 일).
"좀비가 60초 동안 쏟아지고, 무기로 막고, 60초 정비하고, 다시 쏟아지고 — 무한 반복"을 이
엔진에 있는 것 위주로 설계한다. 새 아키텍처(컴퓨트·GPU 시뮬 등)는 안 끌어옴 —
`docs/particle-system-research.md`/`docs/horde-design.md`가 이미 확인한 원칙 그대로: CPU +
`JobSystem::ParallelFor` + 값 기반 스냅샷으로 충분한 규모.

관련: [horde-design.md](horde-design.md)(대규모 크라우드 — 이 문서가 실제로 쓰는 건 그 설계가
아니라 **이미 구현된** `SimAgent`/`instanced-rendering.md` 경로. §4.5 "무한 리스폰"은 이 문서의
전투 페이즈가 소비처), [particle-system-research.md](particle-system-research.md)
(총구·폭발 VFX 그대로 재사용), [instanced-rendering.md](instanced-rendering.md)(`MeshInstance`
패턴), [collider-design.md](collider-design.md)(레이캐스트·브로드페이즈, 탐지만),
[demo-scene.md](demo-scene.md)(씬 2, `SimAgent`), [entity-lifecycle-design.md](entity-lifecycle-design.md)
§3A(SoA 없이 AoS+`ObjectPool` 로 충분한 경우), [time-design.md](time-design.md)(고정 스텝),
[scene-flow-design.md](scene-flow-design.md)(`GameState::WaveResults`, 이 문서가 그 세부 계약),
[roadmap.md](roadmap.md) D5, [synopsis.md](synopsis.md)(장르).

---

## 0. 게임 루프 — 전투 → 정산 → 정비, 무한 반복 (핵심 상태기계 + 패배 처리 구현 완료, §0.1은 구현·§0.3/§0.5는 아직 설계만)

```text
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
▼                                                                      │
Combat(60s)  ──타이머 만료──▶  전멸 처리(§0.2)  ──▶  WaveResults(§0.3) │
  waveActive=true                                    (아직 없음 — 미구현)│
  좀비 무한 리스폰(horde §4.5)                              │           │
  목표HP 0 되면──▶ EnterTitle() (구현됨, 결과화면 없이 즉시)  │ "다음으로" │
                                                              ▼           │
                                                     Prep(60s)(§0.4) ─────┘
                                                       waveActive=false
                                                       배치/구매만, 좀비 없음
```

(`waveActive` 는 `horde-design.md` §4.5 의 파라미터 이름 그대로 — 이 문서의 `MatchPhase::Combat`
과 같은 뜻이다. `Horde::Step` 호출부에서 `waveActive = (m_phase == MatchPhase::Combat)` 로 넘김.)

- **무대**: 지금 씬 2(`m_demoScene == DemoScene::DefenseCombat`, 언덕 위 1인칭 플레이어 + 아래
  필드의 `SimAgent` 크라우드)를 그대로 쓴다 — 이미 다수 개체·인스턴싱·레이캐스트가 다 있다.
  (원래는 절벽이었으나 1인칭 전환과 함께 10m/20° 언덕으로 교체 — §1 "구현노트".)
- **상태 소유 분리(SRP)**: `Application`/`GameState` 는 "화면이 몇 개고 언제 바뀌는지"만
  안다(`InGame`(전투+정비 둘 다) / `WaveResults`) — [scene-flow-design.md](scene-flow-design.md)
  §1 이 이미 이 자리를 비워뒀다. **전투냐 정비냐**는 `Simulation`(또는 그 안의 `WaveDirector`)
  소유의 게임플레이 상태:
  ```cpp
  enum class MatchPhase : std::uint8_t { Combat, Prep };
  // Simulation 또는 WaveDirector 멤버
  MatchPhase m_phase{ MatchPhase::Combat };
  float      m_phaseTimeLeft{ 60.0f };
  int        m_waveNumber{ 1 };
  ```
  두 페이즈 다 `Simulation::Step` 이 계속 돈다(플레이어는 정비 중에도 걸어다니며 배치한다) —
  `GameState` 전환이 필요한 건 `WaveResults`(전체화면 요약) 하나뿐.
- **"쏟아짐" 지속(전투 페이즈)**: `horde-design.md` §8.1 의 `WaveDirector`(시간→스폰 레이트
  곡선)로 초기 개체 수를 채우고, **60초 내내 죽어도 계속 오는 건 별도 트릭**(`horde-design.md`
  §4.5 "무한 리스폰" — SoA 대상으로 적었지만 **지금 AoS `SimAgent`/`core::ObjectPool` 에도
  그대로, 오히려 더 쉽게 적용된다**): 죽은 좀비를 `m_agents.Release()` 하지 않고, 같은
  슬롯/핸들을 유지한 채 `SeedAgent()` 로 필드만 출발점 상태로 리셋 — 전투 페이즈 내내
  `ObjectPool` 의 `Acquire`/`Release` 를 아예 안 거친다. `m_phase != Combat` 이 된 뒤 죽는
  개체만(§0.2) 진짜 `Release`. §2 의 사망 판정(`state=Dead`) 분기에서 `Release` 대신 이
  리셋을 부르는 한 줄 차이 — capacity 는 "동시 생존 목표 수"만 결정하면 되고 웨이브 길이·총
  킬 수와 무관해진다.
- **목표 도달도 같은 파이프라인**: 좀비가 목표 지점(플레이어 방어선, §1)에 도달하면 "사망"과
  똑같이 취급 — 목표 HP 감소 + 즉시 §4.5 리스폰(킬 크레딧 없이). 별도 despawn 경로를 안 만들고
  §2/§4.5 의 죽음-리스폰 파이프라인에 "도달"이라는 세 번째 트리거만 추가하면 된다(사망 원인이
  `Weapon`이냐 `ReachedGoal`이냐만 다름 — `DamageEvent` 처럼 원인 태그 하나로 분기).
- **승패**: 목표 HP 0 = 즉시 패배(웨이브 진행 중 언제든) → `EnterTitle()`. 그 외엔 무한 반복
  (승리 조건 없음 — 엔드리스 웨이브 디펜스, `synopsis.md` 장르 그대로). 최고 웨이브 도달 수가
  스코어(아직 기록 안 함 — §0.3에서 할 일).

  **구현노트**: 결과 화면(§0.3, "패배" 표시 + "타이틀로" 버튼)이 아직 없어서 최소 연결만 함 —
  `Application::Run()`의 프레임 루프가 스텝 직후 `m_simulation.IsMatchLost()`
  (`m_objectiveHealth <= 0.0f`)를 보고 참이면 그 프레임에 바로 `EnterTitle()`. 확인 화면·
  "다음으로/타이틀로" 버튼 분기 없이 한 프레임 만에 타이틀로 끊긴다. 재시작은 `EnterInGame()`이
  매번 `Simulation::ResetMatch()`를 불러 처리 — 이게 유일한 InGame 진입점이라 첫 "시작하기"든
  패배 후 재시작이든 똑같이 웨이브 1/목표 HP 풀로 깨끗하게 시작한다(에이전트·기브·오드넌스
  풀·슬로우존 전부 초기화 후 `SpawnSimAgents()`). 검증: `kObjectiveDamagePerBreach`를
  일시적으로 1500까지 올려(1000 HP 기준 한 번의 도달로 즉사) 패배→재시작 루프를 강제로 반복시켜
  25초 동안 10회 이상 사이클을 돌렸고 크래시·누수 징후 없음 확인.

  **밸런스 스톱갭(`kObjectiveMaxHealth = 40000`)**: 원래 1000이었으나, 실플레이(가만히 걸어만
  다녀도)에서 웨이브 시작 후 약 6~7초 만에 패배하는 문제가 보고됨 — 좀비가 목표에 "도달"하면
  안 죽고 스폰 지점으로 순간이동 후 다시 오는 무한 리스폰(§4.5) 때문에, 목표 근처에 스폰된
  개체들이 왕복 몇 초마다 계속 브리치를 내서 타이머(60~120초)가 끝나기 한참 전에 목표 HP가
  먼저 바닥남. 진짜 해법은 §0.5 웨이브 에스컬레이션(난이도 곡선 — 낮은 웨이브는 소수/느린
  적)인데 아직 스코프 밖이라, 임시로 HP 풀만 키워 "방치해도 대략 1분은 버틴다"를 목표로 실측
  교정: `kObjectiveMaxHealth`를 일시적으로 매우 크게(100만) 두고 무플레이 상태로 관찰한 결과,
  브리치 데미지 누적이 초반 램프업 이후 **초당 약 750(steady-state)**로 수렴 — 60초 시점 누적
  약 39100. 여기에 소폭 여유를 둬 40000으로 설정(그래도 방치 시 60초를 살짝 넘겨서 죽음, 실제
  플레이어가 쏘면 그보다 더 오래감). **난이도 곡선이 아니라 고정 버퍼일 뿐**이므로 §0.5 작업
  시 이 값도 같이 재검토 필요.
  **부수 버그 발견·수정**: 이 계측 도중 언덕(§1 "구현노트") 도입으로 생긴 회귀를 하나 찾음 —
  `kCrowdGoal`이 `y=0`으로 고정돼 있었는데, 좀비가 경사면을 오르며 `y`가 최대 `kHillHeight`
  (10)까지 올라가므로 목표까지의 3D 거리(`Length(kCrowdGoal - a.pos)`)가 고도차 때문에 절대
  좁혀지지 않아 **아무도 목표에 도달할 수 없게 됨**(70초 방치해도 브리치 0회로 확인). `kCrowdGoal`
  의 `y`를 `kHillHeight`로 맞춰서 해결.

### 0.1 자원 경제 — 정비 구매용 화폐 (구현 완료 — 획득/소비, 상점 UI는 없음)

"지뢰를 심든 박격포 포탄을 사는" 에는 화폐가 필요하다.

```cpp
// Simulation 멤버
int m_supplies{ 0 };   // 정비 페이즈에서 소비. 정찰/보너스 등 확장 여지는 있으나 v1은 킬 보상만.
```

- **획득**: §2 의 사망 판정(무기로 죽인 경우만 — `ReachedGoal` 로 리스폰된 건 보상 없음, 오히려
  목표 HP 를 깎는 페널티)에서 `m_supplies += kSupplyPerKill(typeId)`.
- **소비**: 정비 페이즈(§0.4)의 상점 UI에서 `PlacedOrdnance`/`SlowZone` 배치 또는 무기 재고
  (`m_mortarAmmo`/`m_mineAmmo` 카운트, §4) 충전에 사용. 부족하면 배치/구매 버튼 비활성.
- **표시**: `WaveResults`(§0.3)에서 "이번 웨이브 획득 + 누적", 정비 UI에서 "현재 보유".

**구현 노트** (`Simulation.h/.cpp`):
- `typeId`별 보상 테이블은 안 만들었다 — 크라우드가 아직 단일 타입이라 `kSupplyPerKill`(고정
  10) 하나면 충분(YAGNI). 여러 타입이 생기면 그때 테이블화.
- 획득은 `AwardKill()`(신규 private 헬퍼)이 `++m_killCount`와 `m_supplies += kSupplyPerKill`을
  같이 처리 — 기존 3곳의 킬 집계 지점(`DamageAgent`/`TriggerExplosion`/화염 도트의
  `needsKillCount` 소비)을 전부 이 한 곳으로 통일해서 새 보상 로직이 하나만 있으면 됐다.
  `EndCombatPhase`(§0.2 전멸)와 목표 도달(`reachedGoal`)은 `AwardKill`을 안 부르므로 자동으로
  보상 대상에서 빠진다(문서가 요구한 그대로).
- **소비는 상점 UI 없이 바로 배치 함수에 넣었다** — `PlaceOrdnance`/`PlaceSlowZone`이 각자
  `kMortarCost`(30)/`kMineCost`(20)/`kWireCost`(15)를 확인해 부족하면 조용히 no-op(풀이 꽉
  찼을 때와 같은 "드롭, 치명적 아님" 처리). §0.4가 상정한 "버튼 비활성화" UI 피드백은 아직
  없지만 자금 부족 시 실제로 배치가 안 되는 동작 자체는 이미 완성.
- 검증: 라이플 자동발사로 킬→`supplies`+10/킬 누적, 박격포/지뢰/철조망 순서로 자동 소비 시도
  — 잔액 부족한 항목(철조망)만 계속 배치가 안 됨(zones 그대로)을 확인. 대량 폭발로 581킬이
  한 번에 나는 상황에서도 `supplies` 값이 일관되게 누적/차감됨(오버플로우·불일치 없음).

### 0.2 웨이브 종료 — 전멸 처리

60초 타이머 만료 순간(또는 §12 "판단 필요"에 따라 타이머와 무관하게 즉시), **살아있는 좀비
전체를 강제로 죽인다** — `horde-design.md` §4.5가 "필요해지면"으로 열어뒀던 필드 클리어가
바로 이 자리다.

```cpp
// Simulation::EndCombatPhase() — Combat → (전멸) → Results 전환 시 1회
void Simulation::EndCombatPhase()
{
    m_phase = MatchPhase::Prep;   // 이후 죽는 개체(이번 호출이 만드는 죽음 포함)는 리스폰 안 함(§0)
    for (agent : m_agents.ActiveIndices())
        if (agent.state == Alive) agent.state = Die;   // 사망 파이프라인 그대로 태움(§2)
    // 기브(§3)는 스킵 — 한 스텝에 수백 마리가 동시에 기브를 뿜으면 기브 풀(256)이 순식간에
    // 찬다(§12에서 이미 지적된 문제). 전멸은 "쓰러짐"(사망 애니만) + 페이드로 충분하지 실제
    // 총알/화염에 죽는 것과 시각적으로 달라도 무방 — 웨이브 끝의 정리 연출이라는 게 명확하므로.
}
```

- **`bool skipGibs` 플래그**: §2/§3 의 사망 처리 함수에 인자 하나 추가 — 개별 킬(무기)은
  `skipGibs=false`, 전멸은 `true`. 새 시스템 아니고 기존 함수 시그니처 확장.
- **애니메이션**: 그냥 쓰러지는 것만으로 충분(§3 과 같은 판단 — 이 엔진 스케일엔 래그돌 불필요).
  전부 동시에 쓰러지는 게 부자연스러우면 슬롯 인덱스 기반으로 0~0.3초 사이 지연(결정적, RNG 없음).
- **워커 안전**: `state=Die` 대입은 자기 슬롯 필드라 `ParallelFor` 워커 안에서도 안전(규칙 6) —
  이 루프 자체를 병렬화할 수도 있지만, 웨이브 끝은 프레임당 1회뿐인 이벤트라 메인에서 순차
  순회해도 비용 무시할 만함(수천 개면 병렬화, 측정 게이트).

**구현 노트** (`Simulation.h/.cpp`) — **핵심 상태기계만** 구현, §0.1(자원)/§0.3(정산 화면)/
§0.5(에스컬레이션)는 아직 설계만(범위를 좁힌 판단, 아래):
- `agent.state`(Alive/Die) 열거형은 안 만들었다 — 실제 `SimAgent`엔 그런 필드가 없고 `health`
  float 만 있다. `EndCombatPhase`는 그 대신 **`m_agentHandles`를 전부 `m_agents.Release()`**
  — 사망 파이프라인을 거치지 않고 풀에서 바로 비움(스케치의 `state=Die`보다 한 단계 더 직접적).
  `skipGibs` 플래그도 안 만들었다 — `EndCombatPhase`가 `DamageAgent`/`TriggerExplosion` 등
  기존 사망 경로를 아예 안 거치므로 애초에 기브가 안 붙는다(플래그로 끌 필요 자체가 없음).
  `m_killCount`도 안 늘어난다(이건 "죽인 것"이 아니라 "웨이브 종료 청소").
  **쓰러짐 애니메이션·지연 연출은 스코프 밖** — 순간 `Release`.
- `StepMatchPhase(fixedDelta)`가 `Step()`의 **맨 마지막**에 돈다 — 이번 프레임의 다른 모든
  시스템(무기·기브·오드넌스)이 여전히 "이전" 페이즈를 보고 처리되고, 전환은 다음 프레임부터
  적용된다. 프레임 도중 `SpawnSimAgents()`(풀 `Init`)가 같은 프레임의 다른 코드와 겹치는 걸
  피하려는 단순한 재진입 회피.
- **`GameState::WaveResults`/정산 화면 UI 자체는 안 만들었다** — Combat 종료 시 정산 화면 없이
  바로 Prep 으로 넘어간다(§0.4 가 이미 "정비→전투 전환은 화면 전환 없음"이라고 적어둔 것과
  같은 무전환 패턴을 Combat→Prep 에도 적용한 셈). `Simulation::Phase()`/`PhaseTimeLeft()`/
  `WaveNumber()` 접근자는 만들어 뒀으니 정산 화면·HUD 타이머는 이 값을 그대로 읽으면 된다.
- **§0.1 자원(`m_supplies`)/재고 게이팅은 없음** — 오드넌스/철조망 배치는 지금도(§4/§6) 무제한
  이다. 상점 UI가 나올 때 같이 붙이는 게 자연스러움.
- **§0.5 에스컬레이션 없음** — 웨이브마다 `SpawnSimAgents()`가 항상 같은 `kActiveCrowd.count`
  로 리스폰(난이도 곡선 없음). `WaveDirector`(호드 설계) 자체가 아직 없어서 자연히 스코프 밖.
- 검증: `kCombatDuration`/`kPrepDuration`을 3초로 임시 단축해 3사이클 관찰 —
  Combat 중 `liveAgents=16384` 유지 → 타이머 만료 즉시 `liveAgents=0`(Prep, 크래시 없음,
  `StepSimAgents`의 기존 "풀 비면 즉시 반환" 그대로 작동) → Prep 만료 즉시 `liveAgents=16384`
  복귀 + `wave` 1→2→3 증가. 전부 원복 후 재검증.

### 0.3 정산 화면 (`GameState::WaveResults`) — 화면 자체는 미구현, 대신 상시 텍스트 HUD 구현됨

**구현노트**: `GameState::WaveResults`/`BuildWaveResultsScreen`(아래 스케치)는 안 만들었다.
대신 `SnapshotBuilder::Build`가 FPS 카운터와 같은 방식(`ui::DrawRect`+`ui::DrawText`, 화면
상단, 매 프레임 재계산)으로 웨이브/페이즈/남은 시간/목표 HP/자원/킬 수를 상시 표시한다
(`simulation.ActiveScene() == DemoScene::DefenseCombat`일 때만, [SnapshotBuilder.cpp](../src/game/SnapshotBuilder.cpp) FPS 블록
바로 위). 우상단엔 같은 방식으로 무기 5종의 키매핑 목록(WEAPONS: LMB 라이플/RMB
화염방사기/1 박격포/2 지뢰/3 철조망)을 고정 표시 — 상점 UI(§0.4)가 없는 동안의 임시 참고용,
정산 화면이 생기면 같이 정리할 대상. "다음으로/타이틀로" 버튼이나 웨이브 종료 시점의 별도 화면 전환은 없음 — 그냥 매
프레임 최신 값이 계속 갈아치워진다. FPS 카운터와 마찬가지로 타이틀 화면 위에도 그려짐(게이팅
안 함) — 개발용 상시 리드아웃이라는 성격상 지금은 문제로 안 봄. 진짜 정산 화면(웨이브 종료
때 잠깐 멈추고 "다음으로" 확인)은 아래 스케치 그대로 여전히 미구현.

`scene-flow-design.md`가 이미 예비해 둔 `GameState` 값 — Title/InGame과 똑같이 `SetScreen`
(전체화면 교체), `Simulation::Step` 은 Title 처럼 건너뜀(오버레이 아니라 상태 전환이므로
게이팅 조건에 자동으로 걸림, `scene-flow-design.md` §2).

```cpp
void Application::EnterWaveResults()
{
    m_state = GameState::WaveResults;
    m_ui.ClearOverlay();
    m_ui.SetScreen(BuildWaveResultsScreen(m_simulation.LastWaveStats(), [this] { EnterPrepPhase(); }));
}
```

- **표시 데이터**(`Simulation::LastWaveStats()`, 값 구조체 하나): 웨이브 번호, 킬 수, 목표 HP
  잔량, 이번 웨이브 획득 자원(§0.1), 패배 시 별도 배너.
- **"다음으로" 버튼**: `EnterPrepPhase()` 하나만 부름 — `GameState::InGame` 으로 복귀 +
  `Simulation::MatchPhase` 는 이미 `Prep`(§0.2 에서 전환됨), 정비 타이머 리셋.
- 패배였으면 버튼이 "다음으로" 대신 "타이틀로" → `EnterTitle()`.

### 0.4 정비 페이즈 (60초, `MatchPhase::Prep`)

`GameState::InGame` 그대로 유지 — 플레이어가 3인칭으로 계속 걸어다니며 배치한다(전투와 같은
조작 스킴, §5의 조준 레이도 그대로 씀). 다른 건 딱 두 가지:

- **좀비 없음**: `MatchPhase::Prep` 이면 `WaveDirector`가 스폰을 멈추고(§0의 `m_agents`는
  이미 §0.2 에서 전부 정리됨), `StepSimAgents`는 활성 개체가 0이라 즉시 반환(기존 관행,
  `horde-design.md` "ActiveIndices() 가 비면 즉시 반환"과 동일).
- **무기 발사 대신 배치/구매**: `WeaponIntent`(§8)의 트리거가 전투 중엔 "발사"였던 게 정비
  중엔 "배치 확정"으로 의미가 바뀐다 — 총/화염방사기는 어차피 대상이 없어 사실상 비활성.
  박격포/지뢰/철조망은 §0.1 의 `m_supplies` 를 소모해 재고를 채우고 원하는 자리에 미리 심는
  용도로 그대로 쓰임(§4·§6, 코드 변경 없음 — "지뢰를 심는다"는 이미 그 설계가 하는 일).
- **타이머 만료**: `m_phaseTimeLeft <= 0` → `m_phase = MatchPhase::Combat`, `m_waveNumber++`,
  `WaveDirector`가 다음 웨이브 스케줄 적용(§0.5) — `GameState` 는 안 바뀜(계속 `InGame`),
  화면상 아무 전환 없이 그냥 좀비가 다시 나타나기 시작.
- **조기 종료(옵션)**: "출발" 버튼으로 60초 다 안 기다리고 바로 전투 시작 — `m_phaseTimeLeft`
  를 0으로 만들기만 하면 끝, 새 로직 불필요.

### 0.5 웨이브 에스컬레이션 (무한 반복의 난이도 곡선)

`WaveDirector::SetSchedule`(호출부는 §8.1 그대로)를 `m_waveNumber` 의 함수로 재적용 —
"고정 스케줄 하나"가 아니라 "웨이브 번호가 스케줄을 만든다":

```cpp
// 매 Combat 진입 시 (Prep → Combat 전환, §0.4)
director.SetSchedule({
    { .atSeconds = 0, .ratePerSec = kBaseRate + kBaseRate * 0.15f * m_waveNumber, .typeId = PickTypeFor(m_waveNumber) },
});
```

- 단순 선형 증가로 시작(YAGNI) — 곡선이 밋밋하거나 급하면 나중에 조정. 특수 개체(`kBruiser`
  등, §8.1 원문 예시)를 웨이브 번호 임계값부터 섞는 것도 이 함수 안에서.
- **`SimAgent` capacity 상한**: 무한 웨이브라 언젠가 `ratePerSec × 웨이브` 가 capacity 를
  넘는다 — §0 의 무한 리스폰 덕에 "동시 생존 수"만 상한이면 되므로, `kActiveCrowd.capacity`
  를 목표 최대 동시 생존 수로 고정해두고 그 이상은 스폰 시도만 실패(무시, 크라우드 관행)로
  자연히 캡핑된다. 별도 상한 로직 불필요.

### 0.6 박격포 미니맵 조준 — 나중, 확장점만 표시

**지금은 안 만든다.** §4 의 박격포는 이미 "타깃 좌표(`math::Vec3`) 하나를 받아 그 자리를
때린다"로 설계돼 있다 — 지금은 그 좌표를 조준 레이(`LookRayResult`)의 지면 히트로 얻지만,
**입력 소스가 뭐든 `PlaceOrdnance(kind, math::Vec3 targetPos)` 시그니처는 안 바뀐다.** 미니맵을
붙일 때:

- 새 `GameState`(또는 `UIContext::SetOverlay` 모달)로 "미니맵 조준" 화면을 하나 얹는다 —
  탑다운 정사영 카메라(기존 `CameraView`, 새 프로젝션만) 또는 2D 미니맵 위젯. 클릭 위치 →
  월드 XZ 로 역변환(정사영이면 역변환이 원근보다 단순).
- 그 좌표를 지금 조준 레이가 하던 자리에 그대로 꽂는다 — `PlaceOrdnance` 호출부만 "레이 히트"
  대신 "미니맵 클릭 좌표"로 바뀌고, 박격포 자체(예약·폭발·데미지·VFX, §4) 는 한 글자도 안
  건드린다(OCP). 이 문서가 지금 더 설계할 게 없다 — 미니맵 UI는 그 시점에 별도 문서로.

---

## 1. 전제 — 좀비가 플레이어를 향해 쏟아지게 (구현 완료)

**지금 `SimAgent`는 목표가 없다.** `Simulation::StepSimAgents`(`Simulation.cpp`)는 헤딩을
사인파로 흔들며 걷고 필드 경계에서 반사할 뿐 — 순수 배회다. "쏟아진다"를 만들려면 최소한의
방향성이 필요하다.

**판단**: `horde-design.md`의 `FlowField`(그리드 전역 경로장) 전체를 지금 끌어올 필요 없음 —
지금 필드는 장애물 없는 열린 공간이라 벽 돌아가기가 필요 없다. **권장(MVP)**: `StepSimAgents`
의 헤딩 계산을 "사인파 드리프트"에서 "목표 지점을 향한 헤딩"으로 한 줄 교체.

```cpp
// StepSimAgents 안, 기존 사인파 드리프트 대신:
const math::Vec3 toGoal = goalPos - a.pos;                 // goalPos = 플레이어 또는 방어선
a.heading = TurnToward(a.heading, std::atan2(toGoal.x, toGoal.z), kSeekTurnRate * fixedDelta);
// 필드 경계 반사 로직은 그대로 두거나(개체가 서로 겹치는 걸 막는 임시 대역), 목표에 가까워지면
// 제거 — §"판단 필요" 참고.
```

- 벽/장애물이 생겨서 개체가 막히는 게 눈에 보이면 그때 `horde-design.md` 의 `FlowField` 를
  실제로 붙인다(문서에 이미 구현 순서까지 있음) — 지금은 그럴 이유가 없다(YAGNI).
- 뭉치는 느낌(밀도 climb, 벽 앞 군집)도 `FlowField` 없이는 안 나온다 — 열린 벌판 러시로
  시작하고, 필요해지면 그 문서로 승격.

**구현 노트** (`Simulation.h/.cpp`):
- `goalPos`가 플레이어 위치가 아니라 **고정 좌표**(`kCrowdGoal`, 메사 아래 한 점)다 — "방어선"
  개념을 플레이어가 서 있는 언덕 위가 아니라 필드 안쪽 고정 지점으로 단순화(§12에서 이미
  이렇게 해결됨). 플레이어를 실제로 쫓아오게 하려면 나중에 `kCrowdGoal`을
  `CharacterPosition()`으로 바꾸면 되지만, 지금은 목표가 고정이라 매 스텝 갱신이 필요 없어
  더 싸다.
- 필드 경계 반사 로직은 "제거"됐다(§12에서 이미 정리) — 옛 사인파 드리프트 코드 자체를
  들어냈다. 남은 건 `a.pos.x`의 루즈 클램프(측면 이탈 방지)뿐.

---

## 2. 공통 데미지 계층 — HP + `DamageEvent` (구현 완료 — 실제로는 `DamageEvent` 큐 없이 직접 처리)

지금 엔진엔 HP/데미지 개념이 전혀 없다(확인됨, grep 0건). 5개 무기가 전부 이 계층에 꽂힌다.

```cpp
// game/Simulation.h — SimAgent 확장
struct SimAgent
{
    // ...기존 pos/heading/speed/phase/animTime/vel/airborne...
    float health{ 100.0f };
    float burnDps{ 0.0f };       // 화염방사기(§7) 잔여 도트 — 0 = 안 타는 중
    float burnTimeLeft{ 0.0f };
    float speedMul{ 1.0f };      // 철조망(§6) 슬로우 배율, 매 스텝 존 안이면 재적용·밖이면 1로 복귀
    void Reset() { *this = SimAgent{}; }
};

// 무기가 이 스텝에 낸 피해를 모아두는 큐. Simulation::Step 안, ParallelFor **밖**에서 채우고
// 소비한다(불변 규칙 6 — 워커가 공유 벡터에 push 하면 안 됨).
struct DamageEvent { std::uint32_t agentSlot; float amount; };
std::vector<DamageEvent> m_pendingDamage;   // 매 스텝 시작에 clear, 무기 시스템들이 push, 스텝 끝에 일괄 적용
```

- **적용 순서(고정 스텝 안)**: ① 이번 스텝의 무기 입력 처리(총 발사, 화염방사기 원뿔 등) →
  `m_pendingDamage`에 push(메인 스레드, `ParallelFor` 시작 전이라 안전) → ② `StepSimAgents`
  (기존 이동 + `speedMul`/`burnDps` 적용, 여기서도 아직 슬롯 쓰기만) → ③ 스텝 끝, 메인에서
  `m_pendingDamage` 순회하며 `health -= amount`, `health <= 0` 이면 `state=Die`(§3 트리거) →
  ④ `Release` 는 항상 그렇듯 다음 스텝 시작 전 메인에서.
- **사망 판정**: `SimAgent`엔 지금 `state` 필드가 없다(씬 2는 순수 배회라 필요 없었음) — 이
  계층에서 `std::uint8_t state{Alive, Dying, Dead}` 를 같이 추가한다. `Dying` 은 사망 애니메이션
  재생 중(있다면) 또는 즉시 `Dead`→기브 스폰→`Release`.
- **거리 감쇠 폭발 데미지는 이미 있다** — `Simulation::TriggerExplosion`(크라우드 폭발 데모)이
  거리 선형 감쇠(`falloff`)를 이미 계산해 넉백에 쓴다. `amount = falloff * kExplosionDamage` 로
  같은 루프에서 `DamageEvent`도 push하면 끝 — 새 로직 아님, 기존 함수에 한 줄.

**구현 노트** (`Simulation.h/.cpp`):
- **`DamageEvent`/`m_pendingDamage` 큐는 안 만들었다.** 실제로 데미지를 넣는 세 경로
  (`DamageAgent`=총, `TriggerExplosion`=폭발, `ApplyFlameCone`이 세팅한 `burnDps`를
  `StepSimAgents`가 소비)가 전부 이미 "메인 스레드·`ParallelFor` 시작 전" 또는 "워커 안이지만
  자기 슬롯만"이라는 안전 조건을 만족해서, 큐에 모았다가 스텝 끝에 일괄 적용할 이유가
  없었다(YAGNI) — 매번 즉시 `health -=`.
- **`std::uint8_t state{Alive,Dying,Dead}` 열거형도 안 만들었다.** `health <= 0.0f`를 그
  자체로 "죽음" 판정으로 쓴다 — 문서가 제안한 3상태 열거형보다 한 단계 더 단순화. `Dying`
  전이(사망 애니메이션 재생 중)에 해당하는 상태는 없음 — §3 판단대로 래그돌/사망 애니 자체를
  스코프 밖으로 뒀으니 자연히 필요 없어졌다.

---

## 3. 요청 1 — 사망 시 파츠 분해 (구현 완료)

**피 스프레이 파티클로 보강됨**: 강체 큐브 조각(아래)에 더해, 터지는 순간의 붉은 스프레이도
[particle-system-research.md](particle-system-research.md) §7.3 `kGibBloodSpray`/
`vfx::SpawnGibBurst` — **구현 완료**, `SpawnGibs`가 기존 큐브 조각 산개 루프 바로 앞에서 호출.

**진짜 메시 절단은 하지 않는다.** 이유는 mushroom 문서 검토 때와 같다 — 이 엔진엔 런타임 메시
분해/컴퓨트가 없고, 인스턴싱된 좀비(수천 개 잠재)마다 서브메시 분리를 하면 그 자체가 새
렌더 경로다. **대신 "기브(gib) 스폰"** — 폭발 넉백(`SimAgent::airborne`)과 완전히 같은 패턴을
작은 정적 파츠 메시에 재사용한다.

```cpp
// game/GibConfig.h — CrowdConfig.h/ParticleEffectDef 와 같은 constexpr 테이블 관행
struct GibDef { render::MeshId mesh; int countMin, countMax; float speedMin, speedMax; float life; };
inline constexpr GibDef kZombieGibs{
    .mesh = render::MeshId::Cube /* 1차: 임시 큐브. 실제 팔/다리/머리 저폴리 메시로 후속 교체 */,
    .countMin = 3, .countMax = 5, .speedMin = 2.0f, .speedMax = 5.0f, .life = 1.5f };

// game/GibPiece.h — 크라우드와 완전히 분리된 자기 SoA/AoS(SRP: 필요 데이터가 다르다)
struct GibPiece { math::Vec3 pos, vel; float life; void Reset() { *this = GibPiece{}; } };
// core::ObjectPool<GibPiece> m_gibs{ 256 };   // 동시 사망 몇십 마리 x 파츠 몇 개면 충분
```

- **스폰 시점**: §2 의 사망 판정(`Dead` 전이) 순간, 죽은 좀비의 `pos`에서 `kZombieGibs.countMin..Max`
  개를 랜덤(결정적 시드 — 이 엔진의 기존 무-RNG 관행, `SeedAgent` 처럼 슬롯 인덱스 기반 해시) 방향
  +위쪽 임펄스로 `Acquire`.
- **스텝**: `GibPiece`도 `airborne` 좀비와 똑같은 중력 적분(코드 재사용 — 헬퍼 함수로 뽑아 두 곳이
  같이 쓰게 하면 DRY, `math::IntegrateBallistic(pos, vel, dt)` 정도). `life` 다 되면 메인에서 `Release`.
- **렌더**: 좀비 크라우드 배치와는 **다른 배치**(다른 메시, VAT 없음 — 정적 바인드포즈만) —
  `instanced-rendering.md`의 `MeshInstance`/`InstanceBatch` 그대로, 새 패스 불필요.
- **애니메이션 없음** — 정적 메시가 탄도로 날아가다 멈추는 것만으로 "터져 흩어짐"이 충분히
  읽힌다(참고: mushroom 문서 검토에서도 이 수준이 이 엔진 스케일에 맞는 선이라고 판단).

**구현 노트** (`GibConfig.h`, `Simulation.h/.cpp`, `SnapshotBuilder.cpp::BuildCliffScene`):
- `GibConfig.h`에 `mesh` 필드 없음 — `CrowdConfig.h`처럼 game/은 render-헤더-프리 유지, 큐브는
  `SnapshotBuilder`가 하드코딩. `GibPiece`는 자기 `selfIndex`/`selfGeneration`을 들고 있다가
  `StepGibs`에서 그걸로 `Release` — `ObjectPool<GibPiece>::Handle`을 `GibPiece` 멤버로 못
  두는(자기 타입이 아직 미완성) 문제를 피하려고.
- 스폰 지점이 둘: (1) `DamageAgent`(메인 스레드, `ParallelFor` 밖) — 그 자리에서 바로
  `SpawnGibs`. (2) 폭격으로 공중에 뜬 좀비가 땅에 닿는 순간(`StepSimAgents`의 airborne 착지
  분기, **워커 안**) — `ObjectPool::Acquire`가 안전하지 않으므로 `reachedGoal`과 똑같은 패턴
  으로 `SimAgent::needsGibSpawn`/`pendingGibPos`에 깃발만 꽂고, `.Wait()` 이후 직렬 패스에서
  실제로 `SpawnGibs` 호출(rule 6).

---

## 4. 요청 2 — 박격포 / 지뢰 (배치형 지연 폭발) (구현 완료)

두 무기가 사실 "즉시 vs 트리거" 차이뿐인 같은 메커니즘 — `Simulation::TriggerExplosion`(이미
구현됨, §2 에서 데미지도 내도록 확장)을 예약/트리거로 감싸는 얇은 계층.

```cpp
// game/Ordnance.h
enum class OrdnanceKind : std::uint8_t { Mortar, Mine };
struct PlacedOrdnance
{
    OrdnanceKind kind;
    math::Vec3   pos;
    float        fuseSeconds{ 0.0f };   // 박격포: 카운트다운. 지뢰: 안 씀(근접 트리거)
    float        radius, power, damage;
    void Reset() { *this = PlacedOrdnance{}; }
};
// core::ObjectPool<PlacedOrdnance> m_ordnance{ 64 };   // 동시 배치 수십 개면 충분, SoA 불필요
```

- **배치 위치**: 플레이어 조준 레이의 지면 히트 포인트 — `LookRayResult`(씬 2에 이미 있음,
  `Simulation::UpdateCrowdQueries`가 매 프레임 채움)를 그대로 읽는다. 새 레이캐스트 코드 불필요.
- **박격포**: 배치 즉시 `fuseSeconds` 카운트다운(포탄 비행 시간 연출, 실제 탄도는 안 시뮬레이션
  — 낙하 이펙트는 §"파티클" 참고), 0 되면 그 자리에서 `TriggerExplosion` 호출 + `Release`.
- **지뢰**: 배치 후 대기, 매 스텝 `CollisionWorld3D`(이미 있는 3D 균일 그리드 브로드페이즈)에
  반경 질의(`RaycastAny`류가 아니라 "이 지점 반경 R 안에 좀비가 있는가" — 크라우드가 이미
  구체 콜라이더로 등록돼 있으므로 `Contacts()` 또는 별도 원형 오버랩 질의) → 하나라도 걸리면
  트리거.
- **폭발 VFX**: `particle-system-research.md` §7.2 의 `SpawnExplosion`(섬광+화구+줄기/갓+불티)
  그대로 호출 — 이 문서가 새로 설계할 VFX 없음.
- **데미지**: §2 에서 이미 `TriggerExplosion`에 붙인 `falloff*damage`.

**구현 노트** (`Ordnance.h`, `Simulation.h/.cpp`, `SnapshotBuilder.cpp::BuildCliffScene`):
- 지뢰 트리거는 `CollisionWorld3D` 질의가 아니라 `TriggerExplosion`이 이미 하던 것과 같은
  **직접 선형 스캔**(`m_agents.ActiveIndices()` 순회 + XZ 거리 제곱 비교)으로 구현 — 새 콜라이더
  질의 API를 추가하는 대신 기존에 검증된 패턴을 재사용. 오드넌스 수(수십 개, `kOrdnancePoolCapacity`)
  x 크라우드 수 스캔이 지금 규모엔 충분히 싸다 — 많아지면 §6 철조망 절과 같은 그리드 승격 후보.
- `PlacedOrdnance`도 `GibPiece`와 같은 이유로 `selfIndex`/`selfGeneration`을 들고 자기
  `Release`를 스스로 한다.
- 폭발 VFX(`particle-system-research.md` §7.2 `vfx::SpawnExplosion` + `Simulation::SpawnFireChunks`
  의 3D 코어)는 **구현 완료** — `TriggerExplosion` 안에서 반경 0 가드 직후 호출, F키 데모 블라스트
  포함 모든 폭발 경로가 공유.
- **배치 UI(조준 프리뷰)·`WeaponIntent`(§8)는 아직 없음** — v1은 총(§5)과 같은 최소 연결:
  `Simulation::PlaceOrdnance(kind)`를 `Application`이 키 입력(데모용 '1'=박격포/'2'=지뢰,
  실제 정비 페이즈 UI는 §0.4)에서 바로 호출, 조준점은 `m_lookRay.point` 그대로. 여러 무기가
  실제로 조준 프리뷰를 공유해야 할 때 §8 프레임워크로 승격.
- 검증: WARP 소프트웨어 렌더러 환경이라 룩레이 히트율이 낮아(카메라가 크라우드를 자주 못 맞춤)
  직접 재현은 어려웠지만, 15초 실행 중 우연히 명중한 배치 1회가 밀집 크라우드에서 즉시
  45마리 킬(`kills` 2→47)을 냈고 그 사망들이 기브 스폰(§3)까지 정상 연쇄됨을 확인.

---

## 5. 요청 3 — 총 (단일 개체 사격) (구현 완료)

**이미 있는 인프라가 정확히 이 기능이다.** 씬 2의 `LookRayResult`(플레이어 시선 레이 →
`RaycastClosest`, mask=크라우드 레이어)는 지금 "보여주기 + 하이라이트"용이었는데, 그 결과의
`agentSlot`을 데미지로 소비하기만 하면 총이다.

```cpp
// Application (플레이어 입력 → Simulation)이 발사 버튼을 감지하면
if (m_input.MousePressed(0) /* 좌클릭 = 발사, 예시 */)
    m_simulation.FireWeapon(WeaponKind::Rifle);

// Simulation::FireWeapon (고정 스텝 안, ParallelFor 밖이라 안전)
void Simulation::FireWeapon(WeaponKind kind)
{
    if (kind == WeaponKind::Rifle && m_lookRay.hit)
        m_pendingDamage.push_back({ m_lookRay.agentSlot, kRifleDamage });
    // 총구 이펙트: vfx::SpawnMuzzleFlash(...) — particle-system-research.md §7.1
}
```

- **연사/쿨다운**: v1은 클릭당 1발 + 최소 간격(`kFireCooldown`)만 — 탄창/재장전은 스코프 밖.
- **총구 소켓 트랜스폼**: `particle-system-research.md` §11 이 이미 열어둔 문제 그대로 — 캐릭터
  전방 고정 오프셋으로 근사(그 문서의 임시안 그대로 채용).

**구현 노트** (`Application.cpp`, `Simulation.cpp`):
- **연사로 전환됨** (원래는 엣지 트리거 클릭당 1발이었으나 이후 요청으로 전체자동으로 변경).
  `m_input.MousePressed(0)` → `MouseDown(0)`(누르고 있는 동안 매 프레임 트리거)로 바꾸고,
  실제 발사 페이스는 `Simulation::kRifleFireInterval`(0.1초 = 초당 10발) 쿨다운
  (`m_rifleCooldown`, `Step()`에서 매 고정 스텝 감소)이 담당. `FireWeapon`이 `bool`을 반환해
  "이번 호출이 실제로 쐈는지"를 알려주므로, `Application`은 그 값으로만 SFX(`blip.wav`)를
  재생 — 매 프레임이 아니라 진짜 발사된 프레임에만. 빗나간 샷도 쿨다운은 정상 소모(실총과
  동일). 탄창/재장전은 여전히 스코프 밖.
  (§7 화염방사기는 발사 트리거 자체가 다르므로 이 무기와 무관.)
  총구 소켓/머즐 플래시 VFX는 **구현 완료**(`particle-system-research.md` §7.1
  `vfx::SpawnMuzzleFlash`) — `Simulation::MuzzleSocketPosition()`(눈 기준 우측·아래로 오프셋된
  가상 총구, `particle-system-research.md` 구현 노트 "화염 이펙트가 카메라를 가림" 참고)에서
  스폰. SFX는 여전히 기존 `blip.wav` 데모 훅 재사용(전용 머즐 SFX는 스코프 밖).
- **조준 디버그 표시(임시)**: 현재 무기 + 이번 프레임 룩레이 결과(명중 슬롯/좌표/거리, 또는
  "NO HIT")를 `SnapshotBuilder`가 화면 중앙 상단에 텍스트로 상시 출력
  (`simulation.ActiveScene() == DemoScene::DefenseCombat`만) —
  실제 크로스헤어·무기 선택 UI가 생기면 제거할 플레이테스트용 임시 표시.

### 5.1 시각 피드백 — 트레이서 · 머즐 플래시 대비 · 반동(연사 탄퍼짐) (구현 완료)

세 가지 다 플레이테스트 요청으로 추가. `Simulation.h`/`.cpp`, `SnapshotBuilder.cpp`만 건드림 —
새 렌더 패스·셰이더 없음.

- **트레이서(직선 이펙트)**: `Simulation::TracerLine{ start, end, ageLeft, life }` 값 타입을
  `m_tracers`(단순 `std::vector`, 수명 `kTracerLife`=0.06초라 동시 존재량이 거의 1개뿐 —
  `core::ObjectPool` 오버킬, `Step()`에서 swap-remove로 직접 청소)에 쌓고
  `SnapshotBuilder::BuildCliffScene`이 매 프레임 `render::debug::Line`으로 그린다.
  **새 파티클/셰이더가 아니라 기존 `DebugDrawPass`(룩레이 시각화에 이미 쓰던 라인 렌더링)를
  재사용** — `vfx::Particle`의 속도 방향 스트레치는 최대 2.5배(`SnapshotBuilder::
  kMaxStretch`)로 캡돼 있어 수십 미터를 가로지르는 "선"으로는 너무 짧다. `DebugDrawPass`는
  블렌드 스테이트가 기본값(불투명)이라 알파 페이드는 안 되지만, 수명이 0.06초(약 3~4프레임)
  뿐이라 그냥 사라져도 눈에 안 띔.
- **머즐 플래시 대비(라이팅 살짝 어둡게)**: 발사 성공 시 `m_muzzleFlashTimer =
  kMuzzleFlashDarkenTime`(0.08초)를 세팅, `Simulation::MuzzleFlashDarken()`(0..1, 발사 직후
  1)을 `SnapshotBuilder::BuildLighting`이 읽어 `key.color.a`·`ambient.sky/ground`를
  `1 - darken*kMuzzleFlashDarkenAmount`(최대 35%)만큼 곱해 낮춘다 — 새 포스트프로세스 없이
  이미 있는 조명 값을 프레임마다 살짝 어둡게 재계산하는 것뿐이라 `Frame` cbuffer 불변 규칙과
  무관.
- **반동(연사 탄퍼짐)**: `m_rifleSpread`(라디안, 원뿔 반각)가 샷마다 `kRifleSpreadPerShot`
  증가(`kRifleSpreadMax` 상한), 쏘지 않는 동안 `Step()`에서 `kRifleSpreadDecayPerSec`로
  회복 — 흔한 FPS 블룸 패턴("빠르게 커지고 천천히 준다"). **실제 탄착점이 바뀐다** — 크로스헤어
  용 `m_lookRay.dir`은 그대로 두고(UI/배치 정밀도 유지), `FireWeapon`이 `ApplyRifleSpread`로
  퍼뜨린 방향으로 **자체 레이캐스트**를 새로 쏴서 데미지·트레이서 끝점을 결정한다(`vfx::
  ParticleSystem::SpawnBurst`와 같은 phi/theta 원뿔 스캐터 수식을 별도로 복제 — SRP, 셋 다
  서로 내부를 몰라도 됨).
- **크로스헤어 UI (실제 탄퍼짐 각도 투영)**: 처음엔 정규화값(0..1)을 임의 픽셀 범위(6~40px)에
  선형 매핑했는데 — 실제 탄퍼짐 각도(`kRifleSpreadMax`=0.09rad≈5.16°)를 카메라 수직
  FOV(`kCameraFovY`=`kPi/3`=60°)로 투영하면 1080p 기준 최대 간격이 약 84px가 나와 절반 가까이
  과소평가돼 있었고, 해상도가 바뀌어도 안 따라갔다(고정 픽셀이라). 지금은
  `spreadPixels = viewportHeight/2 * tan(spreadRad) / tan(fovY/2)`로 **진짜 탄착 반경을
  픽셀로 투영** — 크로스헤어 간격이 실제로 총알이 떨어질 수 있는 범위와 기하학적으로 일치한다
  (해상도가 바뀌어도 `viewportHeight` 항 덕분에 그대로 맞음). 정지 상태(spread=0)에서도 보이게
  더하는 `kRestGapPixels`(6px)만 순수 미관용 — 그 위에 더해지는 벌어짐 자체는 근사가 아니라
  정확한 투영. `BuildCamera`의 FOV 리터럴을 `kCameraFovY` 상수로 빼서 카메라와 크로스헤어가
  같은 값을 쓰도록 함(드리프트 방지).

### 5.2 지형 레이캐스트 — 크라우드를 안 보고 있어도 조준 가능 (구현 완료)

기존 `m_lookRay`는 크라우드 콜라이더만 쏘는 레이캐스트라(`UpdateCrowdQueries`), 좀비가 없는
빈 땅/허공을 보면 `hit=false`라 §4 박격포/지뢰·§6 철조망을 **아무 데도 배치할 수 없었다**.
`Simulation.cpp`의 `RaycastTerrain(origin, dir, maxDist, outPoint)`가 언덕 지형
(`HillHeightAtZ`)에 대한 폴백 레이캐스트를 march-and-bisect(지형이 단순 경사라 닫힌 해를 풀
가치가 없음, 이 파일의 다른 선형 스캔들과 같은 "데모 등급이면 충분" 판단)로 제공 — 크라우드
미스 시 이걸로 재시도해 `m_lookRay.hit=true`(단 `hitAgent=false`)를 채운다.

`LookRayResult`가 `hit`(에이전트든 지형이든 "조준할 점이 있다")과 `hitAgent`(진짜 크라우드
명중, `agentSlot` 유효)로 나뉜 것도 이 때문 — 라이플의 `DamageAgent` 호출은 `hitAgent`만
보고, 배치 계열(`PlaceOrdnance`/`PlaceSlowZone`)은 `hit`만 본다. 이 구분이 없으면 지형 히트가
`agentSlot`의 sentinel 값(0)을 크라우드 0번 슬롯 명중으로 오인해 엉뚱한 좀비에게 데미지가 들어갈
뻔했다.

---

## 6. 요청 4 — 철조망 (슬로우 존) (구현 완료)

가장 단순한 기능 — 새 콜라이더 타입도 필요 없다.

```cpp
// game/SlowZone.h
struct SlowZone { math::Vec3 center; float radius; float speedMul; };
// std::vector<SlowZone> m_slowZones;   // 배치형, 수십 개 이하 — 선형 스캔으로 충분
```

- **적용**: `StepSimAgents`의 속도 계산에 한 단계 추가 — `a.speedMul`을 매 스텝 1로 리셋한 뒤,
  겹치는 모든 `SlowZone`(선형 스캔, 존 개수가 적음)에 대해 `min(a.speedMul, zone.speedMul)`.
  실제 이동 = `dir * (a.speed * a.speedMul * fixedDelta)`.
- **배치**: §4 의 박격포/지뢰와 같은 방식(조준 레이 지면 히트 포인트).
- **시각**: 정적 프롭(`MeshInstance`, 회전 없음) — 애니메이션·VAT 없음.
- **데미지 없음** — 요청에 없으니 순수 CC(crowd control)로 스코프 고정. 나중에 필요해지면
  `SlowZone`에 `dps` 필드 추가(§7 화염방사기와 같은 도트 적용 경로 재사용).
- **존이 많아지면(수백 개)**: 선형 스캔이 병목 → `horde-design.md` §3.4 의 `PileField`와 같은
  그리드 패턴으로 승격(셀당 존 리스트). 지금 규모(플레이어가 손으로 심는 수십 개)엔 과함.

**구현 노트** (`SlowZone.h`, `Simulation.h/.cpp`, `SnapshotBuilder.cpp::BuildCliffScene`):
- `a.speedMul`을 `SimAgent`에 두지 않고 `StepSimAgents` 워커 안 지역 변수로 매 스텝 새로 계산 —
  스텝 사이에 다른 코드가 읽을 일이 없어서 영구 필드로 만들 이유가 없었음(YAGNI).
- `m_slowZones`(`std::vector`, 읽기 전용 캡처)는 워커 안에서 안전 — 배치(`PlaceSlowZone`)가
  전부 메인 스레드·`Step()` 밖에서만 일어나 워커가 도는 동안 절대 안 바뀐다(rule 6).
- 검증: 전체 필드를 덮는 임시 반경(200)의 존을 t=3s에 놓고 크라우드 200개체 평균 전진 속도를
  전/후로 비교 — 존 전 ≈0.91 z/s → 존 후 ≈0.23 z/s(비율 ≈0.25, 설정한 `kWireSpeedMul`=0.35에
  근접, 리스폰/골 도착 노이즈 감안).

---

## 7. 요청 5 — 화염방사기 (원뿔 도트 데미지) (구현 완료)

유일하게 "판정 로직"이 새로 필요한 무기 — 방향+각도 필터.

```cpp
// Simulation::FireWeapon(WeaponKind::Flamethrower) 가 매 스텝(트리거 held 동안) 호출
void Simulation::ApplyFlameCone(math::Vec3 origin, math::Vec3 dir, float range, float halfAngleCos)
{
    // 1차 후보 좁히기: CollisionWorld3D 의 구체 오버랩(반경 = range, 이미 있는 브로드페이즈)
    // 2차 필터: 각도 — dot(normalize(agentPos - origin), dir) > halfAngleCos
    for (agent in candidates)
        if (in cone) { agent.burnDps = kFlameDps; agent.burnTimeLeft = kFlameRefreshSeconds; }
}
```

- **도트 유지**: 원뿔에 맞는 동안 매 스텝 `burnTimeLeft`가 갱신(리필)되고, `StepSimAgents`가
  매 스텝 `burnTimeLeft > 0` 이면 `DamageEvent{slot, burnDps*fixedDelta}` 를 push + `burnTimeLeft -= dt`
  — 트리거를 놓아도 남은 시간만큼 계속 탄다(화염방사기의 실제 느낌).
- **`DamageEvent` push 위치 주의**: 이건 `StepSimAgents`의 `ParallelFor` **안**에서 하면 안 된다
  (공유 벡터 push, 규칙 6 위반) — 각 워커가 "이번 스텝 도트 데미지"를 자기 슬롯의 로컬 값으로만
  기록(`SimAgent`에 `float pendingBurnDamage` 임시 필드 또는 그냥 `health`를 워커가 직접 깎아도
  됨 — 자기 슬롯만 쓰므로 무방, `DamageEvent` 큐는 "다른 시스템이 낸 피해"를 모을 때만 필요).
  즉 도트는 워커 안에서 자기 `health -= burnDps*dt` 직접 처리 가능(규칙 6이 금지하는 건 "겹치는
  범위/공유 카운터"이고, `health`는 자기 슬롯 필드라 안전) — `DamageEvent` 큐는 총/폭발처럼
  **스텝 시작 전에 결정된, 외부에서 들어오는** 피해에만 쓰면 된다. 두 경로가 공존해도 문제없음.
- **파티클**: 화염 콘 자체(연속 가산 블렌드 스프라이트) — **구현 완료**. `ParticleEffects.h`의
  `kFlameJet`, `FireWeapon`이 `ApplyFlameCone` 직전에 매 프레임 `vfx::SpawnFlameJet` 호출(연속
  이미터, §6.1의 "레이트" 확장 그대로). 데미지 로직만 있고 화면엔 아무것도 안 보이던 걸
  플레이테스트로 발견해 뒤늦게 추가(`particle-system-research.md` 구현 노트 참고).

**구현 노트** (`Simulation.h/.cpp`, `SnapshotBuilder.cpp`):
- 도트로 인한 사망은 워커 안에서 일어나므로 `m_killCount`(공유 카운터)를 그 자리에서
  증가시킬 수 없다(rule 6) — `SimAgent::needsKillCount`를 새로 추가해 `needsGibSpawn`과 같은
  자리(`.Wait()` 뒤 직렬 패스)에서 소비. `TriggerExplosion`은 메인 스레드에서 이미
  `++m_killCount`를 하므로 airborne 착지-사망 경로는 이 플래그를 쓰지 않는다 — 두 경로를
  같은 플래그로 합쳤으면 폭발 사망이 두 번 집계됐을 것.
- 도트는 airborne 동안 멈춘다(착지 후 재개) — 두 사망 경로(폭발 착지 vs 화염 도트)가 같은
  스텝에 겹치는 경우를 코드로 안 다뤄도 되게 하려는 단순화.
- 콘 판정은 `CollisionWorld3D` 오버랩 질의 대신 `TriggerExplosion`/`StepOrdnance`와 같은
  직접 선형 스캔(거리 + `dot` 각도 필터) — 기존에 검증된 패턴 재사용.
- 화염 콘은 `FireWeapon`을 총과 달리 **트리거를 누르고 있는 동안 매 프레임** 호출(엣지가
  아니라 held) — `Application`이 우클릭 `MouseDown(1)`으로 연결. 연속 루프 SFX는
  `audio-design.md`에 아직 없어 보류(스팸 방지로 무음).
- 검증: DPS를 임시로 1000까지 올려 사망-집계 경로를 직접 확인(`kills` 0→23→32, 그 즉시
  `burning` 카운트가 0으로 떨어짐 = 사망 시 화염 상태도 같이 정리됨). 실제 튠 값(25 dps)으로는
  WARP 환경의 낮은 조준 재포착률 탓에 burnTimeLeft 갱신은 확인했지만 완전한 사망까지는
  직접 재현하지 않음 — 로직 경로는 부스트 테스트로 동일하게 거쳤음.

---

## 8. 무기 공통 프레임워크

```cpp
enum class WeaponKind : std::uint8_t { Rifle, Mortar, Mine, WireFence, Flamethrower };

// Application → Simulation. PlayerIntent 처럼 "이미 해석된 값"만 (규칙: game 은 InputState 모름).
struct WeaponIntent
{
    WeaponKind selected{ WeaponKind::Rifle };
    bool       triggerHeld{ false };   // 화염방사기 등 연속형
    bool       triggerPressed{ false };// 총/배치형 단발
};
```

- `Simulation::Step`이 `WeaponIntent`를 받아 `selected`에 따라 분기 — 총(§5)·화염방사기(§7)는
  즉시 판정, 박격포/지뢰/철조망(§4·6)은 "배치 확정"(예: 다시 클릭)까지 조준 프리뷰만.
- **탄약/자원(옵션)**: v1 스코프 밖 — 필요해지면 `WeaponDef`에 `ammoMax`/`cooldownSeconds`
  테이블화(`ParticleEffectDef`와 같은 constexpr 관행).

---

## 9. 스레드 / 불변 규칙 매핑

| 규칙 | 이 설계에서 |
|---|---|
| 1 (D3D11 은 렌더 스레드만) | 기브/오드넌스 전부 값만 스냅샷, 렌더 리소스는 기존 `MeshPass3D`/`ParticlePass3D` 경로 재사용 |
| 3 (경계는 값 스냅샷만) | 좀비 HP/burn/기브/오드넌스 전부 `Simulation` 안에 머묾. 스냅샷엔 여전히 `MeshInstance`/`ParticleInstance` 값만 |
| 6 (`ParallelFor` 범위 독립) | `DamageEvent` push 는 워커 밖(총/폭발) 또는 자기 슬롯만(화염 도트). `Acquire`/`Release`(기브·오드넌스·좀비 사망)는 전부 `Wait()` 뒤 메인. §0.2 전멸 처리의 `state=Die` 대입도 자기 슬롯이라 워커 안전 |
| 8 (충돌은 탐지만) | 지뢰 트리거·화염 원뿔 후보 찾기는 `CollisionWorld3D` 탐지만 사용. 데미지 적용(응답)은 physics 밖(`Simulation`) |
| SRP(설계 원칙) | `GameState`(화면·스텝 게이팅) vs `Simulation::MatchPhase`(전투/정비 게임로직)를 분리 — `Application`은 정비 중에 좀비가 없다는 걸 모른다(§0) |

---

## 10. 구현 순서 (측정 게이트마다 멈춤)

1. **§1(좀비가 목표를 향함) + §2(HP/DamageEvent 공통 계층).** 아직 무기 없이 "좀비가 다가오고
   체력이 있다"만 확인.
2. **§5 총** — 이미 있는 레이캐스트 재사용이라 제일 싸다. "쏘면 죽는다" 첫 확인.
3. **§3 기브 스폰(임시 큐브 메시)** — 죽음의 시각 피드백, 실제 저폴리 파츠 메시는 나중 교체.
4. **§4 박격포/지뢰** — `TriggerExplosion` 확장 + 배치 UI(조준 프리뷰).
5. **§6 철조망** — 가장 단순, 선형 스캔.
6. **§7 화염방사기** — 원뿔 판정이 새로 필요해 마지막.
7. **§0.2 전멸 처리 + §0.3 정산 화면(`GameState::WaveResults`)** — "60초 되면 다 죽고 결과
   화면"까지. 이 시점에 처음으로 한 웨이브가 완결된다(승패 없이 "다음으로"만 있어도 확인 가능).
8. **§0.4 정비 페이즈 + §0.1 자원 경제** — 좀비 없는 60초, 배치/구매 UI. `MatchPhase` 전환.
9. **§0.5 웨이브 에스컬레이션** — Prep→Combat 전환 시 스케줄 재적용. 여기서부터 "무한 웨이브".
10. (선택, 나중) **§0.6 박격포 미니맵** — 지금 설계로는 착수 항목 없음(확장점만).

각 단계 독립 커밋 가능, "지금 무기 수로 충분"하면 멈춰도 된다. 1~6(전투 한 번)까지만 해도
데모로 보여줄 수 있고, 7~9가 붙어야 "무한 웨이브 디펜스"가 완성된다.

---

## 11. 사용 방법 (How to use)

### 새 무기 추가

1. `WeaponKind`에 값 추가 + (배치형이면) `PlacedOrdnance`/(즉시형이면) `Simulation::FireWeapon`
   분기 한 줄.
2. 데미지가 필요하면 `m_pendingDamage.push_back` (스텝 시작 전 또는 자기 슬롯 로컬만).
3. VFX는 `particle-system-research.md`의 `ParticleEffectDef` 테이블에 한 줄 추가 — 새 시스템
   불필요.
4. 시각(발사체·설치물)이 메시면 `instanced-rendering.md`의 `MeshInstance` 배치 재사용.

### 튜닝 위치

| 무엇 | 어디 |
|---|---|
| 좀비 최대 HP, 이동속도 | `SimAgent` 기본값 / `SeedAgent` |
| 무기별 데미지·쿨다운·반경 | 각 무기 상수(§4~7 코드 스니펫의 `k*`) — 늘어나면 `WeaponDef` 테이블로 승격 |
| 전투/정비 페이즈 길이 | `m_phaseTimeLeft` 초기값(§0) — 둘 다 60초로 시작, 따로 조정 가능 |
| 웨이브 에스컬레이션 곡선 | `WaveDirector::SetSchedule` 을 부르는 §0.5 의 함수(웨이브 번호 → 레이트/타입) |
| 킬당 자원 획득량 | `kSupplyPerKill`(§0.1, 지금은 고정값 — 타입별 테이블 아님) |
| 기브 개수/속도/수명 | `game/GibConfig.h` `kZombieGibs` |
| 슬로우존 배율 | `SlowZone::speedMul` (배치 시점 파라미터) |

### 하지 말 것

```cpp
// ✗ ParallelFor 워커 안에서 공유 m_pendingDamage 에 push (규칙 6)
m_jobs.ParallelFor(0, n, c, [&](b,e){ for(...) m_pendingDamage.push_back(...); });   // 금지

// ✗ 워커 안에서 GibPiece/PlacedOrdnance Acquire/Release
// — 스텝 끝, 메인에서만 (크라우드 churn과 동일 계약)

// ✗ 좀비 죽음마다 진짜 서브메시 분리/컴퓨트 셰이더 절단 시도 — 이 엔진 스케일 밖(§3 근거)

// ✗ 화염 원뿔/지뢰 반경 질의를 매 스텝 전체 좀비 선형 스캔으로 (좀비가 수천이면)
// — CollisionWorld3D 브로드페이즈로 후보를 먼저 좁히고 세부 필터(각도 등)는 그 후보에만

// ✗ Simulation 이 InputState/Win32 타입을 아는 것 — WeaponIntent 값만 받는다(PlayerIntent 패턴)

// ✗ Application/GameState 가 "지금 정비 중이라 좀비가 없다" 같은 걸 아는 것 (§0 SRP) —
// GameState 는 화면·스텝게이팅만, MatchPhase 는 Simulation 소유. Application 에 if(phase==Prep) 분기 금지

// ✗ 전멸 처리(§0.2)에서 개별 킬처럼 기브를 다 스폰 — 풀 폭증(§12). skipGibs=true 로.
```

---

## 12. 판단 필요 / 열린 질문

**해결됨** (구현하면서 결정 — 아래는 그 결과만 기록, 코드가 근거):
- **필드 경계 반사 vs 목표 추적**: 충돌 안 남. `StepSimAgents`가 옛 사인파 드리프트+반사를
  통째로 목표-추적 스티어링으로 교체했다 — 반사 로직 자체가 크라우드 경로엔 더 이상 없음
  (남은 건 `a.pos.x` 루즈 클램프뿐, 바운스 아님). "반사" 로직은 여전히 `StepOneActor`(씬 1
  전용 캔드 액터)에만 있고 크라우드와는 무관.
- **목표 "도달"의 정의**: 후자(반경 스칼라 비교, `kCrowdGoalRadius`)로 결정 — 콜라이더 접촉이
  아니라 거리 비교. 도달 시 진짜 `Release` 아니라 `RespawnAgentInPlace`(§4.5 원칙과 일치).
- **동시 다수 사망 시 기브 폭증**: 문서가 제안한 그대로 — `Acquire` 실패는 조용히 무시
  (`if (!g) continue;`). 통합 검증에서 실제로 256개 풀이 꽉 차는 것까지 관찰했고 크래시 없음.
- **전멸 타이밍**: 전자(타이머 만료 즉시 전부)로 결정 — `EndCombatPhase`가 그 프레임에
  `m_agentHandles` 전부 `Release`. 원 요청("1분 지나면 바로 모든 좀비가 쓰러지고")과 일치.

**결정했지만 문서 제안과 다른 경로**:
- **화염방사기 원뿔 후보 좁히기**: `CollisionWorld3D` 반경 질의를 새로 추가하지 않고, §4의
  `TriggerExplosion`/`StepOrdnance`와 같은 **직접 선형 스캔**(거리+각도)을 재사용했다 — 이미
  검증된 패턴이라 새 질의 API보다 단순. §11 "하지 말 것"이 "수천 마리면 브로드페이즈로
  좁히라"고 적어둔 것과 문자상 어긋나지만, `StepSimAgents` 자체가 매 스텝 크라우드 전체를
  어차피 순회하므로 지금 규모(16384)에서 추가 O(N) 스캔 하나가 실측으로 문제될 때까지는
  보류 — 측정 게이트, 지금은 안 잼.

**아직 열림** (스코프 밖으로 명시적으로 미룸):
- **철조망 데미지 확장**: 지금은 순수 슬로우. 화염 도트 필드(`burnDps`/`burnTimeLeft`)를
  범용화(`dotDps`/`dotTimeLeft`)해 재사용할 수 있다는 방향만 기록, 두 번째 도트 무기가
  실제로 필요해질 때까지 보류(YAGNI).
- **정비 페이즈 UI(상점 패널)**: §0.1 자원 획득/소비 로직은 이미 구현됐지만(배치 함수 자체가
  자금 게이트) 그걸 보여주는 화면은 없음 — `InGameHud` 갱신 방식(§11 "사용 방법" 참고) 결정
  후 착수.
- **웨이브 에스컬레이션**: `WaveDirector`가 아직 없어 스코프 밖. 나중에 좀비 종류/스펙
  (`typeId`) 확장과 함께 다룰 예정 — 지금은 웨이브마다 항상 같은 `kActiveCrowd.count`로
  리스폰(난이도 곡선 없음).

---

## 13. 관련 문서

- [horde-design.md](horde-design.md) — `FlowField`/`PileField`(목표 추적이 장애물 회피까지
  필요해지면 승격 대상), §4.5 무한 리스폰(§0 이 그 소비처)
- [scene-flow-design.md](scene-flow-design.md) — `GameState::WaveResults`, `Simulation::Step`
  게이팅 원칙
- [particle-system-research.md](particle-system-research.md) — 총구·폭발·화염 VFX 전부 재사용
- [instanced-rendering.md](instanced-rendering.md) — `MeshInstance`(기브·오드넌스·철조망 시각)
- [collider-design.md](collider-design.md) — 레이캐스트·브로드페이즈, 탐지-only 경계
- [demo-scene.md](demo-scene.md) — 씬 2, `SimAgent`, `LookRayResult`
- [entity-lifecycle-design.md](entity-lifecycle-design.md) §3A — AoS+`ObjectPool` 로 충분한 이유
- [time-design.md](time-design.md) — 고정 스텝에서 데미지/도트 적용
- [roadmap.md](roadmap.md) D5 — 이 문서가 그 항목의 실제 설계
- [synopsis.md](synopsis.md) — 장르(대규모 디펜스), 저지 수단 결정에 이 문서가 답이 됨(무기 5종)
