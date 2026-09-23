# 서큘러 전투 판정 · 연출 구조 — 설계 작업 정리

> 상태: **설계 확정, 구현 전.** 무기·적 공격의 **판정 도형**, **움직임(경로)**, **연출(도형 → 이미지)**, **데이터(CSV)** 를 한 구조로 묶는다.
> 결정 D1~D7 은 **전부 권장안 A 로 [확정]**(2026-09-23, §4). 그 밖의 제안은 **[살]** — 기초 설계([# Circular 기초 설계.md](<# Circular 기초 설계.md>))와
> 사용자 [확정]([circular-design.md](circular-design.md) §12.1) 이 우선이다. 충돌하면 사용자에게 먼저 묻는다.
>
> 관련: 무기 목록·효과 태그 [circular-design.md](circular-design.md) §3.2, 적 분류 §5, 렌더 §8 ·
> CSV [circular-balance.md](circular-balance.md) · 이미지 이름 규칙 [circular-art-guide.md](circular-art-guide.md) ·
> 엔진 충돌 모듈 [collider-design.md](collider-design.md).

---

## 1. 현재 상태 (코드 확인 결과, 2026-09-23)

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
| S5 | **ISP** | 모양이 늘 때마다 `MobField` 공개 함수가 하나씩 늘어남(`Damage*` 4개 + `ClosestWithin`) | `MobField.h` | 질의 2개(`DamageInShape`, `ClosestWithin`)로 고정 |
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
| R5 | SRP | `StepProjectiles` 가 이동·판정·연출을 한 함수에서 | `AdvancePaths` → `ResolveHits` → `EmitVisuals` 세 단계(§2.0) |
| R6 | KISS · DIP | (검토) 적/플레이어 대상을 `IHitTarget` 인터페이스로? | 안 함 — 대상은 두 가지(몹 떼, 플레이어 1명)로 고정. `team` 열거 switch 2 case |
| R7 | SRP | 전투 코드를 계속 `Simulation` 에 추가하는 안 | `CircularCombat` 로 분리(W14) |

---

## 2. 목표 구조

한 줄 요약: **"공격 = 효과(무엇을, 어떤 도형으로) × 경로(시간에 따라 어디서)". 판정 도형 값(`HitShape`) 하나를 판정과 연출이 같이 쓴다.**
종류가 늘어나는 것(도형·효과·경로·연출)은 열거 + 테이블로 둔다 — 종류별 if 사슬 금지.

```text
weapons.csv / mobs.csv 행 ──(로더, 효과 이름 표)──▶ CardDef{ form, shape, onHit, path, ... }  ← 숫자의 유일한 출처
        │
CircularCombat::Fire ── path == None ──▶ HitShape 즉시 판정 (지금의 PULSE·검·채찍)
        │               path != None ──▶ AttackInstance{ defIndex, team, t, θ0, dir, origin, hitSlots }
        ▼
CircularCombat::Step (고정 스텝마다, 순서 고정)
   1. AdvancePaths : pos = kPathTable[path](def, inst, anchorPos)        ← t 로 계산, 누적 없음
   2. ResolveHits  : form 별 — Projectile = 지난 위치→새 위치 캡슐 접촉 → onHit
                                Area       = tick 마다 그 자리 HitShape 판정
                     대상 = team 별 — Player 팀 → MobField::DamageInShape / Enemy 팀 → 플레이어 원(대쉬 무적 확인)
   3. EmitVisuals  : AttackVisual{ HitShape, visualKey, age } 기록
        ▼
SnapshotBuilder: 모양별 그리기 테이블 → worldQuads (아트 없음) / SpriteDraw (M7) — 크기 = HitShape × visual_scale
```

### 2.0 소유 — `CircularCombat` (SRP)

- 새 클래스 `game::CircularCombat`(`game/CircularCombat.*`): 무기 발사(`Fire`), `AttackInstance`·`AttackVisual` 풀, 위 세 단계 `Step` 을 소유.
  `Simulation` 은 멤버로 하나 갖고 `Step` 을 부르기만 한다(`m_deck` 쿨다운 관리는 그대로 `Simulation`).
- 입력은 값으로 받는다: `CombatContext{ playerCenter, lastMoveDir, const StatBlock&, dashInvulnerable }` + `MobField&` + `std::mt19937&`.
  반환은 `CombatResult{ kills, damageToPlayer }` — XP·HP 반영은 `Simulation` 이 한다(전투가 진행도·HP 를 직접 안 만짐).
- `SnapshotBuilder` 는 `Simulation::Combat()` 의 const 참조로 `Instances()`/`Visuals()` 를 읽는다(지금 `Projectiles()` 와 같은 방식).
- 메인(시뮬) 스레드 전용. 인스턴스가 많아져 병렬화할 때는 1단계(위치 계산)만 `ParallelFor`(인스턴스마다 자기 칸, 규칙 6), 2·3단계는 순차.

### 2.1 판정 도형 `HitShape` (`game/HitShape.h`)

- `enum class HitShapeKind : uint8_t { Circle, Arc, Capsule }` + `struct HitShape { kind, center, dir, radius, halfAngle, end, halfWidth }`.
- `MobField` 공개 질의는 두 개로 고정한다: `DamageInShape(const HitShape&, float amount, HitReport&)`, `ClosestWithin(...)`(조준용).
  `DamageNearest` 는 BOLT 전용으로 남기되 내부 스캔은 공유. 기존 `DamageInRadius`/`DamageInArc`/`DamageInCapsule` 은 삭제.
  내부는 **스캔 루프 1개 + 모양별 거리식 테이블**(S4·S5).
- **몹 반경 포함 [확정 D3]**: 원 = `dist ≤ radius + r_mob`, 캡슐 = `선분거리 ≤ halfWidth + r_mob`, 부채꼴 = 거리 조건에 `+ r_mob`(각도는 중심 기준 — 단순함 우선).
- `HitReport{ kills, hitSlots[], hitCount }` — 슬롯 번호 + 세대(§2.2)와 킬 수. 흡혈(`ApplyLifeSteal`)은 이 결과를 받는 `ApplyHit` 한 곳에서(S7).
- 적 공격도 같은 `HitShape` 로 플레이어 원을 검사한다(대쉬 중이면 무시).
- `CollisionWorld2D` 로 옮기지 않는다: 몹 4096 SoA 에 매 스텝 콜라이더 재구성은 낭비이고, 질의형이 "탐지만" 규칙(아키텍처 규칙 8)과도 맞다.
  몹 수가 스캔 비용을 넘기면 `MobField` 안에 균일 그리드를 둔다(`MobField.h` 주석이 이미 예고).

### 2.2 투사체 · 인스턴스 `AttackInstance`

- 지금의 `Projectile` 을 `AttackInstance` 로 일반화(움직이는 장판도 같은 타입): `{ defIndex, team, t, θ0, dir, origin, prevPos, pos, tickLeft, piercesLeft, hitSlots[kMaxHitSlots] }`.
  **경로·크기·피해 매개변수는 들고 다니지 않는다** — `defIndex` 로 무기 정의에서 읽는다(R4. F5 재로드 시 색인 어긋남 주의점은 `CardInstance` 와 같음).
  단, 발사 순간 확정되는 값(크리 반영 피해, 공격 크기 배율)은 `damage`/`scale` 두 필드로 인스턴스에 둔다.
- 판정 반경 = `hit_radius × scale`. 접촉은 **지난 위치 → 새 위치 캡슐** 로(스윕, [확정 D4]) + 몹별 `m_radius`(P2·P8).
- `hitSlots`: 이미 맞은 몹의 `{슬롯, 세대, 마지막 타격 시각}`. 관통은 "목록에 있으면 제외"(P7, `kPierceClearDistance` 삭제),
  도는 공격은 "`rehit_interval` 지났으면 다시 허용". `MobField` 에 슬롯 세대 배열을 추가한다([entity-lifecycle-design.md](entity-lifecycle-design.md) 방식).
- `team`: `Player`(몹을 때림) / `Enemy`(플레이어를 때림, M3). 대상 선택은 이 두 case 뿐(R6).

### 2.3 연출 `AttackVisual`

- `PulseRing`/`BoltShot` 을 `AttackVisual{ HitShape shape; VisualKey key; float ageLeft, life; }` 하나로 통합.
  판정에 쓴 `HitShape` 를 그대로 복사하므로 **보이는 크기 = 맞는 크기**(P3·P4·S8). 그림 배율은 `visual_scale` 만([확정 D2]).
- `SnapshotBuilder` 는 **모양별 그리기 테이블**(원 = 점선 원, 부채꼴 = 부채꼴 테두리, 캡슐 = 두 선 + 반원)로 `worldQuads` 를 만든다.
  이동 중 인스턴스는 자기 위치에 `hit_radius` 원으로. M7 이후엔 `visualKey` → 스프라이트, 없으면 도형 폴백.
- 색 같은 표시값은 무기 행(`visual_*` 열) 또는 `fx` 표에서 — `SnapshotBuilder.cpp` 에 무기별 분기 없음(S2).

### 2.4 데이터 — `weapons.csv` 열 (이 문서의 **유일한** 열 정의)

기존 열(`name, effect, cooldown, damage, range, ...`)은 그대로 두고 아래를 추가한다. 빈 칸 = 기본값.
`circular-balance.md` 의 열 설명은 구현 때(W6·W13) 이 표를 옮겨 적고, 이 표는 그쪽 링크로 바꾼다(문서도 DRY).

| 열 | 값 | 기본 | 뜻 |
|---|---|---|---|
| `id` | 소문자·숫자·밑줄 | (필수) | 식별자, 이미지 이름 `wpn_<id>_icon`/`prj_<id>_NN` 의 연결 키 [확정 D1]. `characters.csv` 의 시작 무기 참조도 이걸로 옮긴다 |
| `effect` | 기존 7개 이름 | (필수) | **효과 이름 표**(`CircularBalance.cpp` `kEffectNames`)가 이름 → `{ form, shape, onHit }` 로 푼다 — 아래 표 |
| `hit_radius` | px | 8 | 이동하는 인스턴스의 판정 반경(투사체 크기, 움직이는 장판의 원 반경) |
| `visual_scale` | 배율 | 1.0 | 그림 = 판정 도형 × 이 값. 1 이 아니면 "일부러 다르게" |
| `sprite` / `fx_hit` | 이름 접두사 | 빈 칸 | 이동 중 프레임 / 명중 이펙트. 빈 칸 = 도형 폴백 (M7) |
| `path` | `none` · `straight` · `polar` | 투사체 `straight`, 그 외 `none` | 시간에 따른 위치(§2.5). `none` = 발사 지점에서 즉시 1회 |
| `anchor` | `player` · `cast` | `cast` | `polar` 의 중심: 매 스텝 플레이어 위치 / 발사 순간 위치 |
| `count` | 정수 ≥ 1 | 1 | 한 번에 만드는 수. `polar` 는 360°/count 간격. `extra_projectiles` 능력치가 더해짐 |
| `lifetime` | 초 | 0 | `polar` 의 수명(0 = 사거리로 끝남 — `straight` 기본). 끝나면 사라지고 다음 쿨다운에 다시 생김 [확정 D7] |
| `start_radius` / `radial_speed` / `angular_speed_deg` | px / px·s⁻¹ / °·s⁻¹ | 0 | `polar` 매개변수. `radial_speed` 0 = 궤도, > 0 = 바깥으로 퍼지는 소용돌이 |
| `tick_interval` | 초 | 0 | 이동하는 **장판**(효과 형태 Area)의 판정 간격. 틱마다 그 자리에서 도형 판정 + 연출 1개 |
| `rehit_interval` | 초 | 0 | 이동하는 **투사체**가 같은 몹을 다시 때리기까지 간격(0 = 다시 안 때림 = 관통 규칙) |

효과 이름 표 — 효과 이름 하나가 형태·도형·명중 처리를 한 번에 정한다(S3: 명중 처리 지식은 이 표 한 곳):

| `effect` | `form` | `shape` | `onHit` | 지금 무기 |
|---|---|---|---|---|
| `radialpulse` | Area | Circle | — | PULSE |
| `arcswing` | Area | Arc | — | 검 |
| `lineswing` | Area | Capsule | — | 채찍 |
| `nearestbolt` | Nearest | — | — | BOLT |
| `explodingbolt` | Projectile | Circle(`explode_radius`) | Explode | 스태프 |
| `piercingshot` | Projectile | — | Pierce | 단검 |
| `randomdamageshot` | Projectile | — | Random | 트럼프 |

`CardEffect` 열거는 이 표의 행 이름으로만 남고, 실행 코드는 `form`(3가지)과 `onHit`(3가지)로만 분기한다 → 7-case switch + 안쪽 switch 가 사라짐.

적 공격은 `mobs.csv`(M3, [circular-design.md](circular-design.md) §5.3)의 `projectile`·`zone_radius`·`telegraph_sec` 열이 같은 `effect`·`path` 체계로 들어간다.

### 2.5 공격 움직임 — 효과(형태) × 경로

| 축 | 값 (새 종류 = 열거자 + 테이블 한 항목) |
|---|---|
| **효과 형태 `AttackForm`** | `Area`(도형 안 전부) · `Nearest`(가까운 N체) · `Projectile`(닿으면 `onHit`) |
| **경로 `AttackPath`** | `None`(발사 지점, 즉시) · `Straight`(`origin + dir·speed·t`) · `Polar`(`anchor + 극좌표(start_radius + radial_speed·t, θ0 + angular_speed·t)`) — 이후 `Boomerang`·`Wave` 등 |

- **판정 시점은 형태가 정한다**(R2): `Projectile` = 매 스텝 스윕 접촉, `Area` + 경로 = `tick_interval` 마다 그 자리에서 판정. 별도 축 없음.
- **위치는 `t` 로 계산**한다(`pos += …` 누적 금지): 오차 누적이 없고 같은 입력 = 같은 궤적이라 헤드리스 검증이 쉽다.
- 이동하는 `Area` 의 부채꼴·캡슐 방향 = 경로의 진행 방향.
- 경로 없는 `Nearest` 는 지금처럼 즉시.

**요청된 세 패턴 = CSV 행 조합** (값은 [살] 초기값, 새 무기 3개는 **레벨업 풀에만** — 시작 무기 아님, 기초 설계 5종은 그대로 [확정 D5])

| 패턴 | `effect` | `path` | `anchor` | 주요 값 |
|---|---|---|---|---|
| 플레이어 주위를 도는 공격 | `piercingshot` | `polar` | `player` | `start_radius` 80, `radial_speed` 0, `angular_speed_deg` 180, `count` 3, `lifetime` 4, `rehit_interval` 0.5 |
| 소용돌이치며 바깥으로 퍼지는 공격 | `piercingshot` | `polar` | `cast` | `start_radius` 20, `radial_speed` 90, `angular_speed_deg` 240, `count` 4, `lifetime` 3, `rehit_interval` 0.5 |
| 럴커처럼 한 칸씩 전진하는 공격 | `radialpulse` | `straight` | — | 방향 = 가장 가까운 몹 [확정 D6], `tick_interval` 0.08, 칸 간격 = 속도 × 틱(≈ 40px), `range` 360, `hit_radius` 24 |

- 럴커 = "보이지 않는 점이 직선으로 이동하고, 틱마다 그 자리에 원 판정 + 가시 연출 1개" — 원 장판(`radialpulse`)이 직선 경로를 탄 것일 뿐, 새 코드 없음.
- 같은 조합으로 더: `radialpulse`+`polar`+`player` = 주위를 돌며 주기적으로 터지는 폭탄, `explodingbolt`+`polar` = 퍼지며 닿으면 폭발.
- 적도 같은 구조(W7): 보스 회전 탄막 = `polar` + `team=Enemy`, 마법 몹의 전진 장판 = 럴커 조합.
- 도는 공격은 `lifetime` 이 끝나면 사라지고 쿨다운마다 다시 생긴다 [확정 D7] — 쿨감(`attack_speed`) 능력치가 의미를 가진다.

---

## 3. 작업 목록

순서는 의존 순. 각 작업은 끝나면 빌드 `경고 0 / 오류 0` + 헤드리스 검증(`tools/run_balance_sim.bat`, 테스트 씬)으로 닫고,
**작업 전후 7개 무기의 20초 킬 수를 비교해 기록**한다(D3 로 반경이 들어가는 W1 만 의도적으로 달라짐).

| # | 작업 | 해결 | 주요 파일 | 완료 기준 | 선행 |
|---|---|---|---|---|---|
| ~~W0~~ | ~~결정 받기~~ — ✅ D1~D7 전부 A (§4) | — | — | — | — |
| W14 | **`CircularCombat` 분리** — `ExecuteCard`·`StepProjectiles`·`Projectile`·`PulseRing`·`BoltShot`·`ApplyLifeSteal` 을 옮김(동작 변경 없음) | S1·R7 | 새 `game/CircularCombat.*`(vcxproj 등록), `game/Simulation.*`, `game/SnapshotBuilder.cpp` | 킬 수 전후 동일 | — |
| W1 | `HitShape` + `MobField::DamageInShape`(스캔 1개 + 거리식 테이블, 몹 반경 포함), 옛 `Damage*` 3개 삭제 | P1·S4·S5 | 새 `game/HitShape.h`, `game/MobField.*` | 반경 포함 전후 킬 수 기록 | W14 |
| W15 | **효과 이름 표 → `{form, shape, onHit}`**, `ProjectileKind` 삭제, 실행 분기를 `form`·`onHit` 로 | S3 | `game/Card.h`, `game/CircularBalance.cpp`, `game/CircularCombat.*` | 이중 switch 없음, 킬 수 동일 | W1 |
| W2 | 발사 = "도형 만들기 → `ApplyHit`" 한 경로, 흡혈 1곳 | S7 | `game/CircularCombat.*` | `ApplyLifeSteal` 호출 1곳 | W15 |
| W3 | `AttackVisual` 통합 + 모양별 그리기 테이블(부채꼴·캡슐 신규) | P3·P4·S2·S8 | `game/CircularCombat.*`, `game/SnapshotBuilder.cpp` | 검 = 부채꼴, 채찍 = 폭 있는 선, `PulseRing`/`BoltShot` 삭제 | W2 |
| W16 | `EraseExpired` 도우미로 swap-remove 루프 통일 | S6 | `game/CircularCombat.*`(필요하면 `core/`) | 복제 루프 0 | W3 |
| W4 | `AttackInstance`: `hit_radius`·스윕 캡슐·몹별 반경 | P2·P8 | `game/CircularCombat.*` | 3000px/s 로 올려도 테스트 더미를 안 뚫음 | W1 |
| W5 | 슬롯 세대 + `hitSlots`(관통 중복 방지) | P7·S9 | `game/MobField.*`, `game/CircularCombat.*` | `kPierceClearDistance` 삭제, 큰 몹에 관통 2 가 1회만 맞음 | W4 |
| W10 | 경로 테이블 `AttackPath{None,Straight,Polar}`, `anchor`·`count`·`lifetime`, 위치 = `t` 식 | §2.5 | 새 `game/AttackPath.*`, `game/CircularCombat.*` | 기존 투사체가 `Straight` 로 킬 수 동일, 테스트 씬에서 궤도·소용돌이 확인 | W5 |
| W11 | `rehit_interval`(`hitSlots` 에 시각) | §2.5 | `game/CircularCombat.*` | 궤도가 더미를 초당 `1/rehit_interval` 회만 | W10 |
| W12 | 이동하는 `Area` — `tick_interval` 판정 + 틱마다 `AttackVisual` (럴커) | §2.5 | `game/CircularCombat.*`, `game/SnapshotBuilder.cpp` | 가시가 순서대로 전진, 칸마다 1회 피해 | W3·W10 |
| W6+W13 | `weapons.csv` 에 §2.4 열 전부 + 로더 + 폴백(`kCardDefs`) + 새 무기 3행(레벨업 풀만), `circular-art-guide.md` 의 id 규칙 확인 | P5·P6·S10 | `weapons.csv`, `characters.csv`, `game/CircularBalance.*`, `game/Card.h`, `circular-balance.md` | F5 로 크기·궤도 반경·회전 속도가 판정과 그림에 같이 반영 | W11·W12 |
| W7 | 적 공격 — `team=Enemy`, 플레이어 피격 + 대쉬 무적 | P9 | `game/CircularCombat.*`, `mobs.csv` | M3 의 원거리·마법 몹이 이 경로로만 공격 | W6+W13, M3 착수 |
| W8 | 이미지 — `atlas.groups` 에 `weapon`/`fx`, `visualKey` → `SpriteDraw`(없으면 도형) | 이미지 | `assets/atlas/atlas.groups`, `render/RenderSnapshot.h`, `game/SnapshotBuilder.cpp` | 아트 1장으로 투사체 1종이 스프라이트로 | W6+W13, **M7 월드 스프라이트 경로** |
| W9 | 문서 갱신 — 이 문서 상태, `circular-design.md` §3.2·§8·현재 위치, `circular-balance.md` 열(§2.4 이관), `collider-design.md` | — | `docs/*` | `tools\check_docs.ps1` 통과 | 매 단계 |

**순서**: W14 → W1 → W15 → W2 → W3 → W16 → W4 → W5 → W10 → W11 → W12 → W6+W13 → W7(M3) → W8(M7), W9 는 매 단계.
W14~W5 는 동작을 거의 안 바꾸는 정리(킬 수로 검증), W10~ 이 새 기능이다. M3(적 종류) 전에 W6+W13 까지 끝내는 게 좋다.

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

## 5. 사용 방법 (How to use) — 구조 완성(W6+W13) 후 기준

### 새 무기 추가 — CSV 한 행

`weapons.csv` 한 행(열 뜻은 §2.4). 기존 `effect` × `path` 조합이면 코드 수정 없음.

```csv
id,name,effect,...,hit_radius,visual_scale,sprite,fx_hit,path,anchor,count,lifetime,start_radius,radial_speed,angular_speed_deg,tick_interval,rehit_interval
blade,BLADE,piercingshot,...,10,1.0,prj_blade,fx_hit,polar,player,3,4.0,80,0,180,,0.5
vortex,VORTEX,piercingshot,...,10,1.0,prj_vortex,fx_hit,polar,cast,4,3.0,20,90,240,,0.5
spike,SPIKE,radialpulse,...,24,1.0,prj_spike,,straight,,1,,,,,0.08,
```

이미지는 `assets/src/weapon/prj_blade_00.png` … 로 넣고 `build\tools\atlas_pack.exe --group weapon`
(이름 규칙 [circular-art-guide.md](circular-art-guide.md)). 이미지가 없으면 판정 도형 그대로 그려진다.
F5 로 다시 읽으면 판정과 그림이 같이 바뀐다. 폴백 `kCardDefs` 는 새 무기가 시작 무기가 아니면 안 고쳐도 된다.

### 새 판정 도형 추가 (예: 도넛)

1. `HitShapeKind` 에 `Ring` + `HitShape` 에 필요한 필드.
2. `MobField` 거리식 테이블에 한 항목(몹 반경 포함 규칙 지킴).
3. `SnapshotBuilder` 모양별 그리기 테이블에 한 항목.
4. 효과 이름 표에 `{ "ringburst", Area, Ring, None }` 한 줄 → CSV 에서 `effect=ringburst`.

```cpp
// CircularBalance.cpp — 효과 이름 표. 실행 코드는 form/onHit 로만 분기하므로 여기 한 줄이 끝.
constexpr EffectEntry kEffectNames[] = {
    { "radialpulse", AttackForm::Area, HitShapeKind::Circle, OnHit::None },
    // ...
    { "ringburst",   AttackForm::Area, HitShapeKind::Ring,   OnHit::None },
};
```

### 새 경로 추가 (예: 부메랑)

1. `AttackPath` 에 `Boomerang`.
2. 경로 테이블에 위치 함수 한 항목 — `t` 만으로 위치를 돌려주는 순수 함수.
3. `path` 열 파서에 이름 한 줄. 판정·연출·명중 처리는 그대로 따라온다.

```cpp
// game/AttackPath.cpp — 종류별 if 대신 표 한 줄
math::Vec2 PathBoomerang(const CardDef& def, const AttackInstance& inst, math::Vec2 /*anchor*/)
{
    const float half = def.lifetime * 0.5f;
    const float d = def.projectileSpeed * (inst.t < half ? inst.t : def.lifetime - inst.t);   // 갔다가 돌아옴
    return inst.origin + inst.dir * d;
}
```

### 적 공격 추가 (M3)

`mobs.csv` 의 공격 열이 같은 `effect`·`path` 체계를 쓰고, 인스턴스는 `team=Enemy`. 플레이어 피격은 `ResolveHits` 의 Enemy case 한 곳에서
대쉬 무적을 확인한다 — 몹 종류별로 따로 검사하지 않는다.

### 하지 말 것

- 판정과 그림에 **다른 숫자**를 쓰지 말 것 — 그림 크기는 `HitShape` × `visual_scale` 에서만.
- `SnapshotBuilder` 에 `if (kind == …) color = …` 식 무기별 분기를 늘리지 말 것 — 표(CSV/`visualKey`)로.
- 움직임마다 새 `effect` 를 만들지 말 것 — 움직임은 `path` 열, 효과는 "무엇을 어떤 도형으로"만.
- 경로 함수에서 위치를 `pos += …` 로 누적하지 말 것 — `t` 로 계산(결정론·헤드리스 재현).
- 인스턴스에 무기 매개변수를 복사하지 말 것 — `defIndex` 로 읽는다(발사 순간 확정값 `damage`/`scale` 만 예외).
- 전투 코드를 `Simulation` 에 다시 추가하지 말 것 — `CircularCombat` 에.
- 무기 판정을 `CollisionWorld2D` 에 몹 4096개 콜라이더로 넣지 말 것 — `MobField` 질의가 계약이다(§2.1).
- 판정 함수 안에서 몹을 밀거나 움직이지 말 것 — 넉백이 필요하면 `HitReport` 를 받아 별도 단계에서.
- 대상이 둘뿐인데 `IHitTarget` 같은 인터페이스를 만들지 말 것 — 세 번째 대상이 생기면 그때(KISS).
- 무기 숫자를 `Card.h`/`CircularCombat.cpp` 에 하드코딩하지 말 것 — `kCardDefs` 는 폴백일 뿐([circular-balance.md](circular-balance.md)).
