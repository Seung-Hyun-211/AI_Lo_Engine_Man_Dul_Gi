# 서큘러 (Circular) 설계 — 기초 설계 기반 엔진 설계서

**기준 문서: [# Circular 기초 설계.md](<# Circular 기초 설계.md>)** (이하 **기초 설계**).
이 문서는 기초 설계를 엔진 구조로 옮기고 **살을 붙이는** 문서다. 기초 설계는 바뀌지 않는 헌법이고, 이 문서는 그 밑에 있다.

관련 문서: [circular-balance.md](circular-balance.md)(CSV 밸런싱 환경 + 앞으로 늘릴 데이터 표) ·
[circular-art-guide.md](circular-art-guide.md)(**이미지 추가·이름 규칙·애니메이션 사용 방법**) ·
[synopsis.md](synopsis.md)(3D 디펜스와의 공존) · [roadmap.md](roadmap.md)(서큘러 트랙 순서) ·
[ui-architecture.md](ui-architecture.md) · [scene-flow-design.md](scene-flow-design.md) · [time-design.md](time-design.md) ·
[animation-design.md](animation-design.md) · [texture-atlas-and-sprite-pass.md](texture-atlas-and-sprite-pass.md).

---

## 현재 위치 (스냅샷 — 세션을 시작하거나 컨텍스트가 압축된 뒤 **가장 먼저 읽는 곳**)

> 마일스톤이나 큰 작업을 끝낼 때마다 이 블록을 갱신한다. 상세는 아래 절, 순서는 §10, 결정 기록은 §12. **마지막 갱신: 2026-09-23.**

**한 줄 요약**: 2D 뱀서 라이크 "서큘러". 부팅 = 곧장 Circular 씬(타이틀 없음, ESC → 설정 → LOBBY 로 GAME / TEST SCENE). 기준 = 기초 설계(헌법) > 사용자 [확정] > 이 문서의 [살]. **M1·M3·M5·전투 구조 완료, M2 뼈대 → 다음은 M4(스폰 패턴 — 사용자 입력 대기).**

**어디를 읽나** (주제 → 문서)

| 주제 | 문서 |
|---|---|
| 게임 규칙·진행 상태·결정 기록 | 이 문서(§2 플레이어/능력치, §3 무기, §7 UI, §9 격차표, §10 순서, §12 결정) |
| 무기·공격 구조(판정 도형, 경로, 연출, 사용법) | [circular-combat.md](circular-combat.md) |
| CSV 수치·열 정의(무기 열 포함) | [circular-balance.md](circular-balance.md) |
| 그림 추가·이름 규칙·템플릿 | [circular-art-guide.md](circular-art-guide.md) (§4.1 템플릿) |
| 월드 단위·축·화면 배율 | [engine-conventions.md](engine-conventions.md) 2D |
| 다음 할 일 우선순위 | [roadmap.md](roadmap.md) "서큘러 트랙" |

**구현됨 ✅**
- **화면**: 항상 월드 1920×1080 만큼 보이고 창 크기에 맞춰 균일 확대·축소, 비율이 다르면 레터박스(§8). 게임 수치는 전부 월드 단위(1920×1080 화면의 px).
- **필드**: 무한(이동 경계 없음). 몹은 화면 밖(`spawn_radius` 1150 > 화면 모서리 ≈1101)에서 스폰, 초당 100마리, `MobField` SoA 4096.
- **적 (M3, §5)**: `mobs.csv` 4종 — 근접 GRUNT · 탱커 BRUTE · 원거리 ARCHER(화살) · 마법 WITCH(예고 후 떨어지는 장판). 종류는 `weight` 비율로 섞여 스폰. 행동은 숫자(`keep_distance` 0 = 추적, > 0 = 그 거리에서 멈춤) + `attack`(= `mob_attacks.csv` 행, 무기와 같은 열·같은 전투 구조).
- **플레이어**: 그림 64×128, 히트박스 48×72(그림 바닥에서 8px 위) — `player.csv`. 걷기·달리기(Shift)·대쉬(Space, **무적 — 이제 실제로 피격을 막음**)·스태미너. HP(재생·크리·흡혈). **피격**: 몹 접촉·적 공격 → `damage_reduction` 만큼 감소, 한 번 맞으면 `hurt_invuln` 0.5초 동안 추가 피해 없음. **HP 0 → 런 종료 모달(RETRY / LOBBY)**. 캐릭터 4종 + 선택 모달(부팅·F7).
- **능력치**: 기초 4스텟 + 파생 23종 = `StatId` 27개(`Stats.h`, `stats.csv`). 2026-09-23 추가 `defense`·`dot_damage`·`armor_break` 는 **값·표시만**(계산식 [미정]).
- **무기**: 기초 5종(검 부채꼴 / 채찍 캡슐 / 스태프 폭발 투사체 / 단검 관통 / 트럼프 랜덤 피해) + 구형 PULSE·BOLT + 움직이는 공격 3종(BLADE 궤도, VORTEX 소용돌이, SPIKE 럴커식 전진 — 레벨업 풀 전용). 전부 `weapons.csv` 한 행.
- **전투 구조**(`CircularCombat`, [circular-combat.md](circular-combat.md)): 효과(`kEffectSpecs`) × 경로(None/Straight/Polar). 판정 도형 `HitShape` 하나를 판정과 그림이 같이 씀, 몹 반경 포함, 투사체는 스윕 접촉 + 맞힌 몹 기억(`MobRef` 세대).
- **성장**: XP → 레벨업 3택(무기·장신구 6종·스탯 카드 9종·오버플로우).
- **UI**: 로비(GAME / TEST SCENE), 테스트 씬(더미 99999 HP + **콜라이더 초록 외곽선 표시**, **플레이어 피해 없음**), HUD 뼈대(§7.2 영역이 앵커 좌표에, 전부 단색 자리표시), 런 종료 모달(`YOU DIED 시간 LV` → RETRY / LOBBY).
- **데이터·도구**: CSV 10개(`assets/data/circular/`, F5 다시 읽기 / F6 재시작 / F7 캐릭터), 밸런스 시뮬레이터 `tools/run_balance_sim.bat`(기본 무적, `--mortal` 이면 죽으면 멈춤), 이미지 패커 `tools/atlas_pack`, 그림 템플릿 `assets/templates/`.

**임시 🟡**: 붉은 구역 예고 → 돌진(`kChargePattern`, M4 에서 교체) · 텍스트 HUD · 색 사각형 자리표시(스프라이트는 M7) · 단검 킬 수가 광역 무기보다 낮음(밸런스) · **적 수치 전부 [살] 초기값** — 제자리에 서 있으면 약 10~12초 만에 죽는다(평탄한 초당 100마리 테스트 스폰 기준, 원거리·마법이 먼저 맞힘) · 소비처 없는 능력치(`luck`, `ultimate_charge_mul`, `corruption_power`, `defense`, `dot_damage`, `armor_break`).

**없음 ❌**: 적 보스 · 스폰 패턴 3종 · 스테이지·90초·보스·마계숲 노드 · 궁극기 · CardSelect 카드 레이아웃 · HUD 이미지·호버·클릭 · 씬 구조(로딩·타이틀)·난이도 · 월드 스프라이트·애니메이션(M7) · 메타 재화·세이브.

**다음 할 일** (세부 작업표 = §10.1): **M4**(스폰 패턴 — **사용자가 패턴 상세를 주기로 함, 착수 전 확인**) → M6 → M8. M7(아트)·M2 나머지·밸런스(단검 재측정, 적 수치)는 병행 가능.

**사용자 결정 대기**
- §12.3 A~G — **2026-09-23 에 B(90초·보스)·D(오버플로우 상한)·E(대쉬 효율)·A②(오염 의미)를 물었으나 사용자가 창을 닫음. 다시 묻기 전에 먼저 확인.** C(노드 세부)·F(타이머 위치·클리어 보상)는 아직 안 물음. G(플레이어 사망)는 기본안 [살]으로 구현함 — 다른 규칙을 원하면 바꾼다.
- `defense`/`dot_damage`/`armor_break` 계산식·단위·얻는 방법(사용자: "계산식은 나중에").
- 계속 [미정]: §12.2.

**최근 구현 (2026-09-23, M3)**: `mobs.csv`·`mob_attacks.csv` 신설, `balance.csv` 의 `mob_*` 키는 삭제(→ `mobs.csv`), 적 공격 = 전투 구조의 `Team::Enemy`(W7), 예고 후 떨어지는 장판 = 무기 열 `delay`·`origin`. 계획했던 `MobBehavior` 열거·`TelegraphZone` 일반화는 **안 만듦**(KISS — §5.3).

**최근 결정 (2026-09-23)**: 전투 D1~D7 전부 A([circular-combat.md](circular-combat.md) §4) · 보이는 세계 1920×1080 고정 · 무한 필드 · `spawn_radius` 1150 · 몹 그림 발밑 정렬은 M7 때 · BLADE 반경 80 유지 · 플레이어 히트박스 48×72 + 8px 띄움. 전체 목록 §12.1.

**핵심 파일**: `src/game/{Simulation,CircularCombat,HitShape,MobField,Card,Stats,CircularConfig,CircularBalance,SnapshotBuilder,Application,LevelUpScreen}.*`, `assets/data/circular/*.csv`, `assets/templates/`, `docs/# Circular 기초 설계.md`, `docs/images/HUD.png`.

**작업 방식**: 사용자는 명령으로 운전한다. 응답 = ① 바뀐 것 ② 판단 필요 시 옵션(권장 먼저) ③ `경고 N / 오류 N` ④ 커밋은 명시 요청 시에만(`CLAUDE.md`). 설계는 SOLID·KISS·DRY 로 자체 검사하고 문서에 사용법을 남긴다.

---
## 0. 이 문서의 규칙 — 기초 설계를 위반하지 않는다

1. **기초 설계가 우선한다.** 이 문서의 어떤 내용도 기초 설계와 충돌하면 이 문서가 틀린 것이다. 기초 설계를 바꿀 수 있는 건 사용자뿐이다.
2. **살 붙이기만 한다.** 기초 설계에 있는 것은 그대로 옮기고, 비어 있는 곳(예: 무기·장신구 상세, 스태미너 수치)만 제안으로 채운다.
3. **우선순위: [기초] > [확정] > [살].** 사용자가 질문(§12)에 답해 확정한 것은 [확정]이며 [살]보다 우선한다. 확정 기록은 §12.
4. **표기** — 모든 항목에 아래 태그를 단다. 태그가 없는 문장은 설명이다.

| 태그 | 뜻 |
|---|---|
| **[기초]** | 기초 설계 원문(또는 원문의 직접 귀결). 바꾸지 않는다 |
| **[확정]** | 기초 설계에 없던 것을 **사용자가 답으로 확정**한 것(§12 기록). 바꾸려면 사용자에게 묻는다 |
| **[살]** | 기초 설계·확정이 비워둔 곳에 이 문서가 붙인 제안. **사용자 확정 전이라 바뀔 수 있다** — 코드도 데이터 테이블로 두어 쉽게 바뀌게 한다 |
| **[미정]** | 기초 설계나 사용자가 "미정"이라고 한 것. 구현하지 않고 자리만 둔다 |
| ✅ / 🟡 / ❌ | 구현 상태: 됨 / 임시(기초 설계 밖 테스트용 — 정식 것이 오면 대체) / 안 됨 |

5. **삭제된 옛 설계** — 이 브랜치의 초기 설계 문서는 다른 GDD("덱빌딩 × 오토배틀")를 기준으로 했었다(기초 설계로 교체됨). 기초 설계에 없는 것은 **삭제**했다:
   덱 합성·덱 순환 보너스, 미니언 오토배틀 그리드(3x2 진형·시너지). "카드"는 기초 설계의 **"소지 무기"**로 재해석한다(§3).
   코드 이름 `Card*`(`Card.h`, `CardDef`…)는 당장 바꾸지 않고 §3.5 대응표로 관리한다.
6. 3D 디펜스(`DefenseCombat` 등)와는 **별도 씬으로 공존**한다(`DemoScene::Circular`, [synopsis.md](synopsis.md)). 이 문서는 서큘러만 다룬다.

---

## 1. 기초 설계 요약 (추적용 ID)

아래 ID(`B-…`)로 이 문서 전체가 기초 설계와 연결된다.

| ID | 기초 설계 내용 |
|---|---|
| **B-씬** | 씬 종류: 로딩 / 타이틀(**시작** → 난이도 설정[노말·하드·베리하드·하드코어·익스트림·인세인] · **강화**[다회차, 트리는 차후] · **설정** · **종료**) / 인게임 — §7.6 |
| **B-장르** | 뱀서 라이크류 + 다양한 스테이지 콘셉트 |
| **B-규칙** | 직접적인 공격 없음, **소지 무기별 자동 공격**. 플레이어는 **이동만** 가능(걷기, 달리기, 대쉬) |
| **B-스테이지** | 1~5 마계숲(Slay the Spire 같은 라운드 개념, 이동의 선택지) / 6~10 초원(단일 일직선 노선) / 11~15 인간마을(미정) / 16~20 왕국 성(아이작 같은 라운드 개념, 보스방까지 탐험 필요, 보스는 왕의 기사) |
| **B-캐릭터** | 콘셉트: 마왕군 4대장의 xx왕국 침공. 마도기사 / 스컬매지션 / 서큐버스 / 어쎄신리자드 |
| **B-UI** | 규칙 3개(키보드↔마우스 고정/표시, 호버 아웃라인) + 좌상단·우상단·중앙하단 HUD (`docs/images/HUD.png`) + **CardSelect**(`docs/images/CardSelect.png` — 중앙 1장 + 좌우 1장씩, 티어[운]별 색, 카드 구성: 배경/아이콘/세트명/설명·수치/스탯 변화량 — §3.4) |
| **B-무기장신구** | 무기 5종: 검(부채꼴) / 채찍(직선) / 스태프(전기볼트, 닿으면 폭발) / 단검(투사체, 관통 2) / 트럼프 카드(투사체, 랜덤 데미지) — §3.2. 장신구 항목은 여전히 제목만 있고 비어 있음 (HUD 하단 중앙에 소지 무기·소지 장신구 칸은 있음) |
| **B-적** | 분류: 근접, 탱커, 원거리, 마법, 보스. 스테이지 마지막엔 보스. 마계숲=근접 / 초원=근접·탱커 / 인간마을=근접·원거리·탱커·마법 / 왕국 성=근접·원거리·탱커·마법 |
| **B-스폰** | 일방통행(한 방향, 숫자·속도·모양[화살표·직사각형 등]은 테이블) / 플레이어 기준 사각형으로 가둠(방어력 높은 방패병) / 여러 줄 소환, 한 줄은 좌·한 줄은 우 반복 / 추가… |

---

## 2. 규칙과 플레이어 — B-규칙, B-캐릭터

### 2.1 직접 공격 없음 [기초]

- 플레이어 입력은 **이동 관련만** 존재한다: 이동 축, 달리기, 대쉬(+ 궁극기 §2.4, UI 조작).
- **`PlayerIntent` 에 "공격" 필드를 추가하지 않는다.** 공격은 전부 `Simulation` 안에서 무기가 쿨다운으로 자동 실행한다(✅ 현재 구조 그대로: `Simulation::StepCombat` → `CircularCombat::Fire`).

### 2.2 이동: 걷기 · 달리기 · 대쉬 [기초] + 확정 + 수치 [살]

| 동작 | 입력 **[확정]** | 규칙 |
|---|---|---|
| 걷기 ✅ | WASD / 방향키 | 기본 이동. `player.csv` `walk_speed` = 300 px/s × `move_speed` 스탯 |
| 달리기 ✅ | **Shift** 누르는 동안 | 걷기보다 빠름. **스태미너를 소모**한다 [기초: HUD 스테미너 = 달리기·대쉬]. 스태미너가 바닥나면 `run_resume_stamina` 까지 회복될 때까지 달리기 잠금 |
| 대쉬 ✅ | **Space(스페이스바)** | 짧은 시간 고속 이동. **스태미너를 소모** [기초]. **대쉬 중 무적** [확정] ✅ — `PlayerInvulnerable()` 이 대쉬 시간 동안 참이고, 그동안 몹 접촉·적 장판은 피해를 못 주고 적 투사체는 몸을 통과한다(`Simulation::HurtPlayer`, `CombatContext::playerHittable`) |

> **구현 메모 (M1)**: `Simulation::StepCircularPlayer`. 대쉬 입력은 3D 점프처럼 `QueueDash()` 로 래치(0-스텝 프레임 손실 방지)하며 스태미너가 전체 소모량에 못 미치면 **거절**(부분 대쉬 없음). 대쉬 소모 = `dash_cost × dash_cost_mul × (1 + dash_chain_penalty × dash_chain_penalty_mul × 창 안 직전 대쉬 수)`. 헤드리스로 확인한 값(마도기사): 1회 29.6 → 2회 44.3 → 3회 59.1, 최대 스태미너 106 이면 4연속은 거절됨. 수치는 전부 `assets/data/circular/player.csv` + `stats.csv`.

> 키 충돌 없음: 3D 씬에서 Space 는 점프지만, 그 입력(점프·마우스 룩·무기 키)은 `Application::Run` 의 `ActiveScene() != Circular` 가드 블록 안에 있어 서큘러에서는 대쉬로만 쓰인다.

**소모량 규칙 [확정]: 달리기 ≪ 대쉬. 대쉬를 자주 쓰면 달리는 것보다 손해여야 한다.** 그리고 **이 페널티는 스킬(장신구·능력치)로 조절할 수 있어야 한다.**

- 이 규칙을 지키는 방법 [살] — 두 장치를 함께 쓴다:
  1. **스태미너당 이동 효율**이 대쉬 < 달리기가 되도록 소모량을 잡는다(아래 표 계산).
  2. **연속 대쉬 누적 페널티**: 직전 대쉬로부터 `chain_window`(2.0 s) 안에 또 대쉬하면 소모량이 매번 `chain_penalty`(+50%)씩 오른다 → 연타할수록 달리기보다 확실히 나빠진다. 창이 지나면 초기화.
- **스킬로 조절 [확정 요건]**: 다음 능력치가 페널티를 깎는다(§2.6 파생 능력치): `dash_cost_mul`(대쉬 소모 배율), `dash_chain_penalty_mul`(누적 페널티 배율), `dash_cooldown_mul`, `stamina_regen_mul`, `stamina_max_add`. 즉 장신구·레벨업 스탯 카드로 "연타해도 덜 손해"인 빌드를 만들 수 있다.
- 수치는 [살]로 `player.csv` ✅ 에 둔다(표는 기본값, 최대 스태미너는 `stats.csv` 의 `stamina_max`):

| 항목 | 제안값 [살] | 비고 |
|---|---|---|
| 최대 스태미너 | 100 | |
| 달리기 배율 / 소모 | ×1.6 / **15 per s** | 0이면 달리기 불가(걷기로 강제) |
| 대쉬 이동 | ×3.5 속도, 0.18 s (≈189 px) | 방향 = 입력 방향(입력 없으면 마지막 이동 방향) |
| 대쉬 소모 / 재사용 대기 | **30** / 0.5 s | 연속 시 소모 ×(1 + 0.5·연속횟수) |
| 회복 | 30 per s, 마지막 사용 후 0.8 s 뒤부터 | |
| **대쉬 무적** [확정] | 대쉬 지속시간(0.18 s) 동안 | ✅ 몹 접촉·적 공격을 막는다(M3) |
| 피격 무적 [살] | 맞은 뒤 `hurt_invuln` 0.5 s | 대쉬와 별개. 몹 떼에 둘러싸여도 0.5초에 한 번만 맞는다(`player.csv`) |

**효율 계산(제안값 기준, 걷기 300 px/s 대비 "추가로 번 이동거리 ÷ 스태미너")**

| 방식 | 추가 이동거리 | 스태미너 소모 | 효율 (px/스태미너) |
|---|---|---|---|
| 달리기 1초 | (1.6−1)×300 = 180 px | 15 | **12.0** |
| 대쉬 1회 | (3.5−1)×300×0.18 = 135 px | 30 | **4.5** |
| 대쉬 연속 3번째 | 135 px | 30×(1+0.5×2) = 60 | **2.25** |

→ 대쉬 1회도 달리기보다 효율이 낮고(4.5 < 12), 연타하면 더 나빠진다(2.25). 대신 대쉬는 **무적 + 순간 이동**이라는 다른 가치를 판다 — "위험할 때 한 번" 쓰라는 의도.

- 구현 ✅: `PlayerIntent.run` 은 기존 필드, 대쉬는 edge 라 `Simulation::QueueDash()`(`PlayerIntent` 에 공격/대쉬 필드 추가 없음). `Simulation` 이 `PlayerMotion{Idle,Walk,Run,Dash}` 상태·스태미너·연속 대쉬 카운터·무적 타이머를 소유하고, `SnapshotBuilder` 는 읽기 접근자(`Motion()`, `StaminaFraction()`, `PlayerInvulnerable()`, `RunLocked()`)만 쓴다.
- 입력 축은 [engine-conventions.md](engine-conventions.md) §1 의 2D 규약(`+Y 아래`, `Simulation::Step` 에서 한 번만 뒤집음)을 따른다.

### 2.3 플레이어블 캐릭터 4종 [기초] + 컨셉 [살]

**콘셉트 [기초]: 마왕군 4대장의 xx왕국 침공.** (왕국 이름 xx = [미정])

| # | 캐릭터 [기초] | **주력 스텟** [확정: 캐릭터별 주력 스텟 존재 / 배정 = 살] | 역할 [살] | 시작 무기 컨셉 [살] |
|---|---|---|---|---|
| 1 | 마도기사 | 체력 | 균형형 근접 마법 | 마력검(주변을 도는 검기) |
| 2 | 스컬매지션 | 지력 | 원거리 소환형 | 해골 투사체 |
| 3 | 서큐버스 | 오염 | 기동·보조형(상태이상) | 매혹 표식(가까운 적 다수 타격) |
| 4 | 어쎄신리자드 | 민첩 | 고기동 관통형 | 독 단검(직선 관통) |

- **캐릭터별 주력 스텟 [확정]**: 기초 4스텟(체력·지력·오염·민첩, §2.6) 중 캐릭터마다 하나가 **주력**이다(시작 값·성장 보너스가 높음). 위 4쌍의 **배정은 제안 [살]** — 이름과 컨셉에서 유추했다(§12.3 A).
- 이름·역할·무기 컨셉은 **제안**이다. 확정된 건 4종의 이름·콘셉트·**주력 스텟이 있다는 것**이다.
- 캐릭터는 코드에 박지 않고 `characters.csv` ✅ 한 행 = 한 캐릭터. **지금 열**: `id, name(ASCII — HUD 폰트에 한글 없음), main_stat, start_vit, start_int, start_cor, start_agi, start_weapon(weapons.csv 의 무기 id)`. 예정 열: `portrait, ultimate_kills, ultimate_id`([circular-balance.md](circular-balance.md) §확장 표). 지금 시작값은 주력 8 / 나머지 3 [살].
- **선택 UI ✅ (임시)**: 씬 진입 시 "SELECT CHARACTER" 모달(레벨업 모달과 같은 위젯), **F7** 로 언제든 다시 열기 — 고르면 그 캐릭터로 새 런. Esc 는 현재 런 유지. 정식 선택 화면(초상화)은 M2 HUD·M8 메타 이후.
- 결과 [살 수치로 확인]: 마도기사=체력(최대 HP 98), 스컬매지션=지력(무기 피해 ×1.16·공격속도↑), 서큐버스=오염(오염 위력↑, 소비처 미구현), 어쎄신리자드=민첩(이동 +8%·스태미너 116).
- 아트: 캐릭터당 `char/<id>` 아틀라스 그룹([circular-art-guide.md](circular-art-guide.md)). 아트가 없는 동안은 색 사각형.

### 2.4 궁극기 게이지 [기초: "일정 수 적 사냥시 궁극기 사용"] + 확정

- 적을 **`ultimate_kills` 마리** 잡으면 게이지가 찬다 → 사용 가능. **사용 입력 = Q [확정]**. 사용하면 게이지 0.
- **궁극기의 종류(내용)는 [미정]** (사용자 확정: 미정 유지) — 캐릭터별 1종이라는 구조만 잡고 내용은 비워 둔다(`ultimate_id` 자리만). 이전 문서의 캐릭터별 궁극기 컨셉 제안은 **삭제**했다.
- 직접 공격이 없는 게임이라 궁극기는 "발동형 자동 공격" 으로 취급한다 — 즉 **무기 효과 시스템(§3.2)의 효과 태그를 재사용**하고 새 전투 경로를 만들지 않는다. 충전 속도는 능력치 `ultimate_charge_mul`(§2.6)이 조절한다.

### 2.5 재화 2종 [기초: HUD 좌상단 (2)]

- **런 재화**(인게임, 한 판 안에서만) / **메타 재화**(다회차 강화용, 판을 넘어 저장) [기초].
- **런 재화의 사용처 = 상점 노드 [확정: 마계숲 노드에 "상점"이 있음 → 거기서 쓰는 재화]**. 획득처(몹 드롭·보상 등)는 [미정]. 메타 재화의 사용처(다회차 강화)와 획득처는 [미정].
- 자리를 준비한다: `RunState.runCurrency`(메모리), `SaveData.metaCurrency`(파일 — `core` 에 세이브 계층이 아직 없으므로 ❌, `core::Settings.cpp` 가 유일한 선례). 재화 획득량은 능력치 `currency_gain_mul`(§2.6).

### 2.6 능력치 체계 — 기초 4스텟 + 파생 능력치 [확정] (수치·계수는 [살])

**[확정]**: 능력치는 **기초 4스텟**(체력, 지력, 오염, 민첩)이 있고, **캐릭터마다 주력 스텟**이 있다(§2.3). 그 밖에 **행운, 공격 크기, 추가 투사체 갯수, 공격속도** 등이 있으며 **더 추가할 수 있으면 추가한다** — 아래 표는 그 확장 목록이다.

**계산 [살]**: `최종값 = (기본값 + 가산 합) × (1 + 배율 합)`. 가산/배율은 ① 캐릭터 시작 값(`characters.csv`) ② 기초 스텟 포인트 × 계수(`stats.csv`) ③ 장신구(`accessories.csv`) ④ 레벨업 스탯 카드 ⑤ 무기 오버플로우(§3.4) 에서 온다. 능력치가 바뀔 때만 `StatBlock` 을 다시 계산(더티 플래그) — 매 스텝 계산하지 않는다.

**기초 4스텟 [확정 이름 / 효과는 살]**

| 스텟 | 뜻 [살] | 파생으로 주는 효과 (포인트당) [살] |
|---|---|---|
| 체력 (`vit`) | 튼튼함 | 최대 HP↑, HP 재생↑, 받는 피해↓ |
| 지력 (`int`) | 마력 | 무기 피해 배율↑, 궁극기 충전↑, 쿨다운 소폭↓ |
| 오염 (`cor`) | 마계의 오염 — 지속피해·상태이상 계열 | 오염(독·저주) 피해/확률↑, 처치 시 오염 장판·폭발 확률↑ |
| 민첩 (`agi`) | 기동성 | 이동속도↑, 스태미너 최대·회복↑, 대쉬 소모↓, 공격속도 소폭↑ |

**파생/추가 능력치 [확정: 4종 이상 + 확장 / 목록은 살]**

| id | 이름 | 뜻 | 사용처 | 출처 |
|---|---|---|---|---|
| `luck` | **행운** | 치명타 확률·드롭·레벨업 선택지 품질 | 치명타, 재화/XP 드롭, 선택지 롤 | [확정] |
| `attack_size` | **공격 크기** | 무기 범위(반경/폭) 배율 | `RadialPulse` 반경, 투사체·스윙 크기 | [확정] |
| `extra_projectiles` | **추가 투사체 갯수** | 무기가 한 번에 내보내는 수 +N | `NearestBolt` 대상 수, 투사체 무기 발사 수 | [확정] |
| `attack_speed` | **공격속도** | 무기 쿨다운 배율(↓) | 모든 무기 쿨다운 | [확정] |
| `max_hp`, `hp_regen` | 최대 HP / 초당 재생 | 체력 스텟 파생 | `Simulation::m_playerHp` (`StepCircularPlayer`) | [살 추가] ✅ |
| `damage_reduction` | 받는 피해 감소 | 체력 스텟 파생 | `Simulation::HurtPlayer`(받는 피해 × (1 − 값), 0~0.8 로 잘림) | [살 추가] ✅ |
| `defense` | **방어력** | 받는 피해를 줄이는 수치(점수). **계산식 [미정 — 나중에 추가]** | 몹 공격 피해(M3 이후) | [확정: 2026-09-23 사용자 추가] 값·표시만 ✅ |
| `dot_damage` | **지속피해** | 도트 피해의 세기(점수). **계산식 [미정]** | 지속피해 공격(아직 없음) | [확정: 2026-09-23 사용자 추가] 값·표시만 ✅ |
| `armor_break` | **방어력 감소** | 공격이 대상의 방어력을 깎는 양(점수). **계산식 [미정]** | 몹 방어력(M3 `mobs.csv` 이후) | [확정: 2026-09-23 사용자 추가] 값·표시만 ✅ |
| `life_steal` | **체력 흡수** | 처치당 마지막 타격 피해의 비율만큼 HP 회복 | `CircularCombat::Hit`(킬 게이트 — 생존한 대상에게 준 피해는 집계 안 함, 회복량은 `CombatResult.heal` 로 `Simulation` 이 반영) | [살 추가] ✅ |
| `move_speed` | 이동속도 | 걷기·달리기 배율 | `StepCircularPlayer` | [살 추가] ✅ |
| `weapon_damage` | 무기 피해 | 모든 무기 피해 배율 | `CircularCombat::Fire` | [살 추가] ✅ |
| `crit_chance`, `crit_damage` | 치명타 확률/피해 | 행운 파생(계수는 [미정] — 지금은 기초 스텟 계수 없음) | `CircularCombat::Fire`(캐스트당 1회 롤, 무기 전체에 적용) | [살 추가] ✅ |
| `projectile_speed`, `pierce`, `knockback`, `duration` | 투사체 속도/관통/넉백/지속시간 | 무기 부가 성질 | 투사체·장판 무기 | [살 추가] |
| `pickup_range` | 획득 범위 | 젬·재화 자석 | XP/재화 드롭(M0 이후) | [살 추가] |
| `xp_gain_mul`, `currency_gain_mul` | 경험치/재화 획득량 | | `AwardKills`, 상점 재화 | [살 추가] (`xp_gain` ✅, `currency_gain_mul` 은 제외 — 재화 시스템 이후) |
| `stamina_max`, `stamina_regen` | 스태미너 최대/회복 배율 | 민첩 파생 | §2.2 | [살 추가] ✅ |
| `dash_cost_mul`, `dash_chain_penalty_mul`, `dash_cooldown_mul` | 대쉬 소모/연속 페널티/재사용 | **스킬로 페널티 조절** [확정 요건, §2.2] | 대쉬 | [확정 요건] |
| `ultimate_charge_mul` | 궁극기 충전 속도(필요 킬 수 배율) | 지력 파생 | §2.4 | [살 추가] |

- **HUD 상태창(우상단 2-2 팝업)** 에 보여줄 능력치 = 기초 4스텟 + `max_hp`·`move_speed`·`weapon_damage`·`attack_speed`·`attack_size`·`extra_projectiles`·`luck`·`crit_chance` (+ 스크롤로 나머지). (목록 확정 요청: §12.3 A)
- 데이터: `stats.csv` ✅ `stat_id, base_value, min, max, from_vit, from_int, from_cor, from_agi` (`name_ko`/`display_order` 는 M2 상태창 때 추가). 무기·몹 코드는 능력치를 **id 로 조회**(`m_stats[StatId::AttackSpeed]`)하고 하드코딩하지 않는다.
- **구현 ✅ (M1)**: `game/Stats.h` — `StatId`(기초 4 + 파생), `kStatDefs`(기본값), `StatModifiers{add, mul}`, `StatBlock`, `ComputeStats`(헤더 전용). `Simulation::RecomputeStats()` 가 캐릭터 시작값 + 레벨업 스탯 카드(`m_cardMods`)로 다시 계산한다 — **런 시작 · 레벨업 선택 · F5 리로드 때만**(더티), 매 스텝 아님. 소비처 지금: `attack_speed`(쿨다운 ÷), `weapon_damage`(피해 ×), `attack_size`(반경/사거리 ×), `extra_projectiles`(BOLT 대상 +N), `move_speed`, `xp_gain`, `stamina_max/regen`, `dash_*`, **`max_hp`/`hp_regen`(플레이어 HP, 아래)·`crit_chance`/`crit_damage`(캐스트당 1회 롤)·`life_steal`(킬당 회복)·`damage_reduction`(피격, M3)**. 값만 있고 소비처 없음: `luck`(소비처는 **확정** — CardSelect 카드 티어, §3.4. 티어 판정 자체가 미구현), `ultimate_charge_mul`(궁극기, §2.4), `corruption_power`(오염 계열, [미정]), `defense`·`dot_damage`·`armor_break`(2026-09-23 추가 — 계산식 [미정], 상태창에만 표시).
- **플레이어 HP ✅ (신규)**: `Simulation::m_playerHp`(현재값) + `stats[MaxHp]`(최대값). 새 런/캐릭터 선택 시 풀피, `RecomputeStats` 때 최대치로 클램프(스태미너와 같은 패턴). `StepCircularPlayer` 에서 `hp_regen` 만큼 매초 회복. HUD 좌상단에 바로 표시(스태미너 바 위).
- **피격 ✅ (M3)**: 한 스텝에 들어온 피해(닿아 있는 몹 중 가장 센 `contact_damage`, 적 공격 명중 중 가장 센 것) 중 **가장 큰 하나**만 `Simulation::HurtPlayer` 로 — 대쉬 중·피격 무적(`hurt_invuln`) 중·테스트 씬·갓 모드면 무시, 아니면 `× (1 − damage_reduction)` 만큼 깎고 피격 무적 시작. 맞은 동안 플레이어가 빨강/흰색으로 깜빡인다. `defense`·`armor_break` 는 계산식이 [미정]이라 아직 안 쓴다.
- **사망 ✅ (M3, §12.3 G 기본안 [살])**: HP 0 → `RunOver()` — 서큘러 월드 전체 정지(레벨업 모달과 같은 방식) → `Application` 이 모달 **"YOU DIED 분:초 LV n"** + **RETRY(같은 캐릭터 새 런) / LOBBY**. Esc 로 안 닫힌다. 부활·목숨 여러 개 없음.
- 레벨업 **스탯 카드**(`Card.h` `kStatCards`, 9종): 이동속도 +10% · XP +15% · 공격속도 +8% · 공격 크기 +10% · 무기 피해 +10% · 체력/지력/오염/민첩 +2. `StatCardDef{label, stat, multiplicative, amount}` 한 줄 = 카드 한 종. 옛 `PlayerStats`/`PlayerStat` 은 삭제됨.
- 미구현 파생 [살 목록에 있으나 제외]: `projectile_speed, pierce, knockback, duration, pickup_range, currency_gain_mul` — 소비처(투사체 무기·젬·상점)가 생길 때 `StatId` 한 줄 + `kStatDefs` 한 줄 + `stats.csv` 한 행으로 추가한다.

---

## 3. 무기 · 장신구 — B-규칙(소지 무기), B-UI(하단 중앙), B-무기장신구(공란 → 살)

기초 설계는 "소지 무기별 자동 공격"과 HUD 의 "소지 무기 / 소지 장신구" 칸만 정한다. 나머지는 이 절이 채우는 [살]이다.

### 3.1 개념 [살]

- **무기** = 쿨다운마다 **자동으로** 공격하는 것. 레벨이 있다. (옛 설계의 "공격 카드"가 여기 대응.)
- **장신구** = 공격하지 않는 **패시브**. 능력치(이동속도, 경험치 획득량, 스태미너, 무기 쿨다운 등)만 바꾼다.
- 플레이어는 무기 칸 N개, 장신구 칸 M개를 가진다. **N = M = 6, 최대 레벨 5 [확정]** (HUD.png 의 두 칸은 가로로 긴 띠라 다수 슬롯을 보여주는 형태). 코드: `kProgression.maxDeckSlots` = 6 이 무기·장신구 **둘 다의** 칸 상한(✅), 최대 레벨은 CSV `max_level` = 5.

### 3.2 무기 데이터 모델 ✅ (1차 구현)

`game/Card.h` 의 `CardDef` 테이블이 곧 무기 정의다(§3.5 대응표). **효과 표(`CardEffect` → `kEffectSpecs{형태, 도형, 명중 처리}`) + 경로(`path`) + `CircularCombat`** 이 OCP 지점이다 — 무기 종류마다 if 사슬을 만들지 않는다. 구조·판정 규칙은 [circular-combat.md](circular-combat.md).

| 무기(코드명) | 효과 태그 | 기본 | 레벨 스케일 |
|---|---|---|---|
| PULSE | `RadialPulse` — 주변 반경 전부 타격 | 쿨 0.6s, 피해 12, 반경 120 | 피해 +6, 반경 +14, 쿨 ×0.95 /레벨 |
| BOLT | `NearestBolt` — 가까운 N체 타격 | 쿨 0.9s, 피해 22, 사거리 320, 1체 | 피해 +8, 사거리 +20, 쿨 ×0.93, 2레벨마다 +1체 |

- 최대 레벨 5. 새 효과가 필요하면 `CardEffect` 열거자 + `kEffectSpecs` 한 줄(새 도형이면 `HitShape` 표·그리기 표에도 한 줄) — [circular-combat.md](circular-combat.md) §5.
- 캐릭터별 시작 무기(§2.3)는 `characters.csv` 의 `start_weapon` 이 이 표의 `id` 를 가리키는 것으로 정한다.
- **무기 수치는 `weapons.csv` ✅** (`assets/data/circular/weapons.csv`, [circular-balance.md](circular-balance.md)) — `game/Card.h` 의 `kCardDefs` 는 이제 파일이 없거나 깨졌을 때의 **폴백**일 뿐이다(`CircularBalance::Defaults()`). 게임/무기 관련 코드는 전부 `Balance().weapons`(= `m_balance.weapons`)를 읽고 `kCardDefs` 를 직접 참조하지 않는다.

**기초 설계가 정한 무기 5종 [기초] ✅ 구현됨 — PULSE/BOLT 는 이제 레벨업 풀에만 남은 구형 자리표시자(교체 대상, 삭제는 안 함)**

| # | 무기(코드명) | 모양 [기초] | 효과 태그 | 구현 메모 |
|---|---|---|---|---|
| 1 | 검(SWORD) | 부채꼴 | `ArcSwing` | `HitShape::Arc` → `MobField::DamageInShape`(내적 콘 테스트, 몹 반경 포함). 방향 = `m_lastMoveDir`(대쉬가 쓰는 그 값, 재사용) |
| 2 | 채찍(WHIP) | 직선 | `LineSwing` | `HitShape::Capsule` → `DamageInShape`(선분+반폭, 몹 반경 포함) |
| 3 | 스태프(STAFF) | 기본 전기볼트, 닿으면 폭발 | `ExplodingBolt` | 실제로 날아가는 `AttackInstance`(직선 경로) — 스윕 접촉 시 처음 닿은 몹 자리에서 `explode_radius` 원 장판. 사거리 소진만으로는 안 터짐("닿으면"만) |
| 4 | 단검(DAGGER) | 투사체, 관통 2 | `PiercingShot` | `AttackInstance`, 진행 순으로 최대 `CardTargets()`(=관통 수, `NearestBolt` 와 같은 공식 재사용) 마리. 맞힌 몹은 `MobRef` 로 기억해 다시 안 때림 |
| 5 | 트럼프 카드(TRUMP) | 투사체, 데미지 랜덤 | `RandomDamageShot` | `AttackInstance`, 명중 순간 `[damage, damageMax]` 에서 `std::uniform_real_distribution` 롤(명중이 확실해진 뒤에만 굴려 RNG 낭비 안 함) |

- **소비처는 §2.6 그대로**: 전부 `weapon_damage`·`attack_size`(치명타는 캐스트당 1회 롤, 전체에 적용)를 받는다. `LineSwing` 은 반폭도 `attack_size` 로 커진다. `ExplodingBolt` 는 폭발 반경도 `attack_size` 로 커진다.
- **캐릭터 배정 [살, §12.3 A 대기]**: 마도기사→검, 스컬매지션→스태프, 서큐버스→채찍, 어쎄신리자드→단검(`characters.csv`). 트럼프 카드는 시작 무기가 아니라 레벨업 풀에서만 얻는다(4캐릭터·5무기라 하나는 남음).
- **시각 연출은 자리표시자(평면 도형)**: 판정에 쓴 도형 그대로 — 검 = 부채꼴 외곽, 채찍 = 캡슐 외곽, PULSE/폭발 = 점선 원, 투사체 = `hit_radius` 크기 사각형, 색은 `weapons.csv` `color`(`AttackVisual`, [circular-combat.md](circular-combat.md) §2.3). 스프라이트는 M7.
- **알려진 밸런스 격차 [살, 조정 대기]**: 단검(관통형 단일 대상)이 검/채찍/스태프(광역)보다 시간당 킬 수가 눈에 띄게 낮다(헤드리스 20초 테스트: 관통 54 대 나머지 1300+ — **전투 구조 개편(W1 몹 반경 포함·W4 스윕 접촉) 이전 측정값이라 재측정 필요**) — 코드 결함이 아니라 광역 대 단일 대상의 자연스러운 격차. `weapons.csv` 의 `damage`/`cooldown`/`base_targets`(관통 수)로 조정(재빌드 불필요, F5).
- **아직 안 함**: 관통(`pierce`) 능력치·`projectile_speed` 등 §2.6 "미구현 파생" 목록과의 연동(지금은 무기별 CSV 고정값, 플레이어 능력치가 못 건드림).
- **판정 도형·움직임(궤도·소용돌이·럴커)·연출·CSV 를 묶는 구조와 작업 기록(W1~W16 구현됨)은 [circular-combat.md](circular-combat.md).** 움직이는 공격 3종(BLADE·VORTEX·SPIKE)도 거기 §2.5.

PULSE(`RadialPulse`)와 BOLT(`NearestBolt`)는 5종 구현 전에 주변 시스템(능력치 배선·레벨업·CSV 로더)을 검증하던 **구형 자리표시자**다. 5종이 들어온 지금은 어떤 캐릭터의 시작 무기도 아니고 레벨업 풀에만 남아 있다(삭제는 사용자 결정 전까지 보류).

### 3.3 장신구 데이터 모델 ✅

- `AccessoryDef{ name, stat, multiplicative, amount, maxLevel }` (`CircularBalance.h`) — 능력치(§2.6) **가산/배율**만, 레벨 N = N × amount. 효과 실행 코드 없음(패시브라서) — 대쉬 페널티 완화 같은 "스킬로 조절" 요건([확정], §2.2)이 장신구로 구현되는 대표 경로다.
- `accessories.csv` ✅(`assets/data/circular/`, [circular-balance.md](circular-balance.md)) — 기본 6종(AMULET 이동속도, CHARM 행운, RING 최대HP, BLOODSTONE 체력흡수, GAUNTLET 무기피해, BELT 스태미너)은 `CircularBalance::Defaults()`([살] 제안). 소지 장신구는 `Simulation::m_accessories`(`AccessoryInstance` 벡터, 무기 덱과 같은 6칸 상한), `RecomputeStats` 가 소지한 만큼 `StatModifiers` 에 더한다.
- 레벨업 "스탯 카드"(`kStatCards` 9종 — `Card.h`)는 **별도로 유지**한다 — 기초 설계 §3.4 는 "무기·장신구·스탯 카드·오버플로우"를 4개의 공존하는 선택지 종류로 나열하므로, 장신구가 스탯 카드를 대체하지 않는다.

### 3.4 획득 경로 · 레벨업 [확정: 유지] + 세부 [살]

- **경험치 → 레벨업 → 3택을 유지한다 [확정]**(✅ 구현). 선택지 종류: 무기(신규/강화), 장신구(신규/강화), 스탯 카드(§2.6 능력치 소량), **오버플로우**(아래) — 전부 ✅. 최소 1개는 무기(또는 장신구) 카드 보장 ✅(`RollLevelUpChoices` 의 "guaranteed slot").
- **레벨업 UI 는 기초 설계 UI 를 따른다 [확정: "기초문서에 UI 참고"]**: 모달은 마우스 모드로 진입(커서 표시·고정 해제), **마우스가 올라간 선택지는 아웃라인 활성화**, 닫히기 전까지 HUD 입력 모드 전환 규칙은 정지(§7.1). 3택 버튼의 스타일은 HUD.png 의 위젯 톤(초상화·슬롯 틀)과 맞춘다. (지금 `LevelUpScreen` 은 호버 색만 있는 단순 `Button` 3개 — M2 에서 위젯 공통 아웃라인이 생기면 자동으로 적용된다.)
- **카드 배치 [기초 — 기초 설계 "CardSelect" 절 + `docs/images/CardSelect.png`] ❌**: 중앙에 카드 1장 + 좌우에 1장씩(**3택과 일치** — `kProgression.choiceCount` = 3 ✅ 이므로 개수는 이미 맞다). **카드 티어**(운으로 결정)에 따라 **색상별 카드 이미지**를 쓴다 — 이게 `luck` 능력치의 확정된 소비처다(§2.6 에서 "값만 있고 소비처 없음"으로 남아 있던 것 중 하나). 카드 한 장의 구성 [기초 원문]: 카드 배경 → 아이콘 배경 위에 아이콘 → **세트명** → 설명 텍스트(수치 포함, 예: "피해 +6") → **(있다면)** 선택 시 스테이터스 변화량(스탯 카드의 경우 §2.6 파생값 미리보기).
  - 지금 구현은 세로로 쌓은 단순 텍스트 `ui::Button` 3개(`LevelUpScreen.cpp`) — **개수만 맞고 배치·티어·아이콘·설명·변화량은 전부 없다.** 교체 시점은 M2 HUD 위젯(`ImageWidget`/아웃라인) 이후. 티어 판정(운 → 티어 확률표)은 아직 [살]도 없다 — 카드 레이아웃을 만들 때 `luck` → 티어 테이블을 CSV 로 같이 낸다.

**오버플로우 [확정: "최대 레벨 시 '오버플로우' 스탯 소량 추가"] ✅**

- 무기(또는 장신구)가 **최대 레벨(5)** 이면 그 카드는 더 강화되지 않는다. 대신 레벨업 선택지에 **"오버플로우"** 로 나올 수 있고, 고르면 **연관 능력치가 소량 오른다**.
- 구현: `LevelChoice::Type::OverflowWeapon`/`OverflowAccessory`(`Card.h`) — `RollLevelUpChoices` 가 이미 max level 인 무기/장신구를 `UpgradeCard`/`UpgradeAccessory` 대신 이걸로 채운다. 무기는 `CardDef.overflowStat`/`overflowValue`(`weapons.csv` 열, 빈 값 = 그 무기는 오버플로우 제안 안 함), 장신구는 자기 자신의 `stat`/`amount`(장신구는 이미 스탯 전용이라 별도 필드 불필요)를 `Simulation::m_cardMods` 에 **영구 가산**한다(스탯 카드와 같은 버킷 — 소스 아이템이 더 이상 레벨을 추적 안 하므로). **상한(중첩 횟수)은 [미정](§12.3 D)** — 지금은 무제한 누적.
- 헤드리스 검증: SWORD 를 Lv5 까지 올린 뒤에도 계속 골라 10분 시뮬레이션 → 오버플로우 선택지가 나타나고 골랐을 때 `attack_size` 가 실제로 올라감을 확인.

### 3.5 코드 이름 대응표

| 설계 용어 | 현재 코드 | 상태 |
|---|---|---|
| 무기 정의 | `CardDef`(`game/Card.h`) / `Balance().weapons`(`weapons.csv`, 실제로 읽는 곳), `kCardDefs` 는 폴백 | ✅ |
| 소지 무기 | `CardInstance` 벡터 `Simulation::m_deck` | ✅ |
| 무기 효과 | `CardEffect` → `kEffectSpecs` + `CircularCombat`(`Fire`/`Step`) | ✅ |
| 장신구 정의 | `AccessoryDef`(`CircularBalance.h`) / `Balance().accessories`(`accessories.csv`) | ✅ |
| 소지 장신구 | `AccessoryInstance` 벡터 `Simulation::m_accessories` | ✅ |
| 스탯 카드(장신구와 별개로 유지) | `StatCardDef` / `kStatCards`, `Simulation::m_cardMods` (`game/Card.h`, `Stats.h`) | ✅ |
| 능력치 | `StatId` / `StatBlock` / `ComputeStats` (`game/Stats.h`), `m_stats` | ✅ |
| 레벨업 선택지 | `LevelChoice`, `RollLevelUpChoices`, `LevelUpScreen` | ✅ [확정] |
| 오버플로우(최대 레벨 시 능력치 소량) | `LevelChoice::Type::OverflowWeapon`/`OverflowAccessory` | ✅ [확정] |
| 이름 변경 | `Card*` → `Weapon*` | ❌ 필요해질 때 한 번에(문서·코드 동시) |

---

## 4. 스테이지 · 런 구조 — B-스테이지

### 4.1 원문 [기초]

| 스테이지 | 바이옴 | 형태 |
|---|---|---|
| 1 ~ 5 | 마계숲 | Slay the Spire 같은 **라운드 개념**, 이동의 **선택지** |
| 6 ~ 10 | 초원 | **단일 일직선 노선** |
| 11 ~ 15 | 인간마을 | **미정** |
| 16 ~ 20 | 왕국 성 | 아이작 같은 **라운드(방) 개념**, **보스방까지 탐험** 필요, **보스는 왕의 기사** |

**스테이지 마지막엔 보스** [기초 B-적].

### 4.2 엔진 구조 [살]

- 런은 20개 스테이지의 연속이다. `RunState{ stageNo, biome, path 선택 이력, runCurrency }`.
- 바이옴마다 **진행 방식이 다르다** → `StageFlow` 를 전략(인터페이스)으로 둔다(OCP: 인간마을이 정해지면 구현만 추가).

| 바이옴 | `StageFlow` | 동작 [살] | 상태 |
|---|---|---|---|
| 마계숲 | `RoundMapFlow` | 스테이지 종료 시 **다음 라운드 후보 2~3개**(노드)를 보여주고 플레이어가 고른다(StS 식). **노드 종류 = 휴식 · 상점 · 이벤트 [확정]** (+ 전투 라운드 자체) | ❌ |
| 초원 | `LineRouteFlow` | 하나의 긴 직선 노선. 한 방향으로 진행하며 구간마다 적이 등장(스폰 패턴 §6 의 일방통행이 잘 맞음) | ❌ |
| 인간마을 | `PlaceholderFlow` | **[미정] [확정: 미정 유지]** — 구현 보류. 임시로 단일 라운드를 쓰되 설계를 확정된 것처럼 적지 않는다 | ❌ |
| 왕국 성 | `CastleRoomsFlow` | 방(노드) 그래프. 보스방을 **찾아가야** 한다 [기초]. **방 = 노드이고, 각 방의 전투는 무한 필드 90초 버티기 [확정]** — 방 경계·잠긴 문·\

**마계숲 노드 [확정 이름 / 내용은 살·미정]**

> **스테이지 번호 규칙 [살 — §12.3 C]**: 스테이지 1~5 = **전투 노드**를 5번 치르는 것으로 센다. 휴식·상점·이벤트는 라운드 사이에 끼는 노드로 **스테이지 번호를 소비하지 않는다**(그렇지 않으면 \

| 노드 | 내용 | 런 재화 |
|---|---|---|
| 전투(라운드) | 90초 버티기 스테이지(§4.3) — 기본 진행 단위 | 보상 획득 [미정] |
| **휴식** | 회복류(HP/스태미너 회복 등) [미정: 정확한 효과] | — |
| **상점** | 장신구/무기/스탯 구매 [살] | **런 재화 소비 [확정 근거: 재화 사용처]** |
| **이벤트** | 선택지가 있는 이벤트(득실 랜덤/선택) [미정: 내용] | 변동 |

### 4.3 스테이지 클리어와 보스 — 무한 필드 · 90초 버티기 [확정] + 세부 [살]

- **스테이지의 전투는 "무한 필드에서 90초 버티기" [확정]**: 아레나 경계가 없고(지금 서큘러가 이미 그렇다 ✅), **90초 동안 살아남는 것**이 한 판의 기본 목표다. 지금 코드에는 시간 제한/클리어 개념이 없다(`RunTime()` 만 있음 ❌).
- **보스 [기초: 스테이지 마지막엔 보스] + 컨셉별 보스 2~3개 [확정]**: 바이옴(컨셉)마다 보스가 **2~3종** 있다. 보스 이름·능력은 **[미정]**, 왕국 성의 보스 중 하나는 **왕의 기사** [기초].
- **90초와 보스의 관계 [살 — §12.3 B 확정 대기]**: 기본안 = 스테이지는 **0~90초 버티기(스폰 패턴 타임라인 §6.4)** → 90초에 도달하면 **그 바이옴 보스 풀에서 1체가 등장** → 보스 처치로 스테이지 클리어. (대안: 90초 생존 자체가 클리어이고 보스는 바이옴 마지막 스테이지에서만 등장.)
- 보스는 몹 SoA(`MobField`)에 넣지 않는다: 1체·다양한 패턴이라 **AoS 별도 클래스 `BossController`**(entity-lifecycle-design §3A)가 낫다 [살]. 보스 HP바 UI = [미정].
- 시간 표시: 90초 카운트다운을 HUD 어디에 둘지는 HUD.png 에 없다 → [미정](임시로 상단 중앙 [살]).

### 4.4 데이터 [살]

- `stages.csv`(예정): `stage_no, biome, flow, survive_sec(=90), mob_pool, timeline_id, boss_pool`. 20행. 바이옴 컬럼이 [기초]의 5개씩 묶음을 그대로 반영한다(1~5 forest, 6~10 meadow, 11~15 village, 16~20 castle). **`arena_w/h` 는 없다 — 무한 필드 [확정].**
- `bosses.csv`(예정): `boss_id, name, biome, health, phases, pattern_ids…` — 바이옴당 2~3행 [확정 수량]. `stages.csv` 의 `boss_pool` 이 참조한다.
- `nodes.csv`(예정, 마계숲): `node_type(battle|rest|shop|event), weight, min_stage, max_stage` — 라운드 후보를 뽑는 가중치. 노드 세부(휴식 효과, 이벤트 내용, 상점 진열)는 각각 `rest.csv`/`events.csv`/`shop.csv`(예정, 내용 [미정]).

---

## 5. 적 — B-적

### 5.1 분류 [기초] + 행동 정의 [살] — M3 ✅

| 분류 [기초] | 행동 [살] | 기본 몹 (`mobs.csv`) | 구현 |
|---|---|---|---|
| 근접 | 플레이어를 향해 이동, 접촉 시 피해 | GRUNT — HP 20, 90 px/s, 접촉 8 | ✅ |
| 탱커 | 느리고 단단하고 크다, 세게 부딪힌다. 방패병(§6 포위)이 여기 | BRUTE — HP 140, 55 px/s, 반경 18, 접촉 16 | ✅ (넉백 자체가 없어 "넉백 저항"은 해당 없음) |
| 원거리 | 일정 거리(`keep_distance`)에서 멈춰 투사체 발사 | ARCHER — 360px 에서 멈춤, 520px 안이면 2.4초마다 화살(6, 380 px/s) | ✅ |
| 마법 | 플레이어 자리에 **붉은(보라) 예고 원**을 찍고 잠시 후 그 자리에 피해 — 예고 동안 벗어나면 안 맞는다 | WITCH — 400px 에서 멈춤, 560px 안이면 3.8초마다 HEX(반경 70, 1.1초 뒤 12) | ✅ |
| 보스 | §4.3 별도 클래스 | — | ❌ (M6) |

- 수치는 전부 [살] 초기값 — `mobs.csv`·`mob_attacks.csv` 로 F5 조정. 비율은 `weight`(70 / 10 / 12 / 8).
- 적 공격도 **전투 구조 그대로**다: 같은 `effect`·`path`·`HitShape`·연출, 대상만 플레이어 히트박스(`Team::Enemy`, [circular-combat.md](circular-combat.md) W7). 대쉬 중엔 투사체가 몸을 통과하고 장판은 안 맞는다.

### 5.2 바이옴별 출현 [기초 — 원문 그대로]

| 바이옴 | 출현 분류 |
|---|---|
| 마계숲 | 근접 |
| 초원 | 근접, 탱커 |
| 인간마을 | 근접, 원거리, 탱커, 마법 |
| 왕국 성 | 근접, 원거리, 탱커, 마법 |

(+ 각 스테이지 마지막 보스). 개별 몹의 이름·외형은 [미정] — 분류만 확정.

### 5.3 데이터와 구조 [살] — ✅ (M3)

- **`mobs.csv`** ✅ 한 행 = 한 몹 종류: `id, name, class, health, speed, radius, contact_damage, xp, weight, color, keep_distance, attack, attack_range`(열 뜻은 [circular-balance.md](circular-balance.md) "mobs.csv"). `class` 는 기초 설계 분류 라벨 — 스테이지 출현 풀(M6, §5.2 → `stages.csv` 의 `mob_pool`)과 색이 읽고, **움직임·공격 코드는 안 읽는다.**
- **`mob_attacks.csv`** ✅ = 적 공격 표. **`weapons.csv` 와 열이 같고 로더도 같다**(`LoadAttackTable`) — 레벨 곡선 열만 선택. `mobs.csv` 의 `attack` 이 `id` 로 가리킨다.
- **행동은 열거가 아니라 숫자** (KISS — 계획했던 `MobBehavior` 열거·커널 분기는 안 만듦): `keep_distance` 0 = 추적(근접·탱커), > 0 = 그 거리에서 멈춤(원거리·마법). 탱커는 "느리고 단단하다"가 숫자일 뿐이다. 진짜로 다른 움직임(돌아서 도망, 순간이동 …)이 두 번째로 필요해질 때 열거 + 표를 만든다.
- **`MobField`**(SoA): `type`(uint8, `mobs.csv` 행 번호)·`attackTimer` 열 추가. `Step` 이 종류별 `MobMotion{speed, keepDistance}` 표를 받아 `ParallelFor` 로 움직이고(규칙 6 그대로), `CollectCasts` 가 쿨이 찬 몹 중 사거리 안인 것만 모아 준다(메인 스레드), `StrongestTouching` 이 히트박스에 닿은 몹 중 가장 센 접촉 피해를, `KillTally` 가 종류별 사망 수(→ 종류별 `xp`)를 준다. `MobField` 는 CSV 타입을 모른다 — `Simulation::RebuildMobTables` 가 `mobs.csv` 에서 표를 만들어 넘긴다.
- **예고 장판** = 무기 표의 새 열 `delay`(초)·`origin`(`self`|`target`): 경로 없는 Area 가 `origin=target` 이면 대상(적 → 플레이어, 무기 → 가장 가까운 몹) 자리에, `delay` > 0 이면 그만큼 기다렸다가 떨어진다. 기다리는 동안 맞을 영역이 `VisualStyle::Telegraph` 로 보인다. 계획했던 `ChargeZone` → `TelegraphZone` 일반화는 **안 함** — 돌진 패턴은 M4 에서 지워질 임시라서, 마법 몹은 전투 구조로 해결했다.
- 스폰 종류 선택: 링 스폰 1마리마다 `weight` 비율로 — 스폰 번호의 해시(런 RNG 안 씀: 레벨업·치명타 굴림 순서를 흔들지 않게). 스테이지(M6)가 생기면 `stages.csv` 의 `mob_pool` 이 이 목록을 좁힌다.
- 테스트 씬 더미 = `mobs.csv` 0행 종류(HP 만 99999).

---

## 6. 적 스폰 패턴 — B-스폰

### 6.1 원문 [기초]

1. **일방통행** — 한 방향에서 몰려온다. 디테일한 **숫자, 속도, 모양(화살표, 직사각형 등)은 테이블**로 설정.
2. **플레이어 기준 사각형으로 가둠** — 방어력 높은 방패병.
3. **여러 줄의 적 소환** — 한 줄은 좌, 한 줄은 우, 반복.
4. **추가…** — 패턴은 계속 늘어난다 → 확장 구조가 필수.

### 6.2 확장 구조 [살]

- `SpawnPatternKind` 열거 + 종류별 실행 함수(레지스트리). **새 패턴 = 열거자 하나 + 함수 하나 + CSV 행들.** 스폰 코드에 패턴별 if 사슬 금지.

| kind | 기초 패턴 | 동작 [살] | 테이블 파라미터 |
|---|---|---|---|
| `oneway` | 일방통행 | 한쪽 가장자리(방향)에서 모양대로 배치 후 반대 방향으로 **고정 속도 직진**(추적 안 함) | 방향, 모양(`arrow`/`rect`/`line`…), 개수, 폭/간격, 속도, 몹 종류 |
| `enclose` | 사각 포위 | 플레이어 기준 사각형 둘레에 **탱커(방패병)** 를 배치, 서서히 좁혀 옴 | 사각 반폭, 개수, 수축 속도, 몹 종류(탱커) |
| `lines_alt` | 좌·우 번갈아 줄 | 줄(일방통행 한 줄)을 **좌→우→좌…** 로 반복 | 줄 수, 줄 간격(시간), 시작 쪽, 줄 모양·속도 |
| (추가…) | 추가 | 사용자가 이후 상세 추가 | — |

### 6.3 실행 모델 [살]

- `PatternExecutor` 가 `Simulation` 안에서 고정 스텝으로 돈다. 패턴이 발동하면 `MobField::Spawn` 으로 계산된 위치에 몹을 만들고 **`MobState::March`**(고정 속도 직진 — 지금 `Charge` 와 같은 동작, 이름만 일반화)를 준다. `enclose` 는 위치가 매 스텝 갱신되는 별도 상태.
- 텔레그래프: 패턴 시작 전 붉은 구역/화살표를 `telegraph_sec` 동안 표시 후 스폰. 지금 있는 인프라 = `ChargeZone`/`kChargePattern`(붉은 사각 예고 → 일부 몹 돌진, 임시). (마법 몹의 예고 장판은 M3 에서 전투 구조의 `delay` 열 + `VisualStyle::Telegraph` 로 따로 해결했다 — §5.3. 패턴 예고는 "몹이 나타날 곳"이라 공격 예고와 다르다; M4 에서 필요한 모양으로 정한다.)

### 6.4 타임라인 [살]

- 기본 밀도 스폰(현재 `spawn_curve.csv`: 링 위치에서 추적 몹이 초당 N마리)은 **배경 밀도**로 남기고, 그 위에 패턴을 시간표로 얹는다: `stage_timeline.csv`(예정) `timeline_id, time_sec, pattern_id`.
- 패턴 자체는 `spawn_patterns.csv`(예정)에 파라미터를 둔다(테이블 설정 — [기초] 그대로).
- **범위 주의 [살]**: 지금 `spawn_curve.csv`/`levels.csv` 는 **런 전체에 하나**(런 시계 기준)다. 스테이지가 90초 단위로 생기면(M6) 스폰 곡선은 **스테이지별 시계(0~90초)** 로 바뀌어야 하므로 `stages.csv` 의 `timeline_id` 가 곡선·패턴을 함께 고르도록 재구성한다. 레벨/XP 는 **런 전체에 이어진다**(스테이지가 바뀌어도 초기화하지 않음) [살].

### 6.5 임시 패턴 🟡

- 현재 붉은 구역 예고 → 몹 일부 돌진(`kChargePattern`, `Simulation::StepChargePattern`)은 **기초 설계에 없는 테스트용**이다. 정식 패턴(위 3종 + 추가)이 들어오면 텔레그래프 인프라(현재 코드명 `ChargeZone` → 일반화 시 `TelegraphZone`)만 남기고 이 임시 패턴은 삭제한다. **정식 패턴은 사용자가 이후 상세히 추가한다.**

---

## 7. UI — B-UI

### 7.1 규칙 [기초] → 구현 사양 [살]

| 규칙 [기초] | 구현 사양 [살] | 현재 |
|---|---|---|
| 키보드 사용 시 마우스는 **고정, 보이지 않도록** | `Application` 이 `InputMode{Keyboard,Mouse}` 를 가진다. 이동/대쉬/궁극기 등 **키보드 입력**이 오면 Keyboard → `SetPointerLocked(true)`(커서 숨김+고정) | 🟡 게임 중 항상 고정(자동 전환 없음) |
| 마우스 사용 시 **고정 해제, 보이도록** | 마우스 **이동량이 임계 이상**이거나 **클릭**이 오면 Mouse → `SetPointerLocked(false)`, 커서 표시 | ❌ |
| 마우스가 올라간 Widget 은 **아웃라인 활성화** | `ui::Widget` 기반에 `hovered` 상태와 아웃라인 그리기를 넣는다(상호작용 위젯 전부: Button, 아이콘 버튼, 슬롯…) | ❌ (`Button` 은 호버 색만) |

- 모드 전환은 **게임 입력을 끊지 않는다** — 키보드로 이동 중 마우스를 살짝 움직이면 커서가 나타날 뿐 이동은 계속된다. 모달(레벨업·설정)이 열리면 무조건 Mouse 모드.
- 전환 로직은 `Application`(입력 → 포인터 잠금)에만 둔다. 위젯은 모드를 모른다(DIP).

### 7.2 HUD 레이아웃 [기초 — `docs/images/HUD.png`, 1920×1080 기준]

![HUD](images/HUD.png)

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ ┌────┐ [2-1 런재화][2-2 메타재화]                                     [1 설정] │
│ │ 1  │ [3 ───── 스테미너 ─────]                                        [2-1 스탯]│
│ │초상│ [4 ───── 궁극기 게이지 ─]                          ┌─2-2 스탯 팝업─┐     │
│ └────┘                                                    │(호버 중에만)  │     │
│                                                           └──────────────┘     │
│                          (플레이 영역)                                           │
│                                                                                  │
│            [1 ─────────── 소지 무기 ───────────][2 ──── 소지 장신구 ────]        │
└──────────────────────────────────────────────────────────────────────────────────┘
```

| 영역 | 요소 [기초] | 그림 실측(1920×1080, 근사 px) [살] | 위젯 [살] |
|---|---|---|---|
| 좌상단 | (1) 캐릭터 초상화 | x0–175, y0–170 (정사각에 가까움) | `ImageWidget` |
| | (2) 재화: (2-1) 인게임 런 재화, (2-2) 다회차 강화용 재화 | 2-1: x178–318 / 2-2: x325–492, y0–58 | `CurrencyLabel`(아이콘+숫자) ×2 |
| | (3) 스테미너 (달리기·대쉬) | x178–492, y66–113 | `Gauge`(0..1) |
| | (4) 궁극기 게이지 (일정 수 적 사냥 시) | x178–490, y120–163 | `Gauge` + "사용 가능" 표시 |
| 우상단 | (1) 설정창 열기 | x1843–1915, y3–75 (72×72) | `IconButton` |
| | (2-1) 스테이터스창 열기 버튼 | x1843–1915, y83–155 | `IconButton` |
| | (2-2) 스테이터스 팝업 — **2-1 에 마우스가 올라간 동안만** 보임. 내용 = §2.6 능력치(기초 4스텟 + 주요 파생, 목록은 [살] — §12.3 A) | x1563–1822, y83–588 (259×505) | `HoverPopup` |
| 중앙 하단 | (1) 소지 무기 | x300–958, y982–1076 (658×94) | `SlotStrip`(무기 아이콘+레벨) |
| | (2) 소지 장신구 | x962–1585, y982–1076 (623×94) | `SlotStrip`(장신구 아이콘) |

### 7.3 구현 계획 [살]

- **앵커 배치**: 기존 화면들(`LobbyScreen`, `SettingsScreen`, `LevelUpScreen`)은 1280×720 고정 좌표다. HUD 는 1920×1080 기준 디자인이므로 위젯에 **앵커(TopLeft/TopRight/BottomCenter) + 기준 해상도 대비 스케일**을 도입한다(`ui::UIContext::Build` 가 뷰포트 크기를 이미 받음). 해상도 프리셋이 바뀌어도 HUD 가 화면 모서리에 붙어야 한다.
- 새 위젯(전부 `ui::Widget` 파생, 렌더 백엔드를 모름 — ISP): `ImageWidget`, `Gauge`, `CurrencyLabel`, `IconButton`, `HoverPopup`, `SlotStrip`. 이미지는 `SpriteDraw`(월드/UI 스프라이트 경로, [circular-art-guide.md](circular-art-guide.md))로 그린다. 아트 전에는 단색 `Quad` 로 자리표시.
- 데이터 흐름: `Simulation` 이 `HudState`(값: 스태미너 0..1, 궁극기 0..1, 재화 2종, 무기/장신구 슬롯 목록·레벨)를 노출 → `SnapshotBuilder`/`ui` 가 위젯에 채움. 위젯은 게임 객체를 잡지 않는다.
- 폰트: 현재 5×7 비트맵 폰트(A-Z, 0-9, `: - . %`)뿐 — 한글 불가. 초상화/아이콘/숫자 위주로 설계하고, 한글 텍스트가 필요해지면 글리프 아틀라스가 선행 조건([texture-atlas-and-sprite-pass.md](texture-atlas-and-sprite-pass.md) §1.3).

### 7.4 현재 UI 상태

| 화면 | 상태 |
|---|---|
| 게임 중 HUD | 🟡 **뼈대만.** §7.2 의 6개 영역이 실제 앵커 좌표(1920×1080 기준, 해상도 무관하게 모서리에 붙음)에 자리잡았고 값도 채워져 있지만, 전부 단색 `Quad`(`DrawHudPanel`) — 이미지·아웃라인·호버 표시(2-2 팝업은 지금 항상 보임)·클릭은 아직 없다. XP 바(풀폭 상단 스트립)와 좌하단 밸런싱 줄(MOBS/KILLS 포함, 개발용)은 HUD.png 영역이 아니라서 앵커 없이 그대로 유지 |
| 설정 화면 / 로비 메뉴 | ✅ (설정 진입은 ESC. HUD 우상단 설정 버튼이 생기면 마우스로도 열림) |
| 레벨업 3택 모달 | 🟡 [확정: 유지] — 동작은 ✅, 모양은 세로 텍스트 버튼 3개. **기초 설계 CardSelect 레이아웃**(좌·중·우 카드, 티어 색 — §3.4)과 UI 규칙(마우스 모드 진입, 호버 아웃라인)은 M2 에서 |
| 예전 좌상단 SETTINGS 패널 | 삭제됨 — 기초 설계의 우상단 (1) 설정 버튼이 정식 위치 |

### 7.5 로비 — GAME/TEST SCENE [확정: 2026-09 사용자 결정, 기초 설계 밖 개발용]

기초 설계에 없는 **개발 편의 결정**이다 — 예전엔 로비(`LobbyScreen`, 옛 이름 `SceneSelectScreen`)가 씬 5개
(CIRCULAR/DEFENSE COMBAT/CHARACTER DEMO/SHADOW SHOWCASE/EFFECTS TEST) + SETTINGS + QUIT 버튼을 전부 가졌으나,
사용자가 **GAME · TEST SCENE 두 경로만** 남기라고 확정했다.

- **GAME** = `Application::EnterInGame(DemoScene::Circular)` — 지금까지의 정상 서큘러 런(캐릭터 선택 모달부터).
- **TEST SCENE** = `Application::EnterInGame(DemoScene::Circular, /*circularTest=*/true)` →
  `Simulation::EnterCircularTestScene()` — **체력 99999 더미 몹 1마리만** 있는 무기 테스트 전용 씬. 스폰 예산(`spawn_curve.csv`)도
  돌진 패턴(`kChargePattern`)도 돌지 않고, **플레이어는 피해를 안 받는다**(M3 — 더미가 붙어 있어도 무기 시험만 하도록) (`Simulation::m_circularTestMode`) — 몹은 죽지 않는 이상 하나만 유지되고
  플레이어를 향해 Seek 는 그대로 한다(이동 대상으로도 씀). 더미가 죽으면(오버플로우가 극단적으로 쌓인 빌드 등) 즉시
  다시 스폰. HUD 상단에 주황 "TEST SCENE" 배너가 항상 떠서 실수로 진짜 런으로 착각하지 않게 한다.
  **콜라이더 표시 ✅**: 테스트 씬에서만 모든 콜라이더를 초록 외곽선으로 그린다 — 플레이어 히트박스(48×72 상자), 몹 충돌 원, 날아가는 투사체의 판정 원
  (`SnapshotBuilder` `DrawColliderDebug`). 공격 범위는 원래부터 판정 도형 그대로 그려진다(`AttackVisual`). 일반 게임에서는 안 보인다.
- **SETTINGS/QUIT 버튼은 로비에서 빠졌다** — Settings 는 인게임 ESC 로만 열리고(그 안의 "LOBBY" 버튼이 로비로 돌아가는
  유일한 길), 종료는 창의 OS 닫기(X/Alt+F4)뿐이다. 그래서 `platform::Win32Window::RequestClose()` 는 **지금 호출하는 곳이
  없다**(죽은 코드가 아니라 대기 중인 플랫폼 API — §7.6 의 타이틀 "종료" 버튼이 생기면 그게 첫 호출자다).
- **3D 데모 씬(DEFENSE COMBAT/CHARACTER DEMO/SHADOW SHOWCASE/EFFECTS TEST)은 이제 어떤 메뉴에서도 못 고른다** — 코드는
  그대로 있다(`DemoScene` 열거자, `Simulation::EnterScene`, `SpawnActors` 등 전부 안 지움), `Application::EnterInGame(DemoScene::N)`
  을 코드에서 직접 불러야 진입한다. `docs/demo-scene.md`/`docs/shadows.md`/`docs/scene-flow-design.md` 가 이 결정을 반영한다.
- 코드: `game/LobbyScreen.{h,cpp}`(`BuildLobbyScreen(onGame, onTestScene)`), `Application::EnterLobby()`(옛 `EnterSceneSelect`),
  `Application::EnterInGame(DemoScene, bool circularTest=false)`, `Simulation::EnterCircularTestScene()`/`IsCircularTestMode()`.
- **이 로비는 기초 설계의 타이틀 화면이 아니다** — 정식 씬 구조는 §7.6 이고, 그게 구현되면 로비는 그 밑의 개발 도구로 내려가거나
  사라진다. 로비에 기초 설계에 없는 버튼(강화·난이도 등)을 늘리지 말 것.

### 7.6 씬 구조 — 로딩 · 타이틀 · 인게임 [기초] ❌ 미구현

기초 설계 **"씬 종류"** 원문이 정한 구조다. **지금은 하나도 없다** — 앱이 곧장 인게임으로 부팅하고(§부록 A), 메뉴는 §7.5 의 개발용
로비뿐이다. 구현 시점은 §10 **M8**(메타·강화가 타이틀의 "강화"와 같은 것이라서 묶는다).

| 씬 [기초] | 내용 [기초] | 상태 | 메모 |
|---|---|---|---|
| **로딩** | — | ❌ | 커튼·비동기 에셋은 설계만 있음([loading-and-streaming.md](loading-and-streaming.md)). `GameState::Title` 자리가 지금 `Menu` 다 |
| **타이틀 → 시작** | 난이도 설정으로 간다 | ❌ | 난이도 **6단계 [기초 이름 확정]**: 노말(Normal) · 하드(Hard) · 베리하드(Very Hard) · 하드코어(Hardcore) · 익스트림(Extreme) · 인세인(Insane). **각 단계가 무엇을 바꾸는지는 [미정]** — 기초 설계가 이름만 정했다(§12.2). 구현할 땐 `difficulty.csv` 한 행 = 한 단계(몹 HP/속도/스폰 배율…)로 내서 §6·§4 의 테이블에 곱한다 |
| **타이틀 → 강화** | 다회차 유도, 디테일한 강화 트리는 **차후 추가** [기초 원문] | ❌ | = §10 **M8** 메타 재화(§2.5)의 사용처. 트리 내용은 [미정] |
| **타이틀 → 설정** | — | 🟡 | 화면 자체는 ✅(`SettingsScreen`, [game-settings.md](game-settings.md)) — 지금은 인게임 ESC 로만 열린다 |
| **타이틀 → 종료** | — | ❌ | `platform::Win32Window::RequestClose()` 가 이미 있다(§7.5) — 버튼만 없다 |
| **인게임** | — | ✅ | 이 문서 나머지 전부 |

- **난이도는 [기초]라서 지우거나 이름을 바꾸지 않는다.** 6단계 전부 자리를 만들고, 효과가 [미정]인 동안엔 전부 배율 1.0 인
  `difficulty.csv` 행으로 둔다(테이블 주도 — §5·§6 과 같은 방식, 종류별 if 사슬 금지).
- 난이도는 **런 시작 시 1회 고정**이고 스테이지 흐름(§4)·스폰(§6)·몹(§5) 테이블에 곱해지는 값이다 — 새 시스템이 아니라
  기존 테이블의 배율 계층이다. 그래서 M3~M6 이 테이블로 먼저 서야 난이도가 붙을 자리가 생긴다.

---

## 8. 렌더 · 아트

- **2D baseline 만 사용**, 3D `#include` 금지(규칙 7). `ENGINE_WITH_3D` 없이 빌드·동작해야 한다.
- **카메라 ✅(2026-09-23)**: 항상 월드 **1920×1080** 만큼을 보여 주고 창 크기에 맞춰 균일 확대·축소, 비율이 다르면 레터박스(`CircularConfig.h` `kCircularView` → `SnapshotBuilder` `WorldView`). 해상도가 달라도 보이는 범위·공격 범위·몹 등장 거리가 같다. 월드 단위 규칙은 [engine-conventions.md](engine-conventions.md) 2D.
- 지금: 몹/플레이어/공격 전부 색 `Quad`(`worldQuads`) — **`EffectPass2D` 글로우는 안 쓴다**(사용자 결정: 전부 평면 도형). 공격 = `AttackVisual`(판정에 쓴 `HitShape` 그대로): 원·부채꼴·캡슐 점선 외곽(`kOutlineDrawers`, 점멸) · BOLT·명중 표시 = 날아가는 점 · 투사체 = `hit_radius` 크기 사각형, 색은 `weapons.csv` `color` — [circular-combat.md](circular-combat.md) §2.3. UI 스프라이트 = `SpritePass2D`(아틀라스 1장 하드코딩, 지금 사용처 없음).
- 앞으로: **월드 스프라이트 경로**(다중 아틀라스 + `SpriteDraw` 월드용 배열) → 스프라이트 애니메이션 → 아트. **이미지를 추가하는 방법, 파일 이름 규칙, 애니메이션 설계 방법은 [circular-art-guide.md](circular-art-guide.md).**
- 아트가 없는 동안의 자리표시자 규칙: 분류별 색 — 근접 빨강, 탱커 남색, 원거리 초록, 마법 보라, 보스 금색(크기 큼) [살].

---

## 9. 현재 구현 vs 기초 설계 (격차표)

| 기초 설계 항목 | 상태 | 비고 |
|---|---|---|
| **B-씬 씬 구조**(로딩 / 타이틀[시작·강화·설정·종료] / 인게임) | ❌ (인게임만 ✅) | §7.6. 곧장 인게임 부팅 + 개발용 로비(§7.5)가 임시로 대신 |
| **B-씬 난이도 6단계**(노말~인세인) | ❌ | §7.6. 이름은 [기초], 각 단계의 효과는 [미정] |
| B-장르 뱀서 라이크 (몹 다수, 자동 공격, XP) | ✅ 골격 | 몹 4096, 초당 100마리, 무기 자동 발동 |
| B-규칙 직접 공격 없음, 소지 무기 자동 공격 | ✅ | `PlayerIntent` 에 공격 없음 |
| B-규칙 걷기 | ✅ | `player.csv` `walk_speed` 300 px/s × `move_speed` 스탯 |
| B-규칙 **달리기(Shift) · 대쉬(Space, 무적)** | ✅ | §2.2 [확정]. 대쉬 중엔 몹 접촉·적 공격이 안 맞는다(M3) |
| B-규칙 **스태미너** (HUD 3) — 달리기 ≪ 대쉬, 연타 페널티, 스킬로 조절 | ✅ (바는 임시 HUD) | §2.2 [확정]. `dash_cost_mul`/`dash_chain_penalty_mul`/`dash_cooldown_mul` 스탯이 조절 |
| B-캐릭터 4종 (+ 캐릭터별 **주력 스텟**) | ✅ 데이터·선택·시작 무기 / 🟡 아트 | `characters.csv` 4행, 선택 모달(F7). 플레이어 = 64×128 색 사각(히트박스 48×72, `player.csv`), 템플릿 `assets/templates/char` |
| **능력치 체계**(기초 4스텟 + 행운·공격 크기·추가 투사체·공격속도 등 파생) | ✅ | §2.6 [확정]. 일부 파생은 소비처 대기 |
| 무기·장신구 슬롯 6/6, 최대 레벨 5 | ✅ | [확정]. 둘 다 `maxDeckSlots` 6칸, `max_level` 5. HUD 하단 슬롯 띠 2줄(뼈대) |
| **오버플로우** (최대 레벨 시 소량 능력치) | ✅ | §3.4 [확정] |
| 스테이지 **무한 필드 · 90초 버티기**, 보스 풀 바이옴당 2~3 | ❌ (무한 필드만 ✅) | §4.3 [확정] |
| 마계숲 노드: 휴식·상점·이벤트 | ❌ | §4.2 [확정] |
| B-UI 궁극기 게이지 · 재화 2종 | ❌ | §2.4, §2.5 |
| B-UI **HUD 전체 레이아웃** | 🟡 뼈대 | §7.2 영역이 앵커 좌표에 단색 자리표시로 있음. 이미지·호버·클릭 ❌(M2) |
| B-UI **마우스 고정/표시 자동 전환, 호버 아웃라인** | ❌ | §7.1 (지금은 항상 고정) |
| B-UI **CardSelect 카드 레이아웃**(좌·중·우 3장, 티어 색, 아이콘/세트명/설명/변화량) | ❌ (개수 3 만 ✅) | §3.4. 지금은 세로 텍스트 버튼 3개. 티어 = `luck` 의 확정 소비처 |
| B-무기장신구 무기 | ✅ | 5종 전부(검/채찍/스태프/단검/트럼프 카드, §3.2) + 움직이는 공격 3종(레벨업 풀) — `weapons.csv`, 구조 [circular-combat.md](circular-combat.md) |
| B-무기장신구 **장신구** | ✅ | `accessories.csv` 6종, 스탯 카드는 별개로 유지(§3.3) |
| B-스테이지 20 스테이지 · 4 바이옴 · 4 흐름 | ❌ | 단일 무한 아레나 |
| B-적 분류 근접·탱커·원거리·마법 | ✅ | §5.1 — `mobs.csv` 4종, 접촉 피해, 원거리 화살, 마법 예고 장판(M3) |
| B-적 바이옴별 출현 | ❌ | 지금은 4종이 `weight` 로 한꺼번에 섞임 — 바이옴·스테이지가 생기면(M6) `stages.csv` 의 `mob_pool` |
| B-적 보스 | ❌ | M6 |
| (살) 플레이어 피격·사망 | ✅ | §2.6 — 피격 무적 0.5초, HP 0 → RETRY/LOBBY 모달(§12.3 G 기본안) |
| B-스폰 3종 + 추가 | ❌ | 링 스폰(밀도) ✅ + 임시 돌진 🟡 |
| 레벨업 3택 · XP | ✅ | 기초 설계 밖이지만 **[확정: 유지]** |
| (인프라) CSV 밸런싱 환경 | ✅ | 새 표를 얹을 기반 ([circular-balance.md](circular-balance.md)) |
| (인프라) 해상도 무관 화면 | ✅ | 월드 1920×1080 고정 시야 + 확대·축소·레터박스(§8) |
| (인프라) 테스트 씬 콜라이더 표시 | ✅ | §7.5 |

---

## 10. 개발 순서 (기초 설계 기준)

각 단계는 **기초 설계 항목을 채우는 순서**이고, 끝나면 이 문서의 격차표(§9)와 [roadmap.md](roadmap.md) 를 갱신한다.

| 단계 | 내용 | 완료 기준 |
|---|---|---|
| **M1** ✅ 플레이어·능력치 | 달리기(Shift)·대쉬(Space, 무적, 연속 페널티)·스태미너(`PlayerMotion`, `player.csv`), **능력치 체계**(`StatBlock` + `stats.csv`: 기초 4스텟 + 파생), 캐릭터 데이터(`characters.csv`, 주력 스텟) 4행 | 이동 변화·스태미너 소모/회복·대쉬 연타 시 손해, 능력치가 무기 쿨다운/범위/투사체 수에 반영, 캐릭터 선택 가능 — **헤드리스 검증 + 빌드 경고 0/오류 0** |
| **M2** HUD·CardSelect | 앵커 배치(✅ `ResolveHudRect`) + 위젯 6종의 **이미지**(placeholder Quad → 아트) + `HudState`/실제 `ui::Widget`화, 입력 모드 전환, 호버 아웃라인, **CardSelect 카드 레이아웃**(§3.4 — 좌·중·우 3장, 티어 색, 아이콘/세트명/설명/변화량) | HUD.png 와 같은 배치(✅) + 이미지 + 키보드↔마우스 자동 전환 + 호버 아웃라인 + CardSelect.png 와 같은 레벨업 화면 |
| **M3** ✅ 적 종류 | `mobs.csv` + `mob_attacks.csv`(근접·탱커·원거리·마법), **몹 접촉 피해 + 플레이어 사망**, 적 공격(W7) — §5 | 4분류 몹이 각자 움직이고 공격, 맞으면 HP 감소·대쉬 중 무적, 색 자리표시자 — **헤드리스 검증 + 빌드 경고 0/오류 0** |
| **M4** 스폰 패턴 | `SpawnPatternKind` 3종 + `spawn_patterns.csv` + `stage_timeline.csv`, 텔레그래프 일반화 | 3패턴이 CSV 만으로 조정됨, 임시 돌진 삭제 |
| **M5** ✅ 무기/장신구 | **무기 5종**(검=부채꼴, 채찍=직선, 스태프=폭발형 전기볼트, 단검=관통2 투사체, 트럼프 카드=랜덤 데미지 투사체 — §3.2 [기초]) + `weapons.csv` + 장신구 모델(`accessories.csv`) + 슬롯 2줄(HUD, 6/6) + **오버플로우** + 캐릭터별 시작 무기(궁극기 내용은 [미정] — 게이지·Q 입력만) | 전부 구현·헤드리스 검증 완료. 남은 건 밸런스(단검)뿐 |
| **M6** 스테이지 | `stages.csv`(90초 버티기), `StageFlow`(마계숲 라운드 선택[휴식·상점·이벤트] / 초원 직선 / 왕국 성 방), 보스 풀(`bosses.csv`, 바이옴당 2~3, `BossController`), 런 상태 | 90초 타이머 → 보스 → 다음 스테이지, 1~5→6~10→… 진행, 왕의 기사 |
| **M7** 아트 | 월드 스프라이트 경로 + `SpriteAnimator` + 아틀라스 그룹 ([circular-art-guide.md](circular-art-guide.md)) | 몹/캐릭터가 스프라이트 애니메이션으로 표시 |
| **M8** 메타·씬 구조 | `SaveData`, 메타 재화, 다회차 **강화**, **로딩·타이틀 씬(시작/강화/설정/종료) + 난이도 6단계**(§7.6, `difficulty.csv`) | 판 사이 재화 유지 + 기초 설계의 씬 구조대로 부팅(개발용 로비 §7.5 는 그 밑으로) |

M7 은 M1~M6 과 **병행 가능**(자리표시자가 있어서) — 아트가 준비되는 대로 끼운다. 인간마을 흐름([미정])은 M6 에서 자리표시자만 둔다.

### 10.1 다음 작업 (2026-09-23 기준 — M1·M3·M5·전투 구조 ✅, M2 뼈대 ✅)

끝난 단계의 구현 기록은 각 절(§2 플레이어·능력치·피격, §3 무기·장신구, §5 적, [circular-combat.md](circular-combat.md) 전투 구조)에 있다. 여기는 **앞으로 할 일만** 둔다. 하나 끝낼 때마다 항목을 지우고 §9 격차표와 "현재 위치"를 갱신한다.

**순서**: M4(사용자 패턴 상세 받은 뒤) → M6 → M8. M2 남은 살·M7(아트)·밸런스는 어느 단계와도 병행 가능.

#### ① M4 — 스폰 패턴 (**다음, 사용자 입력 대기**)

`SpawnPatternKind` 3종(일방통행·사각 포위·좌우 줄) + `spawn_patterns.csv` + `stage_timeline.csv`, 임시 돌진 삭제(§6). **패턴 상세(숫자·모양·순서)를 사용자가 주기로 함 — 착수 전 확인.** 준비된 것: 몹 종류(`mobs.csv` — 방패병 = 탱커 행 하나 더), 패턴이 "어떤 몹을" 부를지는 `mobs.csv` id 로.

#### ② M2 남은 살 (아트가 오거나 필요해질 때)

1. **CardSelect 레이아웃**(§3.4): 좌·중·우 카드 + 티어 색. 티어 확률표(`luck` → 티어)는 [미정] → 제안 CSV 와 함께.
2. HUD 이미지(`DrawHudPanel` 단색 → `ImageWidget`/`SpriteDraw`, 아트 필요).
3. `HudState` + 진짜 `ui::Widget`화(`Gauge`·`CurrencyLabel`·`IconButton`·`HoverPopup`·`SlotStrip`) + 설정/상태 버튼 클릭.
4. 입력 모드 전환(§7.1) + 호버 아웃라인 + 상태 팝업 호버 게이팅.
5. 90초 타이머 위치(§12.3 F)는 M6 에서 필요 — 없으면 상단 중앙 [살].

#### ③ M6 · M7 · M8 (뒤)

- **M6 스테이지**: `stages.csv`·90초·`StageFlow`·보스(`BossController`)·마계숲 노드·**바이옴별 몹 풀**(§5.2, `mob_pool` → `mobs.csv` id) — 착수 전 §12.3 **B**(90초와 보스)·**C**(노드 세부) 답 필요.
- **M7 아트**: 월드 스프라이트 경로 + `SpriteAnimator`, 무기·적 공격 W8(`sprite`/`fx_hit` 열은 준비됨), 몹 그림 발밑 정렬.
- **M8 메타·씬 구조**: 세이브·메타 재화·강화, 로딩·타이틀·난이도 6단계(§7.6). 사망 후 흐름(지금 RETRY/LOBBY)에 "메타 재화 정산"이 붙는 자리.

#### ④ 수시로 (작음)

- **밸런스**: 적 수치 전체([살] — 제자리면 10~12초에 사망, §5.1), 단검 재측정·조정(§3.2 — 전투 구조 개편 전 수치), 움직이는 공격 3종 초기값 검토, 오버플로우 상한(§12.3 D).
- **구형 무기** PULSE·BOLT 를 레벨업 풀에서 뺄지(사용자 결정).
- **몹 방어력**: `defense`·`armor_break` 계산식이 정해지면(사용자 "나중에") `mobs.csv` 에 방어력 열 + `HurtPlayer`/`MobField` 피해 식에 반영.
- **기술 부채**: `Simulation` 나머지 분해(스폰·레벨업·3D 디펜스 — [roadmap.md](roadmap.md) 서큘러 트랙), `Card*` → `Weapon*` 이름 변경(§3.5).

---

## 11. 아키텍처 규칙 준수

- 2D baseline 만, 3D 헤더 include 금지 · `ENGINE_WITH_3D` 없이 동작(규칙 7). `Simulation::Step`/`SnapshotBuilder::Build`/`Application::Run` 의 3D 블록에는 `ActiveScene() != DemoScene::Circular` 가드를 유지한다(새 3D 전용 코드를 넣을 때 빠뜨리지 말 것).
- 스레드 경계는 값 `RenderSnapshot` 만(규칙 3). `HudState`·스프라이트 목록도 값.
- 충돌은 탐지만(규칙 8) — 몹 피격·넉백은 게임 로직. 몹은 `CollisionWorld2D` 를 쓰지 않고 `MobField` 질의를 쓴다.
- `ParallelFor` 는 자기 슬롯만(규칙 6). 스폰/킬/패턴 실행은 메인 스레드.
- 새 코드도 SRP/OCP: 종류가 늘어나는 것(몹 행동, 스폰 패턴, 무기 효과, 스테이지 흐름)은 전부 **열거+레지스트리 또는 인터페이스+테이블** — 종류별 if 사슬 금지.

---

## 12. 결정 기록과 남은 질문

### 12.1 사용자 확정 사항 (2026-09-22 — 아래 9개 질문에 대한 사용자 답)

| # | 질문 | 사용자 답 | 반영 위치 |
|---|---|---|---|
| 1 | 레벨업(XP→3택) 유지? | **유지.** UI 는 **기초 설계의 UI 를 참고** | §3.4, §7.4 |
| 2 | 슬롯 수 / 최대 레벨 | **유지(무기 6 · 장신구 6 · 최대 레벨 5).** 최대 레벨이면 **"오버플로우"** 로 능력치를 소량 추가 | §3.1, §3.4 |
| 3 | 대쉬 무적 / 소모 | **대쉬 무적.** 스태미너 소모: **달리기 ≪ 대쉬**, 자주 쓰면 달리는 것보다 나빠야 함, **스킬로 페널티 조절 가능** | §2.2, §2.6 |
| 4 | 인간마을 진행 방식 | **미정** 유지 | §4.2 |
| 5 | 마계숲 노드 종류 / 런 재화 사용처 | 노드 = **휴식 · 상점 · 이벤트** (재화는 상점에서 소비) | §4.2, §2.5 |
| 6 | 나머지 바이옴 보스 | **미정.** 컨셉(바이옴)별 보스 **2~3개씩** | §4.3, §4.4 |
| 7 | 아레나 경계 / 크기 | **무한 필드, 시간 버티기 90초** | §4.3, §4.4 |
| 8 | 궁극기 키 / 종류 | **Q**(궁극기), **Shift**(달리기), **Space**(대쉬). 궁극기 종류는 **미정** | §2.2, §2.4 |
| 9 | 스테이터스 창 능력치 | **기초 4스텟**(체력·지력·오염·민첩, 캐릭터별 **주력 스텟**) + 행운·공격 크기·추가 투사체 갯수·공격속도 등, **더 추가 가능하면 추가** | §2.3, §2.6 |

2026-09-23 전투 구조 결정 D1~D7(이미지 키 = `id` 열 · 그림은 판정 도형 × `visual_scale` · 판정에 몹 반경 포함 · 투사체 스윕 · 움직이는 공격 무기는 레벨업 풀에만 · 럴커 방향 = 가장 가까운 몹 · 궤도는 수명 후 쿨다운마다 재생성) = **전부 권장안 A** — [circular-combat.md](circular-combat.md) §4.

2026-09-23 화면·필드 결정: 보이는 세계 1920×1080 고정 + 창에 맞춰 확대·축소(레터박스) · **무한 필드**(이동 경계 제거) · 몹 등장 거리 `spawn_radius` 640 → **1150**(화면 밖) · 플레이어 그림 64×128, 히트박스 48×72(그림 바닥에서 8px 위) · 몹 그림의 발밑 기준 정렬은 **M7 때** · BLADE 궤도 반경 80 **유지**.

2026-09-24 이미지 요청(아트 범위): 캐릭터 이동 8프레임 · **바이옴당 일반 몹 6종**(이동 4프레임) · **보스 2종**(이동 4 · 공격 8프레임) + 무기(캐릭터 공격) 이미지 — 목록·크기·템플릿은 [circular-art-guide.md](circular-art-guide.md) §6.6. "스테이지별"은 바이옴별로 해석했다(확인 필요). 보스 수는 전에 [확정]한 "바이옴당 2~3"과 충돌하지 않게 **1차 아트 = 2종**으로만 기록한다.

### 12.2 계속 [미정] (사용자가 미정이라고 한 것 — 구현하지 않고 자리만)

인간마을(11~15) 진행 방식 · 마계숲·초원·인간마을 보스의 이름/능력(바이옴당 2~3종이라는 수량만 확정) · 궁극기 종류 · 왕국 이름 `xx` · 휴식/상점/이벤트의 세부 내용 · 런/메타 재화 획득처와 메타 재화 사용처 · **난이도 6단계가 각각 무엇을 바꾸는지**(이름만 [기초] 확정, §7.6) · **다회차 강화 트리**(기초 설계 원문이 "차후 추가"라고 적음) · **CardSelect 카드 티어 확률표**(운 → 티어, §3.4).

### 12.3 남은 질문 (제안 [살]의 확정 요청 — A~G)

- **A. 능력치 세부**: ① 캐릭터↔주력 스텟 배정(제안: 마도기사=체력, 스컬매지션=지력, 서큐버스=오염, 어쎄신리자드=민첩) ② 기초 4스텟이 파생 능력치에 주는 효과(§2.6 표) — 특히 **"오염"의 의미**(제안: 지속피해·상태이상 계열) ③ 상태창(HUD 우상단 팝업)에 보일 능력치 목록.
- **B. 90초와 보스**: 기본안 = **90초 버티기 → 보스 등장 → 보스 처치로 클리어**(대안: 90초 생존이 곧 클리어, 보스는 바이옴 마지막 스테이지에서만). 또한 ① 바이옴당 보스 2~3종인데 스테이지는 5개 → 보스를 **스테이지마다 풀에서 뽑아 재등장**시킬지, 마지막 스테이지에만 둘지 ② 왕국 성의 방 그래프에서 각 방도 90초 버티기인지(기본안) ③ 보스 등장 시 남은 일반 몹 처리.
- **C. 노드 세부**: 전투 라운드 외에 엘리트 같은 노드가 더 필요한지, 휴식(회복량)/상점(진열·가격)/이벤트(종류) 내용, **스테이지 번호 규칙**(§4.2: 전투 노드만 번호를 소비 — 기본안).
- **D. 오버플로우 세부**: 무기별 연관 능력치(`overflow_stat`) 방식으로 구현·장신구에도 적용(§3.4 — 이 둘은 [살]로 구현 완료, 답에 따라 `weapons.csv`만 고치면 됨). **남은 질문 = 중첩 상한**(지금은 무제한 누적).
- **E. 대쉬 수치**: §2.2 제안값(달리기 15/s, 대쉬 30 + 연속 +50%/2s 창, 무적 0.18 s)의 효율 관계(12.0 > 4.5 > 2.25 px/스태미너)가 의도와 맞는지.
- **F. 기타**: 90초 타이머의 HUD 위치, 스테이지 클리어 보상.
- **G. 플레이어 사망 (2026-09-23 추가)** — **기본안 [살]으로 구현함(M3, §2.6)**, 확정은 아님: HP 0 → 시뮬 정지 + 결과 모달(생존 시간·레벨·킬) → **RETRY**(같은 캐릭터 새 런) / **LOBBY**. 부활·목숨 여러 개 같은 규칙은 없음. (M8 에서 "런 실패 → 메타 재화 정산"으로 확장.) 다른 규칙을 원하면 알려 주면 바꾼다.

---

## 부록 A. 현재 구현 메모 (유지)

- **부팅/씬**: 앱이 곧장 Circular 로 부팅(`Application::Run` → `EnterInGame(DemoScene::Circular)`, 타이틀 화면 없음). ESC → 설정 → LOBBY 로 로비에 돌아갈 수 있다(로비는 GAME/TEST SCENE 2버튼뿐, §7.5). `ENGINE_WITH_3D` 무관하게 항상 사용 가능.
- **카메라/축**: 플레이어를 화면 중앙에 고정하고 월드를 역스크롤(`SnapshotBuilder::BuildCircularScene` 의 `toScreen`). `PlayerIntent.move.y` 는 "앞 = +" 라 `Simulation::Step` 에서 한 번만 `-y` 로 뒤집는다(W=위) — 다른 곳에서 다시 뒤집지 말 것.
- **몹**: `game::MobField` — SoA(병렬 벡터 + free-list + dense active list), `ParallelFor` 추적, `DamageInShape`/`Overlapping`/`Damage(MobRef)`/`DamageNearest`/`ClosestWithin`(선형 스캔, 슬롯 세대 번호), 상태 `Seek/Windup/Charge`, 종류 `type`(mobs.csv 행) + `CollectCasts`/`StrongestTouching`/`KillTally`(§5.3). capacity 4096(`kActiveMob.capacity`, CSV 로 못 바꿈).
- **무기**: `weapons.csv`(§3.2) + `Simulation::StepCombat` → `CircularCombat`([circular-combat.md](circular-combat.md)), 시작 무기는 캐릭터마다 다름(`characters.csv` 의 `start_weapon`). 공격 시각 효과는 글로우 없이 평면 도형만 — 판정 도형 그대로(`AttackVisual`), 데미지 계산과 무관.
- **XP/레벨업**: 킬 → `AwardKills` → 임계값 → `RollLevelUpChoices` → `m_levelUpPending`(이 동안 `Simulation::Step` 이 Circular 를 정지) → `Application::ServiceLevelUp` 이 `LevelUpScreen` 오버레이. Esc 로 못 닫음. 버튼 콜백은 선택만 기록하고 다음 프레임에 적용(자기 위젯 파괴 방지). 선택지 RNG = 고정 시드 `std::mt19937`(엔진 최초 `<random>`).
- **임시 패턴**: §6.5. **색 변화 애니메이션 테스트**: 몹(Seek/Windup/Charge)·플레이어 색을 시간·슬롯 해시로 계산([animation-design.md](animation-design.md)).
- **CSV 밸런싱**: `assets/data/circular/{balance,levels,spawn_curve,player,stats,characters,weapons,accessories,mob_attacks,mobs}.csv`, 씬 진입 시 로드 + F5(리로드)/F6(재시작)/F7(캐릭터 선택), `tools/run_balance_sim.bat`([circular-balance.md](circular-balance.md)).
- **테스트 씬**(로비 "TEST SCENE", §7.5): `Simulation::EnterCircularTestScene()` — 체력 99999 더미 몹 1마리만(`kTestDummyHealth`, 종류 = `mobs.csv` 0행), 스폰 예산·돌진 패턴 없음, **플레이어가 피해를 안 받음**(`m_circularTestMode`). 무기 데미지/투사체 거동을 스웜 소음 없이 확인하는 용도. HUD 상단에 주황 배너로 항상 표시. `EnterScene(DemoScene::Circular)`(=GAME)로 나가면 자동으로 꺼진다.
- **확인 방법**: `Debug|x64` 빌드 → 실행(곧장 Circular). W=위, S=아래, A/D=좌/우.

---

## 사용 방법 (How to use)

### 이 문서를 지키는 법

- **작업 전에** 기초 설계의 해당 항목(`B-…`)과 이 문서 태그를 확인한다. [기초]와 어긋나는 구현·문서는 만들지 않는다. 어긋나 보이면 **먼저 사용자에게 묻는다**(§12 형식).
- 기초 설계에 없는 것을 추가할 땐 **[살]로 표시**하고 §12 질문에 올린다. 확정되면 태그를 지우고 격차표(§9)를 갱신한다.
- 기초 설계 파일(`docs/# Circular 기초 설계.md`)은 **수정하지 않는다.** 사용자가 고치면 이 문서를 그에 맞춰 갱신한다.
- 새 기능을 끝내면: §9 격차표, §10 단계, [roadmap.md](roadmap.md) 서큘러 트랙, 필요 시 [circular-balance.md](circular-balance.md)/[circular-art-guide.md](circular-art-guide.md) 를 함께 갱신.

### 종류를 늘리는 법 (데이터 주도 — 구조는 §5·§6·§3·§4)

| 추가할 것 | 지금 가능한 방법 | 목표 방법(계획) |
|---|---|---|
| 무기 | ✅ `weapons.csv` 한 행(`overflow_stat`/`overflow_value` 열 포함). 움직임은 `path` 열 조합으로(궤도·소용돌이·럴커 = BLADE/VORTEX/SPIKE 행 참고). **새 동작일 때만** `CardEffect` 열거자 + `kEffectSpecs` 한 줄을 먼저 추가한다(`effect` 열은 없는 효과를 발명하지 못함) — [circular-combat.md](circular-combat.md) §5. 폴백 `Card.h` `kCardDefs`(`std::array<CardDef,7>`)는 CSV 가 없거나 전 행이 깨졌을 때만 쓰이므로 같이 안 고쳐도 된다 | 〃 (완료 — M5) |
| 장신구 | ✅ `accessories.csv` 한 행(`name,stat,multiplicative,amount,max_level`). 효과 코드 없음(패시브, §3.3) — 어떤 `StatId` 든 바로 쓸 수 있다 | 〃 (완료 — M5) |
| 난이도 | ❌ | `difficulty.csv` 한 행 = 한 단계(§7.6). 6단계 이름은 [기초] 고정, 배율 열만 조정 |
| 능력치(스텟) | `StatId` 열거자 + `kStatDefs` 한 줄 (`game/Stats.h`) | 위 + `stats.csv` 한 행(값만 조정할 땐 CSV 만) — 기초 4스텟 계수·파생 능력치(§2.6). 스탯 카드는 `kStatCards` 한 줄 |
| 마계숲 노드/보스 | ❌ | `nodes.csv`·`bosses.csv` 행(§4.4). 노드 세부(휴식/상점/이벤트)는 [미정] |
| 몹 종류 | ✅ `mobs.csv` 한 행 — 추적/거리 유지는 `keep_distance`, 공격은 `attack` 에 `mob_attacks.csv` id(없으면 그 행부터 추가). 몹이 스폰되는 비율은 `weight` | 〃 (완료 — M3). 추적·멈춤 말고 **다른 움직임**(도망·순간이동 …)이 생길 때만 움직임 열거 + `MobField::Step` 표 한 줄 |
| 적 공격 | ✅ `mob_attacks.csv` 한 행(`weapons.csv` 와 같은 열 — 효과·경로·`delay`·`origin` 그대로, [circular-combat.md](circular-combat.md) §5 "적 공격 추가") | 〃 (완료 — M3) |
| 스폰 패턴 | 임시 `kChargePattern` 튜닝만 | `SpawnPatternKind` 열거자 + 함수 + `spawn_patterns.csv` 행, 시간표는 `stage_timeline.csv` |
| 캐릭터 | `characters.csv` 한 행 ✅ | 그 행(+ 시작 무기 이름) + `char/<id>` 아틀라스(아트) |
| 스테이지 | ❌ | `stages.csv` 한 행(+ 새 진행 방식이면 `StageFlow` 구현) |
| 이미지/애니메이션 | 아틀라스 패킹 절차 ✅ (월드 렌더 연결은 M7) | [circular-art-guide.md](circular-art-guide.md) |

### 하지 말 것

- 기초 설계의 **[기초] 항목을 코드/문서에서 임의로 바꾸지 말 것**(수치·이름은 [살]만 바꾼다).
- `PlayerIntent` 에 공격 입력을 추가하지 말 것(직접 공격 없음).
- 몹 종류·패턴·무기 효과를 `Simulation::Step` 안에 **if 사슬로 늘리지 말 것** — 열거+레지스트리/테이블.
- 덱 합성·미니언 그리드 등 **삭제된 이전 설계를 되살리지 말 것** — 기초 설계에 없다.
- 몹 AI 를 3D `SimAgent` 와 공유하거나 `ParticlePass3D`/`render/r3d/*` 를 2D 씬에서 부르지 말 것(규칙 7).
- 밸런스 숫자를 `Simulation.cpp`/`CircularConfig.h` 에 다시 하드코딩하지 말 것 — CSV 가 튜닝 지점이고 `CircularConfig.h` 는 폴백 기본값([circular-balance.md](circular-balance.md)).
- 임시(🟡) 패턴·HUD 를 정식으로 착각해 확장하지 말 것 — 정식 항목이 대체한다.
