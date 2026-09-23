# 서큘러 전투 구조 — 판정 · 움직임 · 연출

> 상태: **W1~W16 구현 완료(2026-09-23) — W7(적 공격)도 M3 에서 완료, 남은 건 W8(스프라이트, M7).** 무기·적 공격의 **판정 도형**, **움직임(경로)**, **연출(도형 → 이미지)**, **데이터(CSV)** 를 한 구조로 묶는다.
> 결정 D1~D7 은 **전부 권장안 A 로 [확정]**(2026-09-23, §4). 그 밖의 제안은 **[살]** — 기초 설계([# Circular 기초 설계.md](<# Circular 기초 설계.md>))와
> 사용자 [확정] 사항([circular-design.md](circular-design.md) §12.1)이 우선이다. 충돌하면 사용자에게 먼저 묻는다.
>
> 관련: 무기 목록·효과 태그 [circular-design.md](circular-design.md) §3.2, 적 분류 §5, 렌더 §8 ·
> CSV [circular-balance.md](circular-balance.md) · 이미지 이름 규칙 [circular-art-guide.md](circular-art-guide.md) ·
> 엔진 충돌 모듈 [collider-design.md](collider-design.md).

---

## 1. 구현 전 상태 (2026-09-23 조사 — 기록용, 지금 코드는 §2)

### 1.1 공격 종류 × 판정 × 연출

| 무기 | 효과 태그 (`CardEffect`) | 판정 (전부 `MobField` 질의) | 지금 연출 (`SnapshotBuilder`) |
|---|---|---|---|
| PULSE (구형) | `RadialPulse` | 원 `DamageInRadius(player, range)` | 노란 점선 원 (`PulseRing`) |
| BOLT (구형) | `NearestBolt` | 가까운 N체 `DamageNearest` | 빨간 14px 사각형이 명중점으로 이동 (`BoltShot`, 순수 연출) |
| 검 SWORD | `ArcSwing` | 부채꼴 `DamageInArc(player, m_lastMoveDir, 반각, range)` | ⚠ **원**(`PulseRing` 재사용) — 모양 불일치 |
| 채찍 WHIP | `LineSwing` | 캡슐 `DamageInCapsule(player→끝점, 반폭)` | ⚠ 사각형이 선을 따라 이동(`BoltShot` 재사용) — 폭이 안 보임 |
| 스태프 STAFF | `ExplodingBolt` | 투사체 점 접촉 `ClosestWithin(reach)` → 폭발 원 `DamageInRadius(explodeRadius)` | 주황 12px 사각형 → 폭발 시 점선 원 |
| 단검 DAGGER | `PiercingShot` | 투사체 점 접촉 `DamageNearest(reach, 1)` × 관통 수 | 은색 12px 사각형 |
| 트럼프 TRUMP | `RandomDamageShot` | 투사체 점 접촉, 명중 시 피해 롤 | 보라 12px 사각형 |
| 적 공격 | ❌ 없음 (접촉 피해·원거리 투사체·마법 장판 모두 M3) | — | 붉은 예고 구역만 있음 (`ChargeZone`, 돌진 전용 🟡) |

- 흐름: `ExecuteCard` (쿨 만료 시 즉시 판정 또는 `Projectile` 생성) → `StepProjectiles` (매 고정 스텝 이동 + 접촉 판정) →
  `PulseRing`/`BoltShot`/`Projectile` 값 배열 → `SnapshotBuilder::BuildCircularScene` 이 `worldQuads` 로 변환.
- **`CollisionWorld2D` 는 무기 판정에 쓰이지 않는다.** 플레이어 ↔ 장애물만(`Simulation::StepCollision2D`).
  무기 판정은 `MobField` 의 SoA 선형 스캔 질의가 전부다.
- 투사체는 직선(`pos += vel·dt`) 한 가지뿐 — 도는 공격·퍼지는 공격·전진하는 장판을 만들 자리가 없다.
- 이미지: 패킹 도구(`tools/atlas_pack`)와 이름 규칙(`wpn_<id>_icon`, `prj_<id>_NN`, `fx_*`)은 있지만
  `assets/atlas/atlas.groups` 에 `weapon`/`fx` 그룹이 없고, `RenderSnapshot` 에 월드 스프라이트 배열이 없다(`uiSprites` 만) — M7.

### 1.2 기능 문제

| # | 문제 | 위치 | 영향 |
|---|---|---|---|
| P1 | 원·부채꼴·캡슐 판정이 **몹 중심점만** 검사(몹 반경 무시) | `MobField.cpp` `DamageInRadius`/`DamageInArc`/`DamageInCapsule` | 몹 가장자리가 범위에 걸쳐도 안 맞음. 몹 크기가 종류별로 달라지면(M3) 큰 몹이 부당하게 덜 맞음 |
| P2 | 투사체 접촉 거리 = 전역 `mob_radius` + `kProjectileHitReach`(6) | `Simulation::StepProjectiles` | 투사체 자신의 크기 개념이 없고, 몹별 `m_radius` 를 안 씀 |
| P3 | **그리는 크기 ≠ 판정 크기** — 투사체 12px, BOLT 14px, 점선 원 굵기가 `SnapshotBuilder.cpp` 에 하드코딩 | `SnapshotBuilder.cpp` 830~855줄 부근 | 이미지를 붙이면 "보이는 것과 맞는 것"이 어긋남 |
| P4 | 검·채찍 연출이 판정 모양과 다름(원/사각형 재사용) | `ExecuteCard` 의 `ArcSwing`/`LineSwing` case | 플레이어가 공격 범위를 읽을 수 없음 |
| P5 | 이미지 이름 규칙은 `weapons.csv` 의 `id` 를 요구하지만 **그 열이 없다**(식별자는 `name`) | `circular-art-guide.md` 78줄, `weapons.csv` | 무기 행 ↔ 이미지 연결 키가 없음 |
| P6 | 무기 행에 이미지·클립 이름 열이 없다 | `weapons.csv`, `CardDef` | 이미지 연결을 코드(`if kind==…` 색 분기)로 할 위험 |
| P7 | 관통 중복 타격 방지가 "명중 후 24px 전진" 근사(`kPierceClearDistance`) | `Simulation.h` | 큰 몹·빠른 몹에서 같은 몹 재타격 또는 뒤 몹 누락 가능 |
| P8 | 투사체는 스텝마다 **점**으로만 검사(이동 구간 스윕 없음) | `StepProjectiles` | 지금 수치는 안전(단검 900px/s ÷ 60Hz = 15px/스텝 < 판정 지름 32px). 투사체 속도·작은 몹이 생기면 뚫고 지나감 |
| P9 | 적 공격(M3)이 같은 판정 도형을 재사용할 자리가 없다 | — | M3 에서 몹용 판정을 따로 만들면 중복 |

### 1.3 설계 원칙 검사 — 지금 코드 (SOLID · KISS · DRY, `CLAUDE.md` "설계 원칙")

| # | 원칙 | 위반 | 위치 | 이 문서의 해결 |
|---|---|---|---|---|
| S1 | **SRP** | `Simulation` 한 클래스가 서큘러 전투·스폰·돌진 패턴·레벨업·능력치·3D 디펜스를 전부 가짐(`.cpp` 1946줄 + `.h` 873줄). 무기 코드가 늘면 계속 여기로 쌓인다 | `game/Simulation.*` | 전투만 `CircularCombat` 로 분리(§2.0, W14). 나머지 분해는 이 문서 범위 밖 — [roadmap.md](roadmap.md) 후보 |
| S2 | **OCP** | 투사체 색을 `if (shot.kind == …)` 로 고름, 검·채찍이 다른 무기 연출을 빌려 씀 | `SnapshotBuilder.cpp` 845~853줄, `ExecuteCard` | 모양별 그리기 테이블 + `visualKey`(§2.3) |
| S3 | **DRY** | 같은 지식(명중 처리 종류)이 두 열거형에 있음: `CardEffect::{ExplodingBolt,PiercingShot,RandomDamageShot}` ≡ `ProjectileKind::{Exploding,Piercing,Random}`, 그래서 `ExecuteCard` 안에 **switch 안의 switch** | `Card.h`, `Simulation.h`, `ExecuteCard` | 효과 이름 → `AttackForm` 한 표에서 풀기(§2.4) |
| S4 | **DRY** | 원·부채꼴·캡슐 스캔 루프가 함수마다 복제(스캔·피해·사망 처리 동일, 거리식만 다름) | `MobField.cpp` | `DamageInShape` 하나 + 거리식 테이블(§2.1) |
| S5 | **ISP** | 모양이 늘 때마다 `MobField` 공개 함수가 하나씩 늘어남(`Damage*` 4개 + `ClosestWithin`) | `MobField.h` | 모양 무관 질의(`DamageInShape`/`Overlapping`) — 모양이 늘어도 공개 함수는 안 늘어남 |
| S6 | **DRY** | 수명이 다한 항목을 swap-remove 하는 루프가 곳곳에 복제 | `Simulation.cpp`(`PulseRing`·`BoltShot`·`Projectile` 등) | `EraseExpired(vec, dt)` 도우미 하나(W16) |
| S7 | **DRY** | `ApplyLifeSteal` 호출이 공격 종류마다 흩어짐(8곳) | `Simulation.cpp` | 피해 적용 지점을 한 곳(`ApplyHit`)으로 모으면 1곳 |
| S8 | **DRY** | "닿는 거리" 지식이 판정(`mob_radius`+6)과 그림(12px 등)에 따로 있음 | `Simulation.h`, `SnapshotBuilder.cpp` | 그림 크기는 `HitShape` × `visual_scale` 에서만(§2.3) |
| S9 | **KISS** | 관통 중복 방지를 "24px 밀어내기" 근사로 처리 — 규칙이 숫자에 숨어 있음 | `kPierceClearDistance` | 맞은 몹 목록 `hitSlots`(§2.2) |
| S10 | DRY (허용된 예외) | `kCardDefs`(폴백)와 `weapons.csv` 에 같은 숫자 두 벌 | `Card.h` | 의도된 폴백이라 유지. 대신 CSV 열을 바꾸는 작업(W6·W13·W15)은 **폴백도 같이** 고친다 |

LSP 는 전투 코드에 상속이 없어 해당 없음. DIP 는 `Simulation`/`CircularCombat` → 구체 `MobField` 의존인데, 구현이 하나뿐인 같은 모듈 안이라 **인터페이스를 만들지 않는다**(KISS — 두 번째 구현이 생길 때 추상화).

### 1.4 설계 원칙 검사 — 이 문서의 첫 설계안 (2026-09-23 개정 전)

| # | 원칙 | 첫 안의 문제 | 고친 것 |
|---|---|---|---|
| R1 | KISS · DRY | 경로 `Orbit` 과 `Spiral` 이 따로 — 궤도는 "반경 증가 속도 0 인 소용돌이"일 뿐 | 경로 `Polar` 하나(`radial_speed` 0 = 궤도) |
| R2 | KISS | 판정 시점 축(`Continuous`/`Pulsed`)을 따로 둠 — 사실 공격 형태에서 결정됨(투사체 = 닿을 때, 장판 = 틱마다) | 축 삭제. **효과(형태) × 경로** 두 축만(§2.5) |
| R3 | DRY | CSV 열 목록이 §2.4·§2.5·작업표·사용법 네 곳에 복제 | 열 정의는 §2.4 한 곳, 나머지는 링크 |
| R4 | DRY | 경로 매개변수를 투사체마다 복사(`MotionDef` in `Projectile`) — 같은 값이 512벌 | 인스턴스는 `defIndex` + 자기 상태만. 매개변수는 무기 정의에서 읽음 |
| R5 | SRP | `StepProjectiles` 가 이동·판정·연출을 한 함수에서 | 위치(`PathPosition`) · 접촉(`ResolveContact`) · 장판 판정(`HitArea`) · 연출 수명(`AgeAndErase`)을 각자 함수로(§2) |
| R6 | KISS · DIP | (검토) 적/플레이어 대상을 `IHitTarget` 인터페이스로? | 안 함 — 대상은 두 가지(몹 떼, 플레이어 1명)로 고정. `team` 열거 switch 2 case(W7 에서 추가) |
| R7 | SRP | 전투 코드를 계속 `Simulation` 에 추가하는 안 | `CircularCombat` 로 분리(W14) |

---

## 2. 구조 (구현됨 — 2026-09-23)

한 줄 요약: **"공격 = 효과(무엇을, 어떤 도형으로) × 경로(시간에 따라 어디서)". 판정 도형 값(`HitShape`) 하나를 판정과 연출이 같이 쓴다.**
종류가 늘어나는 것(도형·효과·경로·연출)은 열거 + 테이블이다 — 종류별 if 사슬 없음.

```text
weapons.csv 행 ──(CircularBalance 로더)──▶ CardDef{ effect, 크기, 색, path, ... }   ← 숫자의 유일한 출처
      effect ──kEffectSpecs(Card.h)──▶ { form, shape, onHit }                           ← 효과 지식의 유일한 출처
        │
Simulation::StepCombat ── 쿨다운 끝난 무기마다 CircularCombat::Fire                (Team::Player, weapons.csv)
        │                ── MobField::CollectCasts 로 쿨 찬 몹마다 FireEnemy        (Team::Enemy, mob_attacks.csv — W7)
        │   path == None ──▶ CastArea: delay 0 → LandArea 즉시 / delay > 0 → 대기 인스턴스 + AttackVisual(Telegraph)
        │                    LandArea: Player = MobField::DamageInShape / Enemy = ShapeOverlapsRect(플레이어 히트박스) + Outline
        │                    Nearest   → DamageNearest + AttackVisual(Travel)                       (BOLT, 무기만)
        │   path != None ──▶ SpawnMoving: AttackInstance × count (Polar 만 여러 개)
        ▼
CircularCombat::Step (고정 스텝마다)
   인스턴스마다: t += dt → pos = kPaths[path](def, inst, anchor)            ← t 로 계산, 누적 없음
                 Projectile → Player: ResolveContact(캡슐 Overlapping → 기억 제외 → 진행 순 정렬 → onHit)
                              Enemy: ResolveEnemyContact(캡슐 vs 히트박스, 대쉬 중이면 통과)
                 Area       → 경로 없음 = delay 가 지나면 LandArea 한 번 / 경로 있음 = tick_interval 마다 LandArea
                 수명(lifetime) 또는 이동 거리(range) 끝 → 제거
   AgeAndErase(visuals)
        ▼
CombatResult{ kills, heal, playerDamage } ──▶ Simulation: XP(종류별 KillTally)·레벨업, HP(흡혈, HurtPlayer)
SnapshotBuilder: AttackVisual → kOutlineDrawers[shape] / Travel 점, 인스턴스(Projectile) → 사각형 — 크기 = 판정 × visual_scale
```

### 2.0 소유 — `CircularCombat` (SRP, `game/CircularCombat.*`)

- 무기 발사(`Fire`), `AttackInstance`·`AttackVisual` 풀, `Step` 을 소유. `Simulation` 은 멤버 `m_combat` 하나를 갖고
  `StepCombat` 에서 쿨다운을 돌려 `Fire`/`Step` 을 부르고, 돌려받은 `CombatResult{ kills, heal }` 로 XP·HP 를 바꾼다(전투가 진행도·HP 를 직접 안 만짐).
- 입력은 값/const 참조: `CombatContext{ balance, stats, playerCenter, facing }` + `MobField&` + `std::mt19937&`(런 RNG — 크리·랜덤 피해, 결정론 유지).
- `SnapshotBuilder` 는 `Simulation::Combat()` 의 `Visuals()`/`Instances()` 를 읽는다.
- 메인(시뮬) 스레드 전용. 인스턴스가 많아져 병렬화할 때는 위치 계산만 `ParallelFor`(인스턴스마다 자기 칸, 규칙 6), 판정은 순차.
- **적 공격(W7, M3 ✅)**: `Team{Player, Enemy}` 가 `AttackInstance`·`AttackVisual` 에 있다. 행 표는 `AttackTable(balance, team)`(무기 = `weapons.csv`, 적 = `mob_attacks.csv`) 한 곳에서 고른다. `Simulation` 이 `MobField::CollectCasts` 로 쿨이 찬 몹을 모아 `FireEnemy(attackIndex, 몹 위치, …)` 를 부른다. `CombatContext` 에 `playerBox`(히트박스)·`playerHittable`(대쉬 중 false), `CombatResult` 에 `playerDamage`(그 호출에서 가장 큰 한 방 — 피격 무적은 `Simulation::HurtPlayer` 가 판단).

### 2.1 판정 도형 `HitShape` (`game/HitShape.h`)

- `HitShapeKind{ Circle, Arc, Capsule }` + `HitShape{ kind, center, end, dir, radius, halfAngle }`, 생성 함수 `Circle/Arc/Capsule`.
- 점-도형 검사는 **표 하나**(`kShapeTests`, 종류마다 함수 1개, `static_assert` 로 개수 확인) → `ShapeContains(shape, p, pad)`.
- `MobField` 공개 판정 질의: `DamageInShape`(장판·스윙·폭발), `Overlapping`(읽기 전용 — 투사체가 고르고 기억할 때),
  `Damage(MobRef)`(한 마리), `DamageNearest`(BOLT), `ClosestWithin`(조준). 옛 `DamageInRadius/Arc/Capsule` 삭제.
- **몹 반경 포함 [확정 D3]**: `pad` = 몹의 `m_radius`. 부채꼴 각도는 몹 중심 기준(단순함 우선).
- `MobRef{ slot, generation }`: 슬롯마다 세대 번호가 있고 몹이 죽으면 올라간다 — 오래 들고 있는 참조가 새로 스폰된 몹을 가리키지 않는다.
- **플레이어 쪽 판정**(W7): `ShapeOverlapsRect(shape, rect)` — 같은 `HitShape` 를 플레이어 **히트박스 사각형**(`player.csv` `hitbox_*`, 48×72)과 검사한다. 종류마다 함수 1개인 표(`kRectTests`, `static_assert`): 원 = 사각형까지 최근접 거리, 캡슐 = 선분-사각형 거리(교차 슬랩 테스트 + 끝점/모서리), 부채꼴 = 근사(사각형에서 중심에 가장 가까운 점이 부채꼴 안). 처음 계획한 "플레이어 = 원 하나"보다 정확하고, 이동·충돌이 쓰는 그 콜라이더 그대로다. 몹 접촉도 같은 식(`PointRectDistanceSq` ≤ 몹 반경²).
- `CollisionWorld2D` 로 옮기지 않는다: 몹 4096 SoA 를 매 스텝 콜라이더로 재구성하는 건 낭비이고, 질의형이 "탐지만" 규칙(아키텍처 규칙 8)과도 맞다.
  몹 수가 스캔 비용을 넘기면 `MobField` 안에 균일 그리드를 둔다.

### 2.2 움직이는 공격 `AttackInstance`

- 투사체와 움직이는 장판이 같은 타입: `{ defIndex, piercesLeft, t, damage, damageMax, scale, range, theta0, tickLeft, origin, dir, pos, prevPos, memory[32] }`.
  **경로·크기 매개변수는 들고 다니지 않는다** — `defIndex` 로 무기 행에서 읽는다(발사 순간 확정값 `damage`·`damageMax`·`scale`(attack_size)·`range` 만 예외).
- 접촉 = **지난 위치 → 새 위치 캡슐**(반폭 `hit_radius × scale`) + 몹별 반경 [확정 D4]. 빨라도 뚫지 않는다.
- **기억(memory)**: 맞힌 몹의 `{MobRef, 시각}` 32칸 링. 관통은 기억된 몹을 다시 안 때림, 수명형(궤도·소용돌이)은 `rehit_interval` 이 지나면 다시 때림.
  (32칸이 `rehit_interval` 안에 다 차면 가장 오래된 몹이 일찍 다시 맞을 수 있다 — 허용.)
- 명중 처리(`onHit`): `Pierce` — 진행 순으로 때리고, 사거리형은 관통 수만큼 뒤 소멸 / `Explode` — 처음 닿은 몹 자리에서 `explode_radius` 원 장판 /
  `Random` — `[damage, damage_max]` 롤(닿을 때만 RNG 사용).

### 2.3 연출 `AttackVisual`

- `AttackVisual{ HitShape shape; VisualStyle style; defIndex; ageLeft; life; team }`, `VisualStyle{ Outline, Travel, Telegraph }`.
  Outline = 판정에 쓴 도형의 외곽(점선), Travel = 캡슐 시작→끝을 날아가는 점(BOLT, 투사체 명중 표시), **Telegraph** = 늦게 떨어지는 장판(`delay`)이 곧 맞힐 영역 — 떨어질 때까지 점점 진해지고 빨리 깜빡인다(M3). **보이는 크기 = 맞는 크기** [확정 D2].
- `SnapshotBuilder` 의 `kOutlineDrawers[HitShapeKind]`(원 = 점선 원, 부채꼴 = 부채꼴 테두리, 캡슐 = 두 변 + 반원) — 무기별 분기 없음.
  색 = 무기 행 `color`, 배율 = `visual_scale`. 스프라이트(`sprite`/`fx_hit`)는 M7 에서 이 자리에 들어간다(W8).

### 2.4 데이터

`weapons.csv` 의 열 정의는 [circular-balance.md](circular-balance.md) "weapons.csv — 무기" **한 곳**이다(문서 DRY). 효과 이름 → 동작 표는 `game/Card.h` `kEffectSpecs`:

| `effect` | 형태 | 도형 | 명중 처리 | 무기 (적 공격) |
|---|---|---|---|---|
| `radialpulse` | Area | Circle | — | PULSE, SPIKE(직선 경로) (HEX — `origin=target`, `delay`) |
| `arcswing` | Area | Arc | — | 검 |
| `lineswing` | Area | Capsule | — | 채찍 |
| `nearestbolt` | Nearest | — | — | BOLT |
| `explodingbolt` | Projectile | Circle(폭발) | Explode | 스태프 |
| `piercingshot` | Projectile | — | Pierce | 단검, BLADE·VORTEX(극좌표 경로) (ARROW) |
| `randomdamageshot` | Projectile | — | Random | 트럼프 |

`CardEffect` 는 이 표의 키로만 남는다 — 실행 코드는 `form`(3)·`onHit`(3)·`path`(3)로만 분기한다.
적 공격은 `mob_attacks.csv`(M3 ✅)가 **`weapons.csv` 와 같은 열·같은 로더**로 같은 `effect`·`path` 체계를 쓴다(W7) — `mobs.csv` 의 `attack` 이 그 `id` 를 가리킨다([circular-design.md](circular-design.md) §5.3).

**배치 열 (M3에서 추가, 두 표 공통)**: 경로 없는 Area 에만 — `origin`(`self` = 시전자 주위 / `target` = 대상 자리: 적이면 플레이어, 무기면 `kTargetSearch` 540px 안의 가장 가까운 몹, 없으면 발동 안 함), `delay`(초, > 0 이면 그 자리에 Telegraph 를 띄우고 기다렸다가 떨어짐). 다른 조합(투사체에 `delay` 등)은 로더가 거부한다.

### 2.5 공격 움직임 — 효과(형태) × 경로

| 축 | 값 (새 종류 = 열거자 + 테이블 한 항목) |
|---|---|
| **효과 형태 `AttackForm`** | `Area`(도형 안 전부) · `Nearest`(가까운 N체) · `Projectile`(닿으면 `onHit`) |
| **경로 `AttackPath`** | `None`(발사 지점, 즉시) · `Straight`(`origin + dir·speed·t`) · `Polar`(`anchor + 극좌표((start_radius + radial_speed·t)·scale, θ0 + angular_speed·t)`) |

- **판정 시점은 형태가 정한다**(R2): `Projectile` = 매 스텝 스윕 접촉, `Area` + 경로 = `tick_interval` 마다 그 자리에서 판정.
- **위치는 `t` 로 계산**(`CircularCombat.cpp` `kPaths`) — 같은 입력 = 같은 궤적.
- 조준 = 사거리 안 가장 가까운 몹, 없으면 마지막 이동 방향 [확정 D6]. `Polar` 는 그 각도부터 `count` 개를 360°/count 간격으로.
- 움직이는 `Area` 의 부채꼴·캡슐 방향 = 진행 방향. 수명(`lifetime`)이 끝나면 사라지고 쿨다운마다 다시 생긴다 [확정 D7].

**요청된 세 패턴 = `weapons.csv` 행** (값은 [살] 초기값, **레벨업 풀 전용** — 기초 설계 5종은 그대로 [확정 D5])

| 패턴 | 행 | `effect` | `path` | 주요 값 |
|---|---|---|---|---|
| 플레이어 주위를 도는 공격 | BLADE | `piercingshot` | `polar`, `anchor=player` | 반경 80, 180°/s, 3개, 수명 4s(쿨 4.5s), 재타격 0.5s |
| 소용돌이치며 바깥으로 퍼지는 공격 | VORTEX | `piercingshot` | `polar`, `anchor=cast` | 반경 20 → +90px/s, 240°/s, 4개, 수명 3s, 재타격 0.5s |
| 럴커처럼 한 칸씩 전진하는 공격 | SPIKE | `radialpulse` | `straight` | 500px/s, 틱 0.08s(≈ 40px 간격), 반경 24, 사거리 360 |

---

## 3. 작업 목록과 결과

검증 = Linux 헤드리스 하네스(`Simulation` 을 창 없이, 무기 1종만 든 캐릭터로 20초 × 정지/이동) + 테스트 씬 더미(99999 HP) 탐침.
MSVC 빌드는 이 환경에 없어 g++ `-Wall -Wextra` 로 2D·3D 두 구성의 `src/game/*.cpp` 를 컴파일 확인했다(`Application.cpp` 는 Win32 헤더라 제외).

| # | 작업 | 상태 | 결과 |
|---|---|---|---|
| W14 | `CircularCombat` 분리 | ✅ | 킬 수 전후 **동일** |
| W1 | `HitShape` + `DamageInShape` + 몹 반경(D3) | ✅ | 반경 끄고 동일 확인 후 켬 — 20초 정지 킬: PULSE 1406→1426, 검 1042→1050, 채찍 1295→1347, 스태프 1182→1386, 투사체 무기 변화 없음 |
| W15 | `kEffectSpecs` → `{form, shape, onHit}`, `ProjectileKind` 삭제 | ✅ | 이중 switch 없음, 킬 수 동일 |
| W2 | 명중 한 경로(`Hit`/`HitArea`), 흡혈 1곳 | ✅ | 〃 |
| W3 | `AttackVisual` + 모양별 그리기 표 | ✅ | 검 = 부채꼴, 채찍 = 캡슐 외곽. `PulseRing`/`BoltShot` 삭제 |
| W16 | `AgeAndErase` 도우미 | ✅ | 복제 루프 0 |
| W4 | 스윕 접촉·`hit_radius`·몹별 반경 | ✅ | 단검 900 / 3000 / 20000 px/s 모두 10초 18회 명중(안 뚫음). 스태프 정지 킬 1386→1180(폭발 중심이 "처음 닿은 몹"으로 바뀜) |
| W5 | `MobRef` 세대 + 기억(관통 중복 방지) | ✅ | `kPierceClearDistance` 삭제. 한 발이 같은 더미를 1회만 때림 |
| W10 | 경로 표 `None/Straight/Polar`, anchor·count·lifetime | ✅ | 기존 투사체 킬 수 동일. 경로 표는 별도 파일이 아니라 `CircularCombat.cpp` 안(KISS) |
| W11 | `rehit_interval` | ✅ | 제자리 칼날: 재타격 0.5s → 24.4회, 0.25s → 47.7회(비례) |
| W12 | 움직이는 `Area`(럴커) | ✅ | SPIKE 틱 0.08s → 15.9회, 0.04s → 23.9회(10초) |
| W6+W13 | `weapons.csv` 열(`id`·색·크기·경로…) + 로더 검증 + BLADE/VORTEX/SPIKE | ✅ | 불가능한 조합은 줄 번호와 함께 행 거부. `characters.csv` 는 무기 `id` 로 참조 |
| W7 | 적 공격(`team=Enemy`, 플레이어 피격 + 대쉬 무적) | ✅ (M3) | 화살(투사체) 300px 거리에서 0.72초 뒤 6 명중, 대쉬 중엔 통과·사거리로 소멸 · HEX(예고 장판) 1.1초 뒤 12, 대쉬 중·예고 밖으로 비키면 0 · 적 장판은 몹을 안 다침 — 헤드리스 확인 |
| W8 | 이미지(`sprite`/`fx_hit` → `SpriteDraw`) | ⏸ M7 월드 스프라이트 경로 때 | 열·이름 규칙은 준비됨 |
| W9 | 문서 | ✅ | 이 문서, `circular-balance.md` 열 표, `circular-design.md`, `command-playbook.md` |

설계와 달라진 점(전부 KISS/DRY 쪽으로): 경로 표를 `game/AttackPath.*` 새 파일 대신 `CircularCombat.cpp` 안에 둠 ·
효과 이름 표는 `CardEffect` 열거 + `kEffectSpecs` 한 표(열거 순서를 `static_assert` 로 검사) · 무기 색 `color` 열 추가(무기별 색 분기 제거용) ·
`team`/대쉬 무적 입력은 W7 까지 보류(→ M3 에서 추가). W7 에서 바뀐 점: 플레이어 판정을 원이 아니라 히트박스 사각형으로(`ShapeOverlapsRect`), 적 공격 표를 `mobs.csv` 열이 아니라 따로 `mob_attacks.csv`(무기와 같은 열)로 — 공격 하나를 여러 몹이 같이 쓸 수 있고 로더를 두 번 만들지 않는다(DRY).

---

## 4. 결정 기록 — 전부 A [확정 2026-09-23]

| # | 질문 | 확정 (A) | 버린 안 (B) |
|---|---|---|---|
| D1 | 무기 ↔ 이미지 연결 키 | `weapons.csv` 에 `id` 열 추가, 아트 가이드 그대로 | `name` 소문자를 id 로 간주 |
| D2 | 판정 크기와 그림 크기 | 판정 도형이 기준, 그림은 `visual_scale` 배율(기본 1) | 그림 크기 따로 지정 |
| D3 | 장판·스윙에 몹 반경 포함 | 포함 — 킬 수가 늘어 밸런스 재조정 필요(W1 에서 기록) | 중심점 유지 |
| D4 | 투사체 스윕 판정 | W4 에서 바로 | 필요할 때까지 보류 |
| D5 | 움직이는 공격 무기의 위치 | 새 무기 행, **레벨업 풀에만**(시작 무기 아님) — 기초 설계 5종은 그대로 | 기존 5종의 레벨/진화 효과로만 |
| D6 | 럴커 공격 방향 | 가장 가까운 몹(`ClosestWithin`) | 마지막 이동 방향 |
| D7 | 궤도 공격 수명 | `lifetime` 후 사라지고 쿨다운마다 다시 생김 | 계속 유지 |

---

## 5. 사용 방법 (How to use)

### 새 무기 추가 — CSV 한 행

`weapons.csv` 한 행(열 뜻은 [circular-balance.md](circular-balance.md) "weapons.csv — 무기"). 기존 `effect` × `path` 조합이면 코드 수정 없음,
재빌드 없음 — 게임에서 **F5**(다시 읽기)/**F6**(다시 읽고 재시작). 불가능한 조합은 로더가 줄 번호와 함께 알려 준다.

```csv
id,name,effect,cooldown,damage,range,...,color,hit_radius,visual_scale,sprite,fx_hit,path,anchor,count,lifetime,start_radius,radial_speed,angular_speed_deg,tick_interval,rehit_interval
bomb,BOMB,radialpulse,3,20,200,...,FF4040,30,,,,polar,player,2,4,90,0,120,0.5,
```

(위 예 = 플레이어 주위를 돌며 0.5초마다 터지는 폭탄 2개.) 이미지는 `assets/src/weapon/prj_bomb_00.png` … 로 넣고
`build\tools\atlas_pack.exe --group weapon`(이름 규칙 [circular-art-guide.md](circular-art-guide.md)) — 화면 표시는 M7, 그 전엔 판정 도형이 그대로 그려진다.
시작 무기로 쓸 게 아니면 폴백 `kCardDefs` 는 안 고친다.

### 새 효과 추가 (예: 도넛 장판)

1. 새 도형이 필요하면 `HitShape.h`: `HitShapeKind` 에 `Ring`(+ `Count` 앞) + 필드 + `kShapeTests` 에 검사 함수 1개(몹 반경 `pad` 규칙 지킴).
2. `SnapshotBuilder.cpp` `kOutlineDrawers` 에 그리기 함수 1개. (`static_assert` 가 빠뜨림을 잡는다.)
3. `Card.h`: `CardEffect` 에 `RingBurst`(+ `Count` 앞) + `kEffectSpecs` 에 한 줄. 모양 매개변수가 새로 필요하면 `CircularCombat.cpp` `AreaShape` 에 case 1개.

```cpp
// Card.h — 효과 지식은 이 표 한 곳. 실행 코드는 form/onHit 로만 분기한다.
inline constexpr EffectSpec kEffectSpecs[] = {
    { CardEffect::RadialPulse, "radialpulse", AttackForm::Area, HitShapeKind::Circle, OnHit::None },
    // ...
    { CardEffect::RingBurst,   "ringburst",   AttackForm::Area, HitShapeKind::Ring,   OnHit::None },
};
```

### 새 경로 추가 (예: 부메랑)

1. `Card.h` `AttackPath` 에 `Boomerang`(+ `Count` 앞).
2. `CircularCombat.cpp` `kPaths` 에 위치 함수 1개 — `t` 만으로 위치를 돌려주는 순수 함수.
3. `CircularBalance.cpp` 의 `path` 파서에 이름 한 줄(+ 필요하면 조합 검증 한 줄). 판정·연출·명중 처리는 그대로 따라온다.

```cpp
// CircularCombat.cpp — 종류별 if 대신 표 한 줄
math::Vec2 PathBoomerang(const CardDef& def, const AttackInstance& attack, math::Vec2)
{
    const float half = def.lifetime * 0.5f;
    const float d = def.projectileSpeed * (attack.t < half ? attack.t : def.lifetime - attack.t);   // 갔다가 돌아옴
    return attack.origin + attack.dir * d;
}
constexpr PathFn kPaths[] = { &PathNone, &PathStraight, &PathPolar, &PathBoomerang };
```

### 적 공격 추가 (W7 ✅) — CSV 두 줄

1. `mob_attacks.csv` 에 한 행(열은 `weapons.csv` 와 같음, 레벨 곡선 열은 비워도 됨). `nearestbolt` 는 적 공격이 될 수 없다.
2. `mobs.csv` 의 그 몹 행에 `attack`(= 1의 `id`)과 `attack_range`(이 거리 안일 때만 쏨).

```csv
# mob_attacks.csv - 플레이어 자리에 1.1초 뒤 떨어지는 반경 70 장판 (WITCH 의 HEX)
id,name,effect,cooldown,damage,range,projectile_speed,hit_radius,color,origin,delay
hex,HEX,radialpulse,3.8,12,70,,,B060FF,target,1.1
# 부채꼴로 세 발 도는 칼날을 쏘는 적이 필요하면: effect=piercingshot, path=polar, count=3, lifetime ... (무기와 똑같이)
```

코드는 안 고친다 — `CircularCombat` 이 `Team::Enemy` 면 대상을 플레이어 히트박스로 바꾸는 것 말고는 무기와 같은 경로를 탄다.
대상 선택은 `team` 두 갈래(몹 떼 = `MobField` 질의 / 플레이어 = `ShapeOverlapsRect` + `playerHittable`) — 몹 종류별로 따로 검사하지 않는다.

### 하지 말 것

- 판정과 그림에 **다른 숫자**를 쓰지 말 것 — 그림 크기는 `HitShape` × `visual_scale` 에서만.
- `SnapshotBuilder` 에 `if (kind == …) color = …` 식 무기별 분기를 만들지 말 것 — 무기 행(`color`, `sprite`)으로.
- 움직임마다 새 `effect` 를 만들지 말 것 — 움직임은 `path` 열, 효과는 "무엇을 어떤 도형으로"만.
- 경로 함수에서 위치를 `pos += …` 로 누적하지 말 것 — `t` 로 계산(결정론·헤드리스 재현).
- 인스턴스에 무기 매개변수를 복사하지 말 것 — `defIndex` 로 읽는다(발사 순간 확정값만 예외).
- 전투 코드를 `Simulation` 에 다시 추가하지 말 것 — `CircularCombat` 에.
- 무기 판정을 `CollisionWorld2D` 에 몹 4096개 콜라이더로 넣지 말 것 — `MobField` 질의가 계약이다(§2.1).
- 판정 함수 안에서 몹을 밀거나 움직이지 말 것 — 넉백이 필요하면 판정 결과를 받아 별도 단계에서.
- 대상이 둘뿐인데 `IHitTarget` 같은 인터페이스를 만들지 말 것 — 세 번째 대상이 생기면 그때(KISS).
- 무기 숫자를 `Card.h`/`CircularCombat.cpp` 에 하드코딩하지 말 것 — `kCardDefs` 는 폴백일 뿐([circular-balance.md](circular-balance.md)).
