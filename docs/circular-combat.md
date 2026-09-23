# 서큘러 전투 판정 · 연출 구조 — 설계 작업 정리

> 상태: **설계 작업 목록(구현 전)**. 무기·적 공격의 **판정 도형**, **연출(도형 → 이미지)**, **데이터(CSV)** 를 한 구조로 묶기 위해
> 해야 할 일을 정리한다. 이 문서의 제안은 전부 **[살]** — 기초 설계([# Circular 기초 설계.md](<# Circular 기초 설계.md>))와
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
- 이미지: 패킹 도구(`tools/atlas_pack`)와 이름 규칙(`wpn_<id>_icon`, `prj_<id>_NN`, `fx_*`)은 있지만
  `assets/atlas/atlas.groups` 에 `weapon`/`fx` 그룹이 없고, `RenderSnapshot` 에 월드 스프라이트 배열이 없다(`uiSprites` 만) — M7.

### 1.2 발견된 문제

| # | 문제 | 위치 | 영향 |
|---|---|---|---|
| P1 | 원·부채꼴·캡슐 판정이 **몹 중심점만** 검사(몹 반경 무시) | `MobField.cpp` `DamageInRadius`/`DamageInArc`/`DamageInCapsule` | 몹 가장자리가 범위에 걸쳐도 안 맞음. 몹 크기가 종류별로 달라지면(M3) 큰 몹이 부당하게 덜 맞음 |
| P2 | 투사체 접촉 거리 = 전역 `mob_radius` + `kProjectileHitReach`(6) | `Simulation::StepProjectiles` | 투사체 자신의 크기 개념이 없고, 몹별 `m_radius` 를 안 씀 |
| P3 | **그리는 크기 ≠ 판정 크기** — 투사체 12px, BOLT 14px, 점선 원 굵기가 `SnapshotBuilder.cpp` 에 하드코딩 | `SnapshotBuilder.cpp` 830~855줄 부근 | 이미지를 붙이면 "보이는 것과 맞는 것"이 어긋남 |
| P4 | 검·채찍 연출이 판정 모양과 다름(원/사각형 재사용) | `ExecuteCard` 의 `ArcSwing`/`LineSwing` case | 플레이어가 공격 범위를 읽을 수 없음 |
| P5 | 이미지 이름 규칙은 `weapons.csv` 의 `id` 를 요구하지만 **그 열이 없다**(식별자는 `name`) | `circular-art-guide.md` 78줄, `weapons.csv` | 무기 행 ↔ 이미지 연결 키가 없음 |
| P6 | 무기 행에 이미지·클립 이름 열이 없다 | `weapons.csv`, `CardDef` | 이미지 연결을 코드(`if kind==…` 색 분기)로 할 위험 — OCP 위반 |
| P7 | 관통 중복 타격 방지가 "명중 후 24px 전진" 근사(`kPierceClearDistance`) | `Simulation.h` | 큰 몹·빠른 몹에서 같은 몹 재타격 또는 뒤 몹 누락 가능 |
| P8 | 투사체는 스텝마다 **점**으로만 검사(이동 구간 스윕 없음) | `StepProjectiles` | 지금 수치는 안전(단검 900px/s ÷ 60Hz = 15px/스텝 < 판정 지름 32px). 투사체 속도·작은 몹이 생기면 뚫고 지나감 |
| P9 | 적 공격(M3)이 같은 판정 도형을 재사용할 자리가 없다 | — | M3 에서 몹용 판정을 따로 만들면 중복 |

---

## 2. 목표 구조 [살]

한 줄 요약: **"판정 도형 값(`HitShape`) 하나를 판정과 연출이 같이 쓴다."** 무기·적 공격 모두 이 값으로 표현하고,
종류가 늘어나는 것(모양·투사체 행동·연출)은 열거 + 테이블로 둔다(`CLAUDE.md` OCP — 종류별 if 사슬 금지).

```text
weapons.csv / mobs.csv 행 ──(로더)──▶ CardDef / MobDef  (hit 크기 · visual 크기 · sprite 키)
        │
ExecuteCard / 몹 공격 커널 ──▶ HitShape{Circle|Arc|Capsule} 값  ──┬─▶ MobField::DamageInShape (몹 반경 포함)
        │                                                        │   또는 플레이어 피격 판정(대쉬 무적 확인)
        │                                                        └─▶ AttackVisual{shape, visualKey, age}  (연출 값)
        ▼
Projectile{shape(원 반경), team, kind, ...} ─ StepProjectiles: 이동 구간 스윕 캡슐로 판정
        ▼
SnapshotBuilder: AttackVisual/Projectile → (아트 없음) 도형 worldQuads / (M7) SpriteDraw — 크기는 shape 에서
```

### 2.1 판정 도형 `HitShape` (게임 쪽 값 타입, `game/HitShape.h` 예정)

- `enum class HitShapeKind : uint8_t { Circle, Arc, Capsule }` + `struct HitShape { kind, center, dir, radius, halfAngle, end, halfWidth }`.
- `MobField::DamageInShape(const HitShape&, float amount, HitReport&)` 하나로 통합. 기존 `DamageInRadius`/`DamageInArc`/`DamageInCapsule` 은
  이 함수의 내부 분기(모양별 거리 함수 테이블)로 흡수하거나 얇은 래퍼로 남긴다.
- **몹 반경 포함**: 원 = `dist ≤ radius + r_mob`, 캡슐 = `선분거리 ≤ halfWidth + r_mob`, 부채꼴 = 거리 조건에 `+ r_mob`(각도는 중심 기준 유지 — 단순함 우선).
- 적 공격(M3)도 같은 `HitShape` 로 플레이어를 검사한다(플레이어는 원 하나, 대쉬 중이면 무적으로 무시).
- `CollisionWorld2D` 로 옮기지 않는다: 몹 4096 SoA 에 매 스텝 콜라이더 재구성은 낭비이고, 판정은 "탐지만" 규칙(아키텍처 규칙 8)과도 맞는
  질의형이 낫다. 몹 수가 스캔 비용을 넘기면 `MobField` 안에 균일 그리드를 둔다(`MobField.h` 주석이 이미 예고).

### 2.2 투사체

- `Projectile` 에 `hitRadius`(투사체 자신의 판정 반경), `team`(Player/Enemy — M3 원거리 몹이 재사용), `visualKey` 추가.
- 판정은 **이번 스텝 이동 구간 = 캡슐(이전 위치 → 현재 위치, 반폭 `hitRadius`)** 로 → P8 해결. 접촉 거리는 `hitRadius + 몹별 m_radius` → P2 해결.
- 관통 중복 방지: 투사체마다 작은 고정 배열 `hitSlots[kMaxPierce]`(이미 맞은 몹 슬롯 번호)를 두고 제외 → P7 해결.
  이를 위해 `MobField` 질의가 슬롯 번호를 돌려줘야 한다(지금은 위치만). 슬롯 재사용(죽고 새로 스폰)은 세대 번호로 구분
  ([entity-lifecycle-design.md](entity-lifecycle-design.md) 방식).
- `ProjectileKind`(Piercing/Exploding/Random)의 명중 처리 switch 는 유지 — 새 행동 = 열거자 + case.

### 2.3 연출 `AttackVisual`

- `PulseRing`/`BoltShot` 두 전용 타입을 `AttackVisual{ HitShape shape; VisualKey key; float ageLeft, life; }` 하나로 통합.
  판정에 쓴 `HitShape` 를 그대로 복사하므로 **보이는 크기 = 맞는 크기**(P3·P4 해결).
- `SnapshotBuilder` 는 모양별 그리기 함수 테이블(원 = 점선 원, 부채꼴 = 부채꼴 테두리, 캡슐 = 두 선 + 반원)로 `worldQuads` 를 만든다.
  M7 이후엔 `visualKey` 로 스프라이트를 찾고, 없으면 도형으로 폴백.
- 색·굵기 같은 표시값도 `SnapshotBuilder.cpp` 상수가 아니라 표(무기 행의 `visual_*` 열 또는 `fx` 표)로.

### 2.4 데이터 (`weapons.csv` 열 추가 계획)

| 열 | 뜻 | 소비처 |
|---|---|---|
| `id` | 소문자 식별자(`sword`, `staff`…). 이미지 이름 `wpn_<id>_icon`/`prj_<id>_NN` 의 연결 키 | 이미지·`characters.csv` 참조(장기적으로 `name` 대신) |
| `hit_radius` | 투사체 판정 반경 px (투사체 무기만) | `Projectile.hitRadius` |
| `visual_scale` | 판정 도형 대비 그림 배율(기본 1.0 — 1 이 아니면 "일부러 다르게"임을 표에서 보이게) | `SnapshotBuilder` |
| `sprite` / `fx_hit` | 투사체·스윙 프레임 접두사, 명중 이펙트 접두사(비면 도형 폴백) | M7 스프라이트 경로 |

적 공격은 `mobs.csv`(M3, [circular-design.md](circular-design.md) §5.3)의 `projectile`·`zone_radius`·`telegraph_sec` 열이 같은 `HitShape`/`Projectile` 로 들어간다.

### 2.5 공격 움직임 패턴 — 경로 × 판정 시점 × 명중 처리

지금 투사체는 "직선으로 날아가다 닿으면 처리" 한 가지뿐이다(`pos += vel·dt`). 플레이어 주위를 도는 공격, 소용돌이치며 퍼지는 공격,
럴커처럼 한 칸씩 전진하는 공격을 **종류별 코드 없이** 만들려면 공격 하나를 세 축의 조합으로 본다.

| 축 | 뜻 | 값 (열거 — 새 종류 = 열거자 + 테이블 한 항목) |
|---|---|---|
| **경로 `AttackPath`** | 시간 t 에 따라 도형이 어디 있나 | `Straight`(직선) · `Orbit`(원 궤도) · `Spiral`(궤도 + 반경 증가) · 이후 `Homing`·`Boomerang`·`Wave` 등 |
| **판정 시점 `Delivery`** | 언제 판정하나 | `Continuous`(매 스텝, 지나간 구간 스윕) · `Pulsed`(`tick_interval` 마다 그 지점에서만 한 번) |
| **명중 처리 `ProjectileKind`** | 맞았을 때 무엇을 하나 | 지금 있는 `Piercing`·`Exploding`·`Random` 그대로(§2.2) |

- **경로는 위치를 적분하지 않고 식으로 계산한다**: `pos(t) = anchor + 극좌표(r(t), θ(t))`, `r(t) = start_radius + radial_speed·t`, `θ(t) = θ0 + angular_speed·t`.
  직선은 `anchor + dir·speed·t`. 누적 오차가 없고, 같은 입력이면 같은 궤적이라 헤드리스 검증이 쉽다.
- **`anchor`**: `Player`(매 스텝 플레이어 중심을 다시 읽음 — 플레이어를 따라다니는 궤도) / `Cast`(발사 순간 위치에 고정 — 제자리 소용돌이).
- **여러 발**: `count` 발을 `θ0` 를 360°/count 간격으로 벌려 한 번에 만든다. `extra_projectiles` 능력치가 `count` 에 더해진다.
- **수명**: 직선이 아닌 경로는 사거리(`rangeLeft`)가 아니라 `lifetime` 초로 끝난다(직선은 지금처럼 사거리).
- **같은 몹 반복 타격**: 도는 공격은 같은 몹을 계속 스친다 → 몹마다 `rehit_interval` 초가 지나야 다시 맞는다.
  W5 의 `hitSlots`(슬롯 번호 + 세대)에 "마지막으로 때린 시각"을 붙여 쓴다(관통 중복 방지와 같은 장치).

**요청한 세 패턴의 조합**

| 패턴 | 경로 | anchor | 판정 시점 | 판정 도형 | 주요 값 |
|---|---|---|---|---|---|
| 플레이어 주위를 도는 공격 | `Orbit` | `Player` | `Continuous` | 원(`hit_radius`) | `start_radius` 80, `angular_speed_deg` 180, `count` 3, `lifetime` 4, `rehit_interval` 0.5 |
| 소용돌이치며 바깥으로 퍼지는 공격 | `Spiral` | `Cast`(또는 `Player`) | `Continuous` | 원 | `start_radius` 20, `radial_speed` 90, `angular_speed_deg` 240, `count` 4, `lifetime` 3 |
| 럴커처럼 한 칸씩 전진하는 공격 | `Straight` | `Cast` | `Pulsed` | 원(칸마다 가시 하나) | 방향 = 가장 가까운 몹, `tick_interval` 0.08, 칸 간격 = `speed·tick_interval`(≈ 40px), `range` 360 |

- 럴커 공격은 "보이지 않는 점이 직선으로 이동하고, `tick_interval` 마다 그 자리에 원 판정 + `AttackVisual`(가시 이미지) 하나를 남기는" 것이다.
  새 개념 없이 경로 `Straight` + 판정 시점 `Pulsed` 조합이다. 칸마다 남는 가시는 §2.3 의 `AttackVisual` 이 수명을 두고 사라진다.
- 같은 조합으로 더 만들 수 있는 것: `Orbit`+`Pulsed` = 주위를 돌며 일정 간격으로 터지는 폭탄, `Spiral`+`Pulsed`+`Exploding` = 퍼져 나가며 연속 폭발.
- 적도 같은 구조를 쓴다(W7): 보스의 회전 탄막 = `Orbit`/`Spiral` + `team=Enemy`, 마법 몹의 전진 장판 = 럴커 조합.

**데이터 흐름**

```text
weapons.csv 행: path, anchor, delivery, count, lifetime, start_radius, radial_speed, angular_speed_deg, tick_interval, rehit_interval
      │ (로더)
      ▼
CardDef.motion (MotionDef 값) ──ExecuteCard──▶ count 개의 Projectile{ path, anchor, t, θ0, dir, ... }
                                                   │ StepProjectiles: t += dt
                                                   │   pos = kPathTable[path](motion, anchorPos, t)     ← 종류별 if 없음
                                                   │   Continuous: 이전 pos → 새 pos 캡슐로 판정
                                                   │   Pulsed    : tick 마다 새 pos 에서 원 판정 + AttackVisual
                                                   ▼
                                     명중 처리 switch(ProjectileKind) — 그대로
```

- `Projectile` 에 `MotionState{ path, anchor, t, θ0, dir, origin }` 를 붙인다(값 타입, 포인터 없음). 크기가 커지면 경로 매개변수는
  `CardDef` 색인만 들고 표에서 읽는다(투사체 512개 × 필드 수 고려).
- 스레드: 전부 메인(시뮬) 스레드의 `StepProjectiles` 안. 투사체 수가 늘어 병렬화하면 위치 계산만 `ParallelFor`(투사체마다 자기 칸만 씀, 규칙 6),
  판정·피해는 그 뒤 메인에서 순차.

---

## 3. 작업 목록

순서는 의존 순. 각 작업은 끝나면 빌드 `경고 0 / 오류 0` + 헤드리스 검증(`tools/run_balance_sim.bat`, 테스트 씬)으로 닫는다.

| # | 작업 | 해결 | 주요 파일 | 완료 기준 | 선행 |
|---|---|---|---|---|---|
| W0 | **결정 받기** — §4 D1~D4 | — | 이 문서 | 사용자 답을 §4 에 [확정] 으로 기록 | — |
| W1 | `HitShape` 값 타입 + `MobField::DamageInShape`(몹 반경 포함), 기존 질의 흡수 | P1 | 새 `game/HitShape.h`, `game/MobField.*` | 7개 무기 결과가 도형 테스트로 재현, 반경 포함 전후 킬 수 비교 기록 | W0(D3) |
| W2 | `ExecuteCard` 가 `HitShape` 를 만들어 판정 — case 는 "도형 만들기"만 | P1 | `game/Simulation.cpp` | switch 는 유지, 판정 호출은 한 곳 | W1 |
| W3 | `AttackVisual` 통합 + `SnapshotBuilder` 모양별 그리기 테이블(부채꼴·캡슐 도형 신규) | P3·P4 | `game/Simulation.*`, `game/SnapshotBuilder.cpp` | 검 = 부채꼴, 채찍 = 폭 있는 선으로 보임, `PulseRing`/`BoltShot` 삭제 | W2 |
| W4 | 투사체: `hitRadius`·스윕 캡슐 판정·몹별 반경 | P2·P8 | `game/Simulation.*` | 속도 3000px/s 로 올려도 테스트 더미를 뚫지 않음 | W1 |
| W5 | 관통 중복 방지 — 슬롯 번호 + 세대 반환, `hitSlots` | P7 | `game/MobField.*`, `game/Simulation.*` | `kPierceClearDistance` 삭제, 큰 몹 1마리에 관통 2 가 1회만 맞음 | W4 |
| W6 | `weapons.csv` 에 `id`·`hit_radius`·`visual_scale`·`sprite`·`fx_hit` + 로더 | P5·P6 | `assets/data/circular/weapons.csv`, `game/CircularBalance.*`, `game/Card.h` | F5 로 크기 바꾸면 판정·그림이 같이 바뀜, `circular-balance.md` 열 설명 갱신 | W3·W4, D1·D2 |
| W7 | 적 공격이 같은 구조 재사용 — `Projectile.team`, 몹 장판 = 예고 후 `HitShape`, 플레이어 피격 + 대쉬 무적 | P9 | `game/Simulation.*`, `game/MobField.*`, `mobs.csv` | M3 의 원거리·마법 몹이 이 경로로만 공격 | W4, M3 착수 |
| W8 | 이미지 연결 — `atlas.groups` 에 `weapon`/`fx` 그룹, `visualKey` → `SpriteDraw`(없으면 도형 폴백) | 이미지 | `assets/atlas/atlas.groups`, `render/RenderSnapshot.h`, `game/SnapshotBuilder.cpp` | 아트 1장으로 투사체 1종이 스프라이트로 보임 | W6, **M7 월드 스프라이트 경로** |
| W10 | 경로 테이블 — `AttackPath{Straight,Orbit,Spiral}` + `MotionDef`/`MotionState`, `Projectile` 위치를 식으로 계산, `anchor`, `count`, `lifetime` | §2.5 | 새 `game/AttackMotion.h`, `game/Simulation.*`, `game/Card.h` | 기존 투사체 3종이 `Straight` 로 결과 동일(킬 수 비교), 테스트 씬에서 궤도·소용돌이가 보임 | W4 |
| W11 | 반복 타격 간격 — `hitSlots` 에 마지막 타격 시각, `rehit_interval` | §2.5 | `game/Simulation.*` | 궤도 공격이 더미를 초당 `1/rehit_interval` 회만 때림 | W5·W10 |
| W12 | 판정 시점 `Pulsed` — `tick_interval` 마다 원 판정 + 칸마다 `AttackVisual` (럴커) | §2.5 | `game/Simulation.*`, `game/SnapshotBuilder.cpp` | 가시가 순서대로 전진하며 칸마다 한 번씩 피해 | W3·W10 |
| W13 | `weapons.csv` 에 움직임 열(`path, anchor, delivery, count, lifetime, start_radius, radial_speed, angular_speed_deg, tick_interval, rehit_interval`) + 예시 무기 3행(궤도·소용돌이·럴커 — 레벨업 풀에만) | §2.5 | `weapons.csv`, `game/CircularBalance.*`, `circular-balance.md` | CSV 만 고쳐 F5 로 궤도 반경·회전 속도가 바뀜 | W10~W12, D5~D7 |
| W9 | 문서 갱신 — 이 문서 §5 를 "구현됨" 으로, `circular-design.md` §3.2·§8·현재 위치, `circular-art-guide.md`(id 열), `collider-design.md` 링크 | — | `docs/*` | `tools\check_docs.ps1` 통과 | 각 W 완료 때마다 |

W1~W6 은 M3(적 종류) **전에** 하는 게 좋다 — M3 가 W7 로 같은 구조를 바로 쓴다. W8 은 M7 과 같이 간다.
움직임 패턴(W10~W13)은 W4·W5 뒤, W7 전에 — 보스·마법 몹 공격이 같은 경로 테이블을 쓰게 하려면 먼저 있어야 한다.
권장 순서: W0 → W1 → W2 → W3 → W4 → W5 → W10 → W11 → W12 → W6+W13(CSV 한 번에) → W7(M3) → W8(M7), W9 는 매 단계.

---

## 4. 판단 대기

| # | 질문 | 권장 (먼저) | 대안 |
|---|---|---|---|
| D1 | 무기 ↔ 이미지 연결 키 | **A**: `weapons.csv` 에 `id` 열 추가, 아트 가이드 그대로 | B: `name` 소문자를 id 로 간주(가이드를 수정) |
| D2 | 판정 크기와 그림 크기 | **A**: 판정 도형이 기준, 그림은 `visual_scale` 배율(기본 1) | B: 그림 크기 따로 지정(`visual_size` px) — 어긋남 허용 |
| D3 | 장판·스윙에 몹 반경 포함 | **A**: 포함(체감상 자연스러움, 킬 수↑ → 밸런스 재조정 필요) | B: 중심점 유지(지금 밸런스 보존) |
| D4 | 투사체 스윕 판정 도입 시점 | **A**: W4 에서 바로(비용 작음, 미래 고속 투사체 대비) | B: 속도가 판정 지름을 넘을 때까지 보류 |
| D5 | 움직임 패턴 무기를 기존 5종과 어떻게 두나 (기초 설계의 무기는 5종) | **A**: 새 무기 행으로 추가하되 **레벨업 풀에만**(시작 무기 아님, PULSE/BOLT 와 같은 위치) — 헌법의 5종은 그대로 | B: 새 무기 없이 기존 5종의 레벨/진화 효과로만 사용(예: 스태프 Lv5 = 소용돌이) |
| D6 | 럴커 공격의 방향 | **A**: 가장 가까운 몹(투사체 조준과 같은 `ClosestWithin`) | B: 마지막 이동 방향(`m_lastMoveDir`, 검·채찍과 같음) |
| D7 | 궤도 공격의 수명 | **A**: `lifetime` 후 사라지고 쿨다운마다 다시 생김(뱀서 방식, 쿨감 능력치가 의미 있음) | B: 한 번 생기면 계속 유지(쿨다운 무시) |

---

## 5. 사용 방법 (How to use) — 구조 완성(W1~W6) 후 기준

### 새 무기 추가 (기존 모양 재사용)

`weapons.csv` 한 행만 추가한다. 코드 수정 없음.

```csv
id,name,effect,cooldown,damage,range,...,hit_radius,visual_scale,sprite,fx_hit
spear,SPEAR,lineswing,0.8,18,200,...,,1.0,prj_spear,fx_hit
```

이미지는 `assets/src/weapon/prj_spear_00.png` … 로 넣고 `build\tools\atlas_pack.exe --group weapon`
(이름 규칙 [circular-art-guide.md](circular-art-guide.md)). 이미지가 없으면 판정 도형 그대로 그려진다.

### 새 판정 모양 추가 (예: 도넛)

1. `HitShapeKind` 에 `Ring` 열거자 추가 + `HitShape` 에 필요한 필드.
2. `MobField` 의 모양별 거리 함수 테이블에 한 항목(몹 반경 포함 규칙 지킴).
3. `SnapshotBuilder` 의 모양별 그리기 테이블에 한 항목.
4. 그 모양을 쓰는 `CardEffect` 열거자 + `ExecuteCard` case(도형 만들기만).

```cpp
case CardEffect::RingBurst:
{
    const HitShape shape = HitShape::Ring(playerCenter, innerRadius, range);
    return ApplyHit(shape, damage, VisualKey::FromDef(def));   // 판정 + AttackVisual 을 한 번에
}
```

### 움직이는 공격 추가 (W10~W13 후) — CSV 행만

기존 경로·판정 시점을 조합하면 코드 수정 없음(열은 §2.5, 빈 칸 = 기본값).

```csv
id,name,effect,...,path,anchor,delivery,count,lifetime,start_radius,radial_speed,angular_speed_deg,tick_interval,rehit_interval
blade,BLADE,piercingshot,...,orbit,player,continuous,3,4.0,80,0,180,,0.5
vortex,VORTEX,explodingbolt,...,spiral,cast,continuous,4,3.0,20,90,240,,0.5
spike,SPIKE,piercingshot,...,straight,cast,pulsed,1,,,,,0.08,
```

- `effect` 는 기존 투사체 효과(`piercingshot`/`explodingbolt`/`randomdamageshot`)를 그대로 쓴다 = 명중 처리. 움직임 전용 효과를 새로 만들지 않는다.
  `lifetime` 이 있는 경로(궤도·소용돌이)는 관통 수 대신 수명으로 끝난다(관통 수 무시, 반복 타격은 `rehit_interval`).

### 새 경로 추가 (예: 부메랑)

1. `AttackPath` 에 `Boomerang` 열거자.
2. 경로 테이블에 위치 함수 한 항목 — `t` 만으로 위치를 돌려주는 순수 함수(상태 누적 금지).
3. `weapons.csv` 의 `path` 파서에 이름 한 줄. 끝 — 판정·연출·명중 처리는 그대로 따라온다.

```cpp
// game/AttackMotion.cpp — 종류별 if 대신 표 한 줄
math::Vec2 PathBoomerang(const MotionDef& m, const MotionState& s, math::Vec2 anchor)
{
    const float half = m.lifetime * 0.5f;
    const float d = m.speed * (s.t < half ? s.t : m.lifetime - s.t);   // 갔다가 돌아옴
    return anchor + s.dir * d;
}
```

### 적 공격 추가 (M3)

`mobs.csv` 에 `projectile`/`zone_radius`/`telegraph_sec` 를 채우면 원거리 = `Projectile{team=Enemy}`, 마법 = 예고 후 `HitShape`.
플레이어 피격은 한 함수에서 대쉬 무적을 확인한다 — 몹 종류별로 따로 검사하지 않는다.

### 하지 말 것

- 판정과 그림에 **다른 숫자**를 쓰지 말 것 — 그림 크기는 `HitShape` + `visual_scale` 에서만 나온다.
- `SnapshotBuilder` 에 `if (kind == …) color = …` 식 무기별 분기를 늘리지 말 것 — 표(CSV/`visualKey`)로.
- 무기 판정을 `CollisionWorld2D` 에 몹 4096개 콜라이더로 넣지 말 것 — `MobField` 질의가 계약이다(§2.1).
- 움직임 패턴마다 `CardEffect`/`ProjectileKind` 를 새로 만들지 말 것 — 움직임은 `path`×`delivery` 조합, 명중 처리만 `ProjectileKind`.
- 경로 함수에서 위치를 `pos += …` 로 누적하지 말 것 — `t` 로 계산(결정론·헤드리스 재현).
- 판정 함수 안에서 몹을 밀거나 움직이지 말 것 — 넉백이 필요하면 판정 결과(`HitReport`)를 받아 별도 단계에서.
- 무기 숫자를 `Card.h`/`Simulation.cpp` 에 하드코딩하지 말 것 — `kCardDefs` 는 폴백일 뿐([circular-balance.md](circular-balance.md)).
